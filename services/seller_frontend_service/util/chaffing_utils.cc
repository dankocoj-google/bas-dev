// Copyright 2025 Google LLC
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

#include "services/seller_frontend_service/util/chaffing_utils.h"

#include <utility>

#include "absl/status/statusor.h"

namespace privacy_sandbox::bidding_auction_servers {

RequestConfig GetChaffingV1GetBidsRequestConfig(
    bool is_chaff_request, RandomNumberGenerator& rng,
    CompressionType sfe_bfe_compression_algo) {
  RequestConfig request_config = {.minimum_request_size = 0,
                                  .compression_type = sfe_bfe_compression_algo,
                                  .is_chaff_request = is_chaff_request};

  if (is_chaff_request) {
    request_config.minimum_request_size =
        rng.GetUniformInt(kMinChaffRequestSizeBytes, kMaxChaffRequestSizeBytes);
  }

  return request_config;
}

RequestConfig GetChaffingV2GetBidsRequestConfig(
    absl::string_view buyer_name, bool is_chaff_request,
    const MovingMedianManager& moving_median_manager, RequestContext context,
    CompressionType sfe_bfe_compression_algo) {
  RequestConfig request_config = {.minimum_request_size = 0,
                                  .compression_type = sfe_bfe_compression_algo,
                                  .is_chaff_request = is_chaff_request};

  if (is_chaff_request) {
    absl::StatusOr<bool> is_window_filled =
        moving_median_manager.IsWindowFilled(buyer_name);
    absl::StatusOr<int> median = moving_median_manager.GetMedian(buyer_name);
    if (!is_window_filled.ok() || !median.ok()) {
      PS_LOG(ERROR, context.log)
          << FirstNotOkStatusOr(is_window_filled, median);
      request_config.minimum_request_size =
          kChaffingV2UnfilledWindowRequestSizeBytes;
    } else {
      request_config.minimum_request_size =
          *is_window_filled ? *median
                            : kChaffingV2UnfilledWindowRequestSizeBytes;
    }
  } else {
    // Non-chaff request - pad the request if the per buyer window is *not*
    // filled.
    absl::StatusOr<bool> is_window_filled =
        moving_median_manager.IsWindowFilled(buyer_name);
    if (!is_window_filled.ok()) {
      PS_LOG(ERROR, context.log) << is_window_filled.status();
      request_config.minimum_request_size =
          kChaffingV2UnfilledWindowRequestSizeBytes;
    } else {
      request_config.minimum_request_size =
          *is_window_filled ? 0 : kChaffingV2UnfilledWindowRequestSizeBytes;
    }
  }

  return request_config;
}

bool ShouldSkipChaffing(size_t num_buyers, RandomNumberGenerator& rng) {
  if (num_buyers <= 5) {
    // Skip chaffing for 99% of requests.
    return (rng.GetUniformDouble(0, 1) < .99);
  }

  // Skip chaffing for 95% of requests.
  return (rng.GetUniformDouble(0, 1) < .95);
}

absl::StatusOr<std::pair<absl::Duration, int>> GetBuyerChaffingV2Values(
    const ChaffMedianTrackers& chaff_median_trackers) {
  bool request_duration_window_filled =
      chaff_median_trackers.request_duration->IsWindowFilled();
  bool response_size_window_filled =
      chaff_median_trackers.response_size->IsWindowFilled();
  if (!request_duration_window_filled || !response_size_window_filled) {
    return std::pair<absl::Duration, int>{
        kChaffingV2UnfilledWindowResponseDurationMs,
        kChaffingV2UnfilledWindowResponseSizeBytes};
  }

  absl::StatusOr<int> request_duration =
      chaff_median_trackers.request_duration->GetMedian();
  absl::StatusOr<int> response_size =
      chaff_median_trackers.response_size->GetMedian();
  if (!request_duration.ok() || !response_size.ok()) {
    return FirstNotOkStatusOr(request_duration, response_size);
  }

  return std::pair<absl::Duration, int>{absl::Milliseconds(*request_duration),
                                        *response_size};
}

}  // namespace privacy_sandbox::bidding_auction_servers
