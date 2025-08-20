// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "grpc_sidecar.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "sandbox/sandbox_worker.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "src/util/status_macro/status_util.h"
#include "utils/cancellation_util.h"
#include "utils/cpu.h"
#include "utils/log.h"
#include "utils/tcmalloc.h"

ABSL_FLAG(bool, inference_enable_cancellation_at_sidecar, false,
          "When enabled, cancellation will be propagated to modules of "
          "the inference sidecar.");

namespace privacy_sandbox::bidding_auction_servers::inference {

// Uses 600000 ms for 10 mins.
constexpr int kGrpcServerHandshakeTimeoutMs = 600000;
constexpr int kGrpcKeepAliveTimeoutMs = 600000;

InferenceServiceImpl::InferenceServiceImpl(
    const InferenceSidecarRuntimeConfig& config)
    : inference_module_(ModuleInterface::Create(config)) {}

grpc::ServerUnaryReactor* InferenceServiceImpl::RegisterModel(
    grpc::CallbackServerContext* context, const RegisterModelRequest* request,
    RegisterModelResponse* response) {
  grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
  RETURN_GRPC_IF_CANCELLED(*context, CancelLocation::kRegModelEnter, reactor);
  GRPCContextAdapter cancellation_context{
      *context, absl::GetFlag(FLAGS_inference_enable_cancellation_at_sidecar)};
  absl::StatusOr<RegisterModelResponse> register_model_response =
      inference_module_->RegisterModel(*request, cancellation_context);
  if (!register_model_response.ok()) {
    reactor->Finish(
        server_common::FromAbslStatus(register_model_response.status()));
    return reactor;
  }

  // Save the model path since it was registered successfully.
  absl::WriterMutexLock write_model_paths_lock(&model_paths_mutex_);
  model_paths_.insert(request->model_spec().model_path());

  *response = *register_model_response;
  reactor->Finish(grpc::Status::OK);
  return reactor;
}

grpc::ServerUnaryReactor* InferenceServiceImpl::DeleteModel(
    grpc::CallbackServerContext* context, const DeleteModelRequest* request,
    DeleteModelResponse* response) {
  grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
  RETURN_GRPC_IF_CANCELLED(*context, CancelLocation::kDelModelEnter, reactor);
  GRPCContextAdapter cancellation_context{
      *context, absl::GetFlag(FLAGS_inference_enable_cancellation_at_sidecar)};
  absl::StatusOr<DeleteModelResponse> delete_model_response =
      inference_module_->DeleteModel(*request, cancellation_context);
  if (!delete_model_response.ok()) {
    reactor->Finish(
        server_common::FromAbslStatus(delete_model_response.status()));
    return reactor;
  }

  // Remove the model path since it was deleted successfully.
  absl::WriterMutexLock write_model_paths_lock(&model_paths_mutex_);
  model_paths_.erase(request->model_spec().model_path());

  *response = *delete_model_response;
  reactor->Finish(grpc::Status::OK);
  return reactor;
}

grpc::ServerUnaryReactor* InferenceServiceImpl::Predict(
    grpc::CallbackServerContext* context, const PredictRequest* request,
    PredictResponse* response) {
  grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
  RETURN_GRPC_IF_CANCELLED(*context, CancelLocation::kPredictEnter, reactor);
  RequestContext log_context(
      [response] { return response->mutable_debug_info(); },
      request->is_consented());
  GRPCContextAdapter cancellation_context{
      *context, absl::GetFlag(FLAGS_inference_enable_cancellation_at_sidecar)};
  absl::StatusOr<PredictResponse> predict_response =
      inference_module_->Predict(*request, log_context, cancellation_context);

  if (!predict_response.ok()) {
    ABSL_LOG(ERROR) << predict_response.status();
    reactor->Finish(server_common::FromAbslStatus(predict_response.status()));
    return reactor;
  }

  std::swap(*response->mutable_metrics_list(),
            *predict_response->mutable_metrics_list());
  if (predict_response->output_data_case() == PredictResponse::kProtoOutput) {
    response->mutable_proto_output()->Swap(
        predict_response->mutable_proto_output());
  } else {
    response->set_output(std::move(*predict_response->mutable_output()));
  }

  reactor->Finish(grpc::Status::OK);
  return reactor;
}

// TODO: (b/348968123) - Relook at API implementation.
grpc::ServerUnaryReactor* InferenceServiceImpl::GetModelPaths(
    grpc::CallbackServerContext* context, const GetModelPathsRequest* request,
    GetModelPathsResponse* response) {
  grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
  RETURN_GRPC_IF_CANCELLED(*context, CancelLocation::kGetPathsEnter, reactor);
  absl::ReaderMutexLock read_model_paths_lock(&model_paths_mutex_);

  for (const std::string& model_path : model_paths_) {
    ModelSpec* model_spec = response->add_model_specs();
    model_spec->set_model_path(model_path);
  }

  reactor->Finish(grpc::Status::OK);
  return reactor;
}

absl::Status SetCpuAffinity(const InferenceSidecarRuntimeConfig& config) {
  if (config.cpuset().empty()) {
    return absl::OkStatus();
  }
  std::vector<int> cpuset(config.cpuset().begin(), config.cpuset().end());
  return SetCpuAffinity(cpuset);
}

absl::Status EnforceModelResetProbability(
    InferenceSidecarRuntimeConfig& config) {
  if (config.model_reset_probability() != 0.0) {
    return absl::InvalidArgumentError(
        "model_reset_probability should not be set");
  }
  config.set_model_reset_probability(kMinResetProbability);
  return absl::OkStatus();
}

absl::Status SetTcMallocConfig(const InferenceSidecarRuntimeConfig& config) {
  SetTcMallocParams(config.tcmalloc_release_bytes_per_sec(),
                    config.tcmalloc_max_total_thread_cache_bytes(),
                    config.tcmalloc_max_per_cpu_cache_bytes());
  return absl::OkStatus();
}

absl::Status SetInferenceCancellationFlag(
    const InferenceSidecarRuntimeConfig& config) {
  absl::SetFlag(&FLAGS_inference_enable_cancellation_at_sidecar,
                config.inference_enable_cancellation_at_sidecar());
  return absl::OkStatus();
}

absl::Status Run(const InferenceSidecarRuntimeConfig& config) {
  SandboxWorker worker;
  grpc::ServerBuilder builder;
  // Sets timeouts to prevent connection closure during slow model loading.
  // Along with the keepalive timeout, a handshake timeout is necessary
  // to handle cases where cloud model fetching on the bidding server delays the
  // handshake process, ensuring the server doesn't time out while waiting for
  // the handshake.
  builder.AddChannelArgument(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS,
                             kGrpcServerHandshakeTimeoutMs);
  builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS,
                             kGrpcKeepAliveTimeoutMs);
  // Sets the max receive size to unlimited; The default max size is only 4MB.
  builder.SetMaxReceiveMessageSize(-1);

  if (!config.module_name().empty() &&
      config.module_name() != ModuleInterface::GetModuleVersion()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Expected inference module: ", config.module_name(),
                     ", but got : ", ModuleInterface::GetModuleVersion()));
  }

  InferenceServiceImpl service(config);
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    ABSL_LOG(ERROR) << "Cannot start the gRPC sidecar.";
    return absl::UnavailableError("Cannot start the gRPC sidecar");
  }

  // Starts gRPC over the sandbox IPC file descriptor.
  grpc::AddInsecureChannelFromFd(server.get(), worker.FileDescriptor());

  // Server->Wait() blocks and uses the framework's internal thread pool
  // to dispatch callbacks to the reactor methods.
  server->Wait();
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
