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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_

#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "services/common/chaffing/moving_median_manager.h"
#include "services/common/clients/async_grpc/request_config.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/status_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

// Chaffing V1 constants.
inline constexpr int kMinChaffRequestSizeBytes = 9'000;
inline constexpr int kMaxChaffRequestSizeBytes = 95'000;

// Chaffing V2 constants.
inline constexpr size_t kChaffingV2MovingMedianWindowSize = 10'000;
inline constexpr int kChaffingV2UnfilledWindowRequestSizeBytes = 100'000;
inline constexpr float kChaffingV2SamplingProbablility = 0.1;
// Constants used to create normal distribution, which is used for calculating
// the number of chaff requests for Chaffing V2.gs
inline constexpr float kChaffingV2GaussianMeanDivisor = 6;
inline constexpr float kChaffingV2GaussianStandardDev = 0.3;
inline constexpr absl::Duration kChaffingV2UnfilledWindowResponseDurationMs =
    absl::Milliseconds(500);
inline constexpr int kChaffingV2UnfilledWindowResponseSizeBytes = 100'000;

struct InvokedBuyers {
  std::vector<absl::string_view> chaff_buyer_names;
  std::vector<absl::string_view> non_chaff_buyer_names;
};

struct ChaffMedianTrackers {
  std::unique_ptr<MovingMedian> request_duration;
  std::unique_ptr<MovingMedian> response_size;
};

RequestConfig GetChaffingV1GetBidsRequestConfig(
    bool is_chaff_request, RandomNumberGenerator& rng,
    CompressionType sfe_bfe_compression_algo = CompressionType::kGzip);

RequestConfig GetChaffingV2GetBidsRequestConfig(
    absl::string_view buyer_name, bool is_chaff_request,
    const MovingMedianManager& moving_median_manager, RequestContext context,
    CompressionType sfe_bfe_compression_algo = CompressionType::kGzip);

// Returns whether chaffing should be skipped based on the provided RNG.
bool ShouldSkipChaffing(size_t num_buyers, RandomNumberGenerator& rng);

absl::StatusOr<std::pair<absl::Duration, int>> GetBuyerChaffingV2Values(
    const ChaffMedianTrackers& chaff_median_trackers);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_
