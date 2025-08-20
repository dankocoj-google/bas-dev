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

#include "services/common/attestation/adtech_enrollment_cache.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "api/attestation.pb.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsGatedAPIProto;
using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsProto;

AdtechEnrollmentCache::AdtechEnrollmentCache()
    : AdtechEnrollmentCacheInterface(),
      cached_attestation_data_(
          std::make_unique<absl::flat_hash_set<std::string>>()) {}

void AdtechEnrollmentCache::Refresh(
    std::unique_ptr<const PrivacySandboxAttestationsProto> message)
    ABSL_LOCKS_EXCLUDED(mutex_) {
  auto fresh_attestation_data =
      std::make_unique<absl::flat_hash_set<std::string>>();

  if (!message->sites_attested_for_all_apis().empty()) {
    for (const auto& site : message->sites_attested_for_all_apis()) {
      fresh_attestation_data->insert(site);
    }
  }
  if (!message->site_attestations().empty()) {
    for (const auto& [site, proto] : message->site_attestations()) {
      if (absl::c_linear_search(
              proto.attested_apis(),
              PrivacySandboxAttestationsGatedAPIProto::PROTECTED_AUDIENCE)) {
        fresh_attestation_data->insert(site);
      }
    }
  }

  {
    absl::MutexLock lock(&mutex_);
    cached_attestation_data_.swap(fresh_attestation_data);
  }
}

bool AdtechEnrollmentCache::Query(absl::string_view fdo_destination)
    ABSL_LOCKS_EXCLUDED(mutex_) {
  bool enrolled = false;
  {
    absl::MutexLock lock(&mutex_);
    enrolled = cached_attestation_data_->find(fdo_destination) !=
               cached_attestation_data_->end();
  }

  if (enrolled) {
    PS_VLOG(5) << "fDO destination " << fdo_destination << " is enrolled.";
  } else {
    PS_VLOG(5) << "fDO destination " << fdo_destination << " is NOT enrolled.";
  }
  return enrolled;
}

}  // namespace privacy_sandbox::bidding_auction_servers
