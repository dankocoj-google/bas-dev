// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SERVICES_BIDDING_SERVICE_INFERENCE_INFERENCE_FLAGS_H_
#define SERVICES_BIDDING_SERVICE_INFERENCE_INFERENCE_FLAGS_H_

#include <optional>
#include <string>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"

ABSL_DECLARE_FLAG(std::optional<std::string>, inference_sidecar_binary_path);
ABSL_DECLARE_FLAG(std::optional<std::string>, inference_model_local_paths);
ABSL_DECLARE_FLAG(std::optional<std::string>, inference_model_bucket_name);
ABSL_DECLARE_FLAG(std::optional<std::string>, inference_model_bucket_paths);
ABSL_DECLARE_FLAG(std::optional<std::string>, inference_model_config_path);
ABSL_DECLARE_FLAG(std::optional<std::int64_t>, inference_model_fetch_period_ms);
ABSL_DECLARE_FLAG(std::optional<std::string>, inference_sidecar_runtime_config);
ABSL_DECLARE_FLAG(std::optional<std::int64_t>, inference_sidecar_rlimit_mb);
ABSL_DECLARE_FLAG(std::optional<std::int64_t>,
                  inference_model_registration_timeout_ms);
ABSL_DECLARE_FLAG(std::optional<std::int64_t>,
                  inference_model_execution_timeout_ms);
ABSL_DECLARE_FLAG(std::optional<std::int64_t>,
                  inference_model_paths_request_timeout_ms);
ABSL_DECLARE_FLAG(bool, inference_enable_cancellation_at_bidding);
ABSL_DECLARE_FLAG(bool, inference_enable_proto_parsing);

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char INFERENCE_SIDECAR_BINARY_PATH[] =
    "INFERENCE_SIDECAR_BINARY_PATH";
inline constexpr char INFERENCE_MODEL_BUCKET_NAME[] =
    "INFERENCE_MODEL_BUCKET_NAME";
inline constexpr char INFERENCE_MODEL_BUCKET_PATHS[] =
    "INFERENCE_MODEL_BUCKET_PATHS";
inline constexpr char INFERENCE_MODEL_CONFIG_PATH[] =
    "INFERENCE_MODEL_CONFIG_PATH";
inline constexpr char INFERENCE_MODEL_FETCH_PERIOD_MS[] =
    "INFERENCE_MODEL_FETCH_PERIOD_MS";
inline constexpr char INFERENCE_SIDECAR_RUNTIME_CONFIG[] =
    "INFERENCE_SIDECAR_RUNTIME_CONFIG";
inline constexpr char INFERENCE_SIDECAR_RLIMIT_MB[] =
    "INFERENCE_SIDECAR_RLIMIT_MB";
inline constexpr char INFERENCE_ENABLE_PROTO_PARSING[] =
    "INFERENCE_ENABLE_PROTO_PARSING";
inline constexpr char INFERENCE_ENABLE_CANCELLATION_AT_BIDDING[] =
    "INFERENCE_ENABLE_CANCELLATION_AT_BIDDING";
inline constexpr char INFERENCE_MODEL_REGISTRATION_TIMEOUT_MS[] =
    "INFERENCE_MODEL_REGISTRATION_TIMEOUT_MS";
inline constexpr char INFERENCE_MODEL_EXECUTION_TIMEOUT_MS[] =
    "INFERENCE_MODEL_EXECUTION_TIMEOUT_MS";
inline constexpr char INFERENCE_MODEL_PATHS_REQUEST_TIMEOUT_MS[] =
    "INFERENCE_MODEL_PATHS_REQUEST_TIMEOUT_MS";

inline constexpr absl::string_view kInferenceFlags[] = {
    INFERENCE_SIDECAR_BINARY_PATH,
    INFERENCE_MODEL_BUCKET_NAME,
    INFERENCE_MODEL_BUCKET_PATHS,
    INFERENCE_MODEL_CONFIG_PATH,
    INFERENCE_MODEL_FETCH_PERIOD_MS,
    INFERENCE_SIDECAR_RUNTIME_CONFIG,
    INFERENCE_SIDECAR_RLIMIT_MB,
    INFERENCE_ENABLE_PROTO_PARSING,
    INFERENCE_ENABLE_CANCELLATION_AT_BIDDING};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_INFERENCE_INFERENCE_FLAGS_H_
