#include "maga_transformer/cpp/normal_engine/NormalExecutor.h"
#include "maga_transformer/cpp/common/status_util.h"

#include "maga_transformer/cpp/deprecated/ParallelModelWrapper.h"

#include "maga_transformer/cpp/models/GptModel.h"
#include "maga_transformer/cpp/models/Sampler.h"
#include "src/fastertransformer/devices/DeviceFactory.h"
#include "maga_transformer/cpp/metrics/RtpLLMMetrics.h"

using namespace std;

namespace rtp_llm {

NormalExecutor::NormalExecutor(
        const MagaInitParams&                                                   params,
        const std::vector<std::unordered_map<std::string, ft::ConstBufferPtr>>& layer_weights,
        const std::unordered_map<std::string, ft::ConstBufferPtr>&              weights,
        const kmonitor::MetricsReporterPtr                                      metrics_reporter):
    metrics_reporter_(metrics_reporter)
{
    // need init model and sampler
    unique_ptr<GptModelInitParams> model_params;
    // model_.reset(new GptModel(*model_params));
    SamplerInitParams sampler_params;
    device_               = ft::DeviceFactory::getDevice(ft::DeviceType::Cuda);
    sampler_params.device = device_;
    sampler_params.max_batch_size = params.gpt_init_parameter->max_context_batch_size_
                                  + params.gpt_init_parameter->max_generate_batch_size_;
    printf("sampler max_batch_size: %d\n", sampler_params.max_batch_size);
    sampler_params.eos_id = params.gpt_init_parameter->special_tokens_->eos_token_id_;
    sampler_.reset(new Sampler(sampler_params));

    model_wrapper_.reset(
            new ParallelModelWrapper(*params.gpt_init_parameter, weights, layer_weights));

    batch_stream_processor_.reset(new NormalBatchStreamProcessor(*params.gpt_init_parameter, !model_wrapper_->useFMHA()));
}

absl::Status NormalExecutor::addLoRA(const int64_t                                                   lora_id,
                             const std::vector<std::unordered_map<std::string, ft::ConstBufferPtr>>& lora_a_weights,
                             const std::vector<std::unordered_map<std::string, ft::ConstBufferPtr>>& lora_b_weights) {
    model_wrapper_->addLoRA(lora_id, lora_a_weights, lora_b_weights);
    return absl::OkStatus();
}

absl::Status NormalExecutor::removeLoRA(const int64_t lora_id) {
    model_wrapper_->removeLoRA(lora_id);
    return absl::OkStatus();
}

ModelRequest NormalExecutor::generateOldModelRequest(GptModelInputs& model_input) {
    ModelRequest model_request;
    model_request.generate_batch_size = model_input.sequence_lengths->shape()[0];
    model_request.context_batch_size  = model_input.input_lengths->shape()[0] - model_request.generate_batch_size;
    model_request.combo_tokens        = model_input.combo_tokens;
    model_request.input_lengths       = model_input.input_lengths;
    model_request.sequence_lengths    = model_input.sequence_lengths;
    model_request.kv_cache_blocks     = model_input.kv_cache_blocks;
    model_request.attention_mask      = model_input.attention_mask;
    return model_request;
}

absl::Status NormalExecutor::process(const std::list<GenerateStreamPtr>& streams) {
    StreamGroups stream_groups(streams);
    reportMetrics(stream_groups);
    auto         model_input_status = batch_stream_processor_->gatherModelInput(stream_groups);
    RETURN_IF_STATUS_OR_ERROR(model_input_status);
    auto& model_input = model_input_status.value();
    tpSyncModelInputs(model_input, device_);
    FT_LOG_DEBUG("model_input: %s", model_input.debugString().c_str());
    auto         merged_output        = std::make_unique<MergedOutput>();
    ModelRequest model_request        = std::move(generateOldModelRequest(model_input));
    auto         model_output         = std::move(model_wrapper_->forward(model_request));
    if (device_->getDeviceProperties().tp_rank > 0) {
        return absl::OkStatus();
    }
    auto sampler_input_status = batch_stream_processor_->gatherSamplerInput(
            stream_groups, model_input, *model_output);
    RETURN_IF_STATUS_OR_ERROR(sampler_input_status);
    auto& sampler_input           = sampler_input_status.value();
    merged_output->sampler_output = std::move(sampler_->forward(sampler_input));
    return batch_stream_processor_->dispatch(stream_groups, merged_output);
}

void NormalExecutor::reportMetrics(const StreamGroups& stream_groups) {
    if (metrics_reporter_) {
        RtpLLMExecutorMetricsCollector collector;
        collector.context_batch_size = stream_groups.contextStreams().size();
        collector.generate_batch_size = stream_groups.totalModelBatchSize() - stream_groups.contextStreams().size();
        collector.execute_token_size = stream_groups.modelExecuteTokenSize();
        collector.max_seq_len = stream_groups.maxSeqLen();
        metrics_reporter_->report<RtpLLMExecutorMetrics, RtpLLMExecutorMetricsCollector>(nullptr, &collector);
    }
}

}  // namespace rtp_llm
