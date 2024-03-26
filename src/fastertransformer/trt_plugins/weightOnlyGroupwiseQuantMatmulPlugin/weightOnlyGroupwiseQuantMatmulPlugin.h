/*
 * SPDX-FileCopyrightText: Copyright (c) 1993-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "src/fastertransformer/utils/quantization.h"
#include "src/fastertransformer/kernels/preQuantScaleKernel.h"
#include "src/fastertransformer/trt_plugins/weightOnlyQuantMatmulPlugin/weightOnlyQuantMatmulPlugin.h"

#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace tensorrt_llm::plugins
{

using WeightOnlyGemmRunner = tensorrt_llm::kernels::cutlass_kernels::CutlassFpAIntBGemmRunnerInterface;
using WeightOnlyGemmRunnerPtr = std::shared_ptr<WeightOnlyGemmRunner>;

class WeightOnlyGroupwiseQuantMatmulPlugin
{
public:
    WeightOnlyGroupwiseQuantMatmulPlugin() = delete;

    WeightOnlyGroupwiseQuantMatmulPlugin(nvinfer1::DataType type, bool has_zeros, int group_size);

    ~WeightOnlyGroupwiseQuantMatmulPlugin() = default;

    size_t getWorkspaceSize(const int m, const int n, const int k) noexcept;
    int    enqueue(const void*  inputs,
                   const void*  weights,
                   const void*  scales,
                   const void*  zeros,
                   const void*  biases,
                   void*        outputs,
                   void*        workspace,
                   const int    m,
                   const int    n,
                   const int    k,
                   cudaStream_t stream) noexcept;

private:
    // group_size: 64, 128
    void init(nvinfer1::DataType type, bool has_zeros,int group_size);

    void configGemm();

private:

    WeightOnlyGemmRunnerPtr m_weightOnlyGroupwiseGemmRunner;
    size_t m_workspaceMaxSize;
    nvinfer1::DataType mType;
    bool mCudaKernelEnabled;
    bool mHasZeros;

    int mGroupSize;

};

} // namespace tensorrt_llm::plugins
