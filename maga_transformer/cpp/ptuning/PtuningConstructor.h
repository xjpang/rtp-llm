#pragma once
#include <assert.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <optional>
#include <vector>
#include <map>
#include <torch/torch.h>
#include "maga_transformer/cpp/dataclass/Query.h"
#include "maga_transformer/cpp/dataclass/GenerateStream.h"
#include "maga_transformer/cpp/dataclass/GenerateConfig.h"
#include "maga_transformer/cpp/cache/CacheManager.h"
#include "maga_transformer/cpp/ptuning/Ptuning.h"
#include "src/fastertransformer/core/Buffer.h"
#include "maga_transformer/cpp/engines/NormalEngine.h"

namespace rtp_llm {

class PtuningConstructor {
public:
    std::map<int, PrefixParams> construct(const GptInitParameter& params, NormalEngine* engine) {
        if (!params.multi_task_prompt_tokens.empty()) {
            return createMultiTaskPrompt(params.multi_task_prompt_tokens, engine);
        }
        return std::map<int, PrefixParams>();
    }

    static PrefixParams createPtuningV2(const GptInitParameter& params, CacheManager* cache_manager, torch::Tensor& prefix_prompt) {        
        size_t prefix_seq_length = prefix_prompt.size(-2);
        // Reshape prefix_prompt to [layer_num, 2, head_num_kv, pre_seq_len, size_per_head]
        prefix_prompt = prefix_prompt.view({params.num_layers_, 2, prefix_prompt.size(1), prefix_prompt.size(2), prefix_prompt.size(3)})
                            .permute({1, 0, 3, 2, 4})
                            .contiguous();
        
        // Compute prefix_blocks
        int64_t prefix_blocks = (prefix_seq_length - 1) / params.seq_size_per_block_ + 1;
        
        // Allocate prefix_block_indice using cache_manager
        // The actual implementation of `malloc` in the `cache_manager` object needs to be provided.
        auto [success, prefix_block_indice] = cache_manager->mallocIndex(prefix_blocks);

        if (!success) {
            std::cout << "malloc kv cache block failed" << std::endl;
            abort();
        }
        setKVPrefixBlock(params, cache_manager, prefix_prompt, prefix_block_indice);
        std::vector<int> prefix_tokens;
        return PrefixParams(PrefixType::PTuningV2, prefix_seq_length, prefix_block_indice, prefix_prompt, prefix_tokens);
    }

    static void setKVPrefixBlock(const GptInitParameter& params, CacheManager* cache_manager,
                                    torch::Tensor& kv_prefix_prompt, std::vector<int>& prefix_block_indice) {
        // Access first and second element of kv_prefix_prompt
        auto k_prefix_prompt = kv_prefix_prompt[0];
        auto v_prefix_prompt = kv_prefix_prompt[1];
        
        // Extract dimensions
        int64_t layer_num = k_prefix_prompt.size(0);
        int64_t pre_seq_len = k_prefix_prompt.size(1);
        int64_t head_num = k_prefix_prompt.size(2);
        int64_t size_per_head = k_prefix_prompt.size(3);
        int64_t block_indice_length = prefix_block_indice.size();
        int64_t append_length = block_indice_length * params.seq_size_per_block_ - pre_seq_len;
        
        // Create a zeros tensor of the required size
        auto blank_tensor = torch::zeros({layer_num, append_length, head_num, size_per_head}, k_prefix_prompt.options());

        // Concatenate k and v prefix prompts with the blank tensor
        auto tiled_k_prefix_prompt = torch::cat({k_prefix_prompt, blank_tensor}, 1);
        auto tiled_v_prefix_prompt = torch::cat({v_prefix_prompt, blank_tensor}, 1);

        // Reshape and permute the k and v prefix prompts
        tiled_k_prefix_prompt = tiled_k_prefix_prompt.view({layer_num, block_indice_length, params.seq_size_per_block_, head_num, size_per_head})
                                                    .permute({0, 1, 3, 2, 4})
                                                    .contiguous();
        tiled_v_prefix_prompt = tiled_v_prefix_prompt.view({layer_num, block_indice_length, params.seq_size_per_block_, head_num, size_per_head})
                                                    .permute({0, 1, 3, 2, 4})
                                                    .contiguous();

        // Set the cached k and v values
        for (int i = 0; i < block_indice_length; ++i) {
            auto k_tensor = tiled_k_prefix_prompt.index({torch::indexing::Slice(), i});
            auto v_tensor = tiled_v_prefix_prompt.index({torch::indexing::Slice(), i});
            auto k_buffer = torchTensor2Buffer(k_tensor);
            auto v_buffer = torchTensor2Buffer(v_tensor);

            cache_manager->setKVBlockValue(prefix_block_indice[i], k_buffer, v_buffer);
        }
    }

    std::map<int, PrefixParams> createMultiTaskPrompt(std::map<int, std::vector<int>> multi_task_prompt_tokens, NormalEngine* engine) {
        std::map<int, PrefixParams> multi_task_prompt_args;
        for (const auto& item: multi_task_prompt_tokens) {
            const auto& task_id = item.first;
            const auto& tokens_id = item.second;

            std::shared_ptr<GenerateInput> generate_input(new GenerateInput());
            std::shared_ptr<GenerateConfig> generate_config(new GenerateConfig());
            generate_config->max_new_tokens = 1;
            std::vector<size_t> shape = {tokens_id.size()};
            generate_input->input_ids = std::make_unique<ft::Buffer>(ft::MEMORY_CPU, ft::TYPE_INT32, shape, (void *)(tokens_id.data()));
            generate_input->generate_config = generate_config;
            
            // TODO(xinfei.sxf) consider tp
            GenerateStreamPtr stream = std::make_shared<GenerateStream>(generate_input);
            engine->enqueue(stream);
            engine->step();
            const auto& kv_cache = stream->kvCache();
            assert(kv_cache.k_ptr.size() == 1);
            assert(kv_cache.k_ptr[0].size() > 0);
            assert(stream->iter_count != 0);
            auto block_indices = engine->cacheManager()->convertAddrToIndex(kv_cache.k_ptr[0][0]);
            std::optional<torch::Tensor> prefix_tensor = std::nullopt;
            multi_task_prompt_args[task_id] = PrefixParams(PrefixType::PromptTuning,
                                            tokens_id.size(), block_indices, prefix_tensor, tokens_id);
        }
        return multi_task_prompt_args;
    }

};

} // namespace rtp_llm