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

#include "services/common/attestation/adtech_enrollment_fetcher.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "api/attestation.pb.h"
#include "services/common/attestation/adtech_enrollment_cache.h"
#include "services/seller_frontend_service/k_anon/k_anon_utils.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsGatedAPIProto;
using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsProto;
using PrivacySandboxAttestedAPIsProto =
    PrivacySandboxAttestationsProto::PrivacySandboxAttestedAPIsProto;

AdtechEnrollmentFetcher::AdtechEnrollmentFetcher(
    const std::string& url_endpoint, absl::Duration fetch_period,
    HttpFetcherAsync* curl_http_fetcher, server_common::Executor* executor,
    absl::Duration time_out_ms, AdtechEnrollmentCacheInterface* cache)
    : PeriodicUrlFetcher({url_endpoint}, fetch_period, curl_http_fetcher,
                         executor, time_out_ms),
      cache_(cache) {}

bool AdtechEnrollmentFetcher::OnFetch(
    const std::vector<std::string>& fetched_data) {
  if (fetched_data.size() != 1) {
    PS_LOG(ERROR) << "Expected one fetched data.";
    return false;
  }
  // fetched_data will be in .binpb format; convert to proto objects.
  // Filter sites attested for all apis and sites attested for
  // PROTECTED_AUDIENCE, and insert those sites.
  PrivacySandboxAttestationsProto message;
  if (!message.ParseFromString(fetched_data[0])) {
    PS_LOG(ERROR) << "Could not parse fetched binary proto string.";
    return false;
  }
  cache_->Refresh(std::make_unique<const PrivacySandboxAttestationsProto>(
      std::move(message)));
  return true;
}

}  // namespace privacy_sandbox::bidding_auction_servers
