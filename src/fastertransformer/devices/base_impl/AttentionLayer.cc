#include "src/fastertransformer/devices/DeviceBase.h"
#include "src/fastertransformer/devices/utils/DebugUtils.h"

#include <numeric>

using namespace std;

namespace fastertransformer {

AttentionLayerOutput DeviceBase::attentionLayer(const AttentionLayerParams& params) {
    const auto &input = params.input;
    const auto &input_lengths = params.common.input_lengths;
    const auto &sequence_lengths = params.common.sequence_lengths;

    const auto &qkv_weight = params.weights.qkv_weight;
    const auto &output_weight = params.weights.output_weight;

    // typically local_head_num * size_per_head
    const auto qkv_hidden_size = output_weight->kernel->shape()[0];
    // typically local_head_num * size_per_head + 2 * local_head_num_kv * size_per_head
    const auto qkv_merged_size = qkv_weight->kernel->shape()[1];

    if (qkv_weight->bias) {
        FT_LOG_WARNING("AttentionLayer bias is not supported yet, ignored");
    }
    const auto qkv = gemm({input, *(qkv_weight->kernel)});
    printBufferData(*qkv, "qkv");

    const auto generate_batch_size = sequence_lengths.shape()[0];
    const auto context_batch_size = input_lengths.shape()[0] - generate_batch_size;

    const auto context_token_num = std::accumulate(
        input_lengths.data<int32_t>(), input_lengths.data<int32_t>() + context_batch_size, 0);
    const auto h_token_num = context_token_num + generate_batch_size;

    const auto qkv_output = allocateBuffer({input.type(), {h_token_num, qkv_hidden_size}});

    auto generate_qkv = qkv->view(0, generate_batch_size);
    auto generate_output = qkv_output->view(0, generate_batch_size);
    auto context_qkv = qkv->view(generate_batch_size, context_token_num);
    auto context_output = qkv_output->view(generate_batch_size, context_token_num);

    if (generate_batch_size) {
        decoderSelfAttention({generate_qkv, generate_output, params.common, params.weights, params.configs});
    }
    if (context_batch_size) {
        contextAttention({context_qkv, context_output, params.common, params.weights, params.configs});
    }
    printBufferData(*qkv_output, "qkv_output");
    printBufferData(*(output_weight->kernel), "output_weight");

    auto output = gemm({*qkv_output, *(output_weight->kernel)});

    return {move(output)};
}

}; // namespace fastertransformer
