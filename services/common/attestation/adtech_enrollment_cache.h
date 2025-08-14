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

#ifndef SERVICES_COMMON_ATTESTATION_ADTECH_ENROLLMENT_CACHE_H_
#define SERVICES_COMMON_ATTESTATION_ADTECH_ENROLLMENT_CACHE_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "api/attestation.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsProto;

class AdtechEnrollmentCacheInterface {
 public:
  AdtechEnrollmentCacheInterface() = default;
  virtual ~AdtechEnrollmentCacheInterface() = default;

  // Creates a new set of enrolled adtech sites from
  // PrivacySandboxAttestationsProto to cache as enrollment list.
  virtual void Refresh(
      std::unique_ptr<const PrivacySandboxAttestationsProto> message) = 0;

  // Checks if the given fDO destination exists in a cached enrollment list.
  virtual bool Query(absl::string_view fdo_destination) = 0;
};

// This cache class stores adtech sites who are enrolled in Protected Audience
// API, using double-buffering to ensure consistent read access. Used for
// attesting fDO destination.
class AdtechEnrollmentCache : public AdtechEnrollmentCacheInterface {
 public:
  AdtechEnrollmentCache();

  void Refresh(std::unique_ptr<const PrivacySandboxAttestationsProto> message)
      ABSL_LOCKS_EXCLUDED(mutex_) override;

  bool Query(absl::string_view fdo_destination)
      ABSL_LOCKS_EXCLUDED(mutex_) override;

 private:
  absl::Mutex mutex_;
  std::unique_ptr<absl::flat_hash_set<std::string>> cached_attestation_data_
      ABSL_GUARDED_BY(mutex_);
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_ATTESTATION_ADTECH_ENROLLMENT_CACHE_H_
