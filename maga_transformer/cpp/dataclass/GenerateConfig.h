#pragma once
#include <assert.h>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace rtp_llm {

// TODO: complete params.
// TODO: implement hash function to bind with sampler.

// NOTE: The params in generate config should be splitted into two parts:
//       1. The params that can be different for a single sampler.
//       e.g. top_k, top_p, temperature, repetition_penalty, etc.
//       2. The params that must be the same for a single sampler.
//       e.g. beam_size, max_seq_len, etc.
//       For the second part, different samplers should be created for different params.
//       So they can not be batched together for now.

class GenerateConfig {
public:
    int max_seq_len        = 8192;
    int max_new_tokens     = 8192;
    int min_new_tokens     = 0;
    int num_validate_token = 0;  // for speculative decoding validation.

    int                  num_beams            = 1;
    int                  num_return_sequences = 1;
    std::optional<int>   top_k;
    std::optional<float> top_p;
    std::optional<float> temperature;
    std::optional<float> repetition_penalty;
    std::optional<int>   min_length;
    std::optional<int>   random_seed;
    std::optional<float> top_p_decay;
    std::optional<float> top_p_min;
    std::optional<int>   top_p_reset_ids;
    std::optional<int>   task_id;
    std::optional<int>   adapter_name;

    std::vector<size_t> select_tokens_id;
    int                 calculate_loss;
    bool                return_logits;
    bool                return_incremental;
    bool                return_hidden_states;
    bool                is_streaming;
    int                 timeout_ms;

    std::string debugString() const {
        std::stringstream debug_string;
        debug_string << "GenerateInput {"
                     << "max_seq_len: " << max_seq_len << ", max_new_tokens:" << max_new_tokens
                     << ", min_new_tokens:" << min_new_tokens << ", num_beams:" << num_beams
                     << ", num_return_sequences:" << num_return_sequences << ", calculate_loss:" << calculate_loss
                     << ", return_logits:" << return_logits << ", return_incremental: " << return_incremental
                     << ", return_hidden_states:" << return_hidden_states << "}";
        return debug_string.str();
    }
};

}  // namespace rtp_llm
