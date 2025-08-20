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

#ifndef SERVICES_COMMON_ATTESTATION_ADTECH_ENROLLMENT_FETCHER_H_
#define SERVICES_COMMON_ATTESTATION_ADTECH_ENROLLMENT_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "services/common/attestation/adtech_enrollment_cache.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/data_fetch/periodic_url_fetcher.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// This class periodically fetches the adtech API enrollment list from the
// configured endpoint and updates cache with the list of adtechs that are
// enrolled in a certain API.
class AdtechEnrollmentFetcher : public PeriodicUrlFetcher {
 public:
  // url_endpoint: an arbitrary endpoint to fetch code blobs from.
  // fetch_period: a time period in between each code fetching.
  // curl_http_fetcher: a pointer to a libcurl wrapper client that performs code
  // fetching with FetchUrl(). Does not take ownership; pointer must outlive
  // AdtechEnrollmentFetcher.
  // executor: a raw pointer that takes in a reference of the
  // executor owned by the servers where the instance of this class is also
  // declared. The same executor should be used to construct a HttpFetcherAsync
  // object.
  // time_out_ms: a time out limit for HttpsFetcherAsync client to stop
  // executing FetchUrl.
  // enrolled_adtech_cache: cache that gets updated with enrolled adtechs sites.
  // Does not take ownership; pointer must outlive AdtechEnrollmentFetcher.
  // Definition of sites can be found here:
  // https://html.spec.whatwg.org/multipage/browsers.html#obtain-a-site.
  explicit AdtechEnrollmentFetcher(const std::string& url_endpoint,
                                   absl::Duration fetch_period,
                                   HttpFetcherAsync* curl_http_fetcher,
                                   server_common::Executor* executor,
                                   absl::Duration time_out_ms,
                                   AdtechEnrollmentCacheInterface* cache);

  ~AdtechEnrollmentFetcher() { End(); }

  // Not copyable or movable.
  AdtechEnrollmentFetcher(const AdtechEnrollmentFetcher&) = delete;
  AdtechEnrollmentFetcher& operator=(const AdtechEnrollmentFetcher&) = delete;

 protected:
  // Convert the fetched .binpb data to proto objects, filter for sites that are
  // enrolled in Protected Audience API, and insert those sites. Refer to
  // PeriodicUrlFetcher for how this OnFetch() function is used.
  bool OnFetch(const std::vector<std::string>& fetched_data) override;

 private:
  AdtechEnrollmentCacheInterface* cache_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_ATTESTATION_ADTECH_ENROLLMENT_FETCHER_H_
