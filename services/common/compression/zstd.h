/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_CLIENTS_COMPRESSION_ZSTD_H_
#define SERVICES_COMMON_CLIENTS_COMPRESSION_ZSTD_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr int kDefaultZstdCompressionLevel = 3;

// Compresses a string using zstd.
absl::StatusOr<std::string> ZstdCompress(
    absl::string_view decompressed,
    int compression_level = kDefaultZstdCompressionLevel);

// Decompresses a zstd compressed string.
absl::StatusOr<std::string> ZstdDecompress(absl::string_view compressed);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_COMPRESSION_ZSTD_H_
