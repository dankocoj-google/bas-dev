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

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char INFERENCE_SIDECAR_BINARY_PATH[] =
    "INFERENCE_SIDECAR_BINARY_PATH";
inline constexpr char INFERENCE_MODEL_LOCAL_PATHS[] =
    "INFERENCE_MODEL_LOCAL_PATHS";
inline constexpr char INFERENCE_MODEL_BUCKET_NAME[] =
    "INFERENCE_MODEL_BUCKET_NAME";
inline constexpr char INFERENCE_MODEL_BUCKET_PATHS[] =
    "INFERENCE_MODEL_BUCKET_PATHS";
inline constexpr absl::string_view kInferenceFlags[] = {
    INFERENCE_SIDECAR_BINARY_PATH, INFERENCE_MODEL_LOCAL_PATHS,
    INFERENCE_MODEL_BUCKET_NAME, INFERENCE_MODEL_BUCKET_PATHS};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_INFERENCE_INFERENCE_FLAGS_H_
