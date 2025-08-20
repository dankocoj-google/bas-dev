//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_GRPC_SIDECAR_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_GRPC_SIDECAR_H_

#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>
#include <grpcpp/support/server_callback.h>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"

ABSL_DECLARE_FLAG(bool, inference_enable_cancellation_at_sidecar);

namespace privacy_sandbox::bidding_auction_servers::inference {

// ML models are reset at the probability of 0.1%.
constexpr double kMinResetProbability = 0.001;

absl::Status SetCpuAffinity(const InferenceSidecarRuntimeConfig& config);

// Makes sure model_reset_probability must be set to kMinResetProbability.
absl::Status EnforceModelResetProbability(
    InferenceSidecarRuntimeConfig& config);

// Sets the TCMalloc config.
absl::Status SetTcMallocConfig(const InferenceSidecarRuntimeConfig& config);

// Sets the inference cancellation flag.
absl::Status SetInferenceCancellationFlag(
    const InferenceSidecarRuntimeConfig& config);

// Runs a simple gRPC server. It is thread safe.
absl::Status Run(const InferenceSidecarRuntimeConfig& config);

// Inference service implementation header.
class InferenceServiceImpl final : public InferenceService::CallbackService {
 public:
  explicit InferenceServiceImpl(const InferenceSidecarRuntimeConfig& config);

  grpc::ServerUnaryReactor* RegisterModel(
      grpc::CallbackServerContext* context, const RegisterModelRequest* request,
      RegisterModelResponse* response) override;

  grpc::ServerUnaryReactor* DeleteModel(grpc::CallbackServerContext* context,
                                        const DeleteModelRequest* request,
                                        DeleteModelResponse* response) override;

  grpc::ServerUnaryReactor* Predict(grpc::CallbackServerContext* context,
                                    const PredictRequest* request,
                                    PredictResponse* response) override;

  grpc::ServerUnaryReactor* GetModelPaths(
      grpc::CallbackServerContext* context, const GetModelPathsRequest* request,
      GetModelPathsResponse* response) override;

 private:
  std::unique_ptr<ModuleInterface> inference_module_;
  mutable absl::Mutex model_paths_mutex_;
  absl::flat_hash_set<std::string> model_paths_
      ABSL_GUARDED_BY(model_paths_mutex_);
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_GRPC_SIDECAR_H_
