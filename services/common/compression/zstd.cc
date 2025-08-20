// Copyright 2025 Google LLC
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

#include "services/common/compression/zstd.h"

#include <zstd.h>

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::string> ZstdCompress(absl::string_view uncompressed,
                                         int compression_level) {
  size_t const estimated_compressed_size =
      ZSTD_compressBound(uncompressed.size());

  std::string compressed_string;
  compressed_string.resize(estimated_compressed_size);

  size_t const compressed_size = ZSTD_compress(
      compressed_string.data(), compressed_string.size(), uncompressed.data(),
      uncompressed.size(), compression_level);

  if (ZSTD_isError(compressed_size)) {
    return absl::InternalError(absl::StrCat(
        "cstd compression error: ", ZSTD_getErrorName(compressed_size)));
  }

  compressed_string.resize(compressed_size);
  return compressed_string;
}

absl::StatusOr<std::string> ZstdDecompress(absl::string_view compressed) {
  unsigned long long const uncompressed_size =
      ZSTD_getFrameContentSize(compressed.data(), compressed.size());

  if (uncompressed_size == ZSTD_CONTENTSIZE_ERROR) {
    return absl::InternalError("Compressed data is not a valid Zstd frame.");
  }

  if (uncompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return absl::InternalError("Uncompressed size not stored in zstd frame");
  }

  if (uncompressed_size == 0) {
    return std::string();
  }

  std::string decompressed_string;
  decompressed_string.resize(uncompressed_size);

  size_t const decompressed_actual_size =
      ZSTD_decompress(decompressed_string.data(), decompressed_string.size(),
                      compressed.data(), compressed.size());

  if (ZSTD_isError(decompressed_actual_size)) {
    return absl::InternalError(absl::StrCat(
        "Decompression error: ", ZSTD_getErrorName(decompressed_actual_size)));
  }

  // Check if the decompressed size matches the expected size.
  if (decompressed_actual_size != uncompressed_size) {
    return absl::InternalError(absl::StrCat(
        "Decompression size mismatch. Expected: ", uncompressed_size,
        ", Got: ", decompressed_actual_size));
  }

  return decompressed_string;
}

}  // namespace privacy_sandbox::bidding_auction_servers
