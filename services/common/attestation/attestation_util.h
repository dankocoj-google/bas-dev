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

#ifndef SERVICES_COMMON_ATTESTATION_ATTESTATION_UTIL_H_
#define SERVICES_COMMON_ATTESTATION_ATTESTATION_UTIL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "services/common/util/hash_util.h"
#include "src/logger/request_context_logger.h"
#include "third_party/libtld/tld.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kInvalidURLErrorMessage = "URL %s is invalid.";
constexpr absl::string_view kInvalidTLDErrorMessage =
    "URL %s doesn't have a valid TLD.";

// Check if the URL is valid in format and has a valid TLD. If it's valid,
// return it in the site format. Otherwise, return an error status.
inline absl::StatusOr<std::string> GetValidAdTechSite(
    absl::string_view adtech_url) {
  // Canocalize URL to a standard format and strip the path.
  std::string canonicalized_url = CanonicalizeURL(adtech_url);
  size_t scheme_separator_pos = canonicalized_url.find("://");
  if (scheme_separator_pos == std::string::npos) {
    return absl::InvalidArgumentError(
        absl::StrFormat(kInvalidURLErrorMessage, adtech_url));
  }
  const int start_of_domain = scheme_separator_pos + 3;
  size_t path_pos = canonicalized_url.find('/', start_of_domain);
  if (path_pos == std::string::npos) {
    return absl::InvalidArgumentError(
        absl::StrFormat(kInvalidURLErrorMessage, adtech_url));
  }
  auto stripped_url = std::string(canonicalized_url, 0, path_pos);
  char const* url = stripped_url.c_str();

  // Check TLD's validity with TLD().
  struct tld_info info;
  enum tld_result result;
  result = tld(url, &info);
  if (result == TLD_RESULT_INVALID || (info.f_status != TLD_STATUS_VALID &&
                                       info.f_status != TLD_STATUS_PROPOSED)) {
    return absl::InvalidArgumentError(
        absl::StrFormat(kInvalidTLDErrorMessage, adtech_url));
  }

  // Subdomain is not included in the enrollment list. Check whether subdomain
  // exists in the URL. (Example:
  // https://github.com/privacysandbox/attestation/blob/main/enrollment_report.csv)
  absl::string_view site(url);
  if (size_t domain_pos = site.find('.', start_of_domain);
      domain_pos == info.f_offset) {
    // If there is no subdomain in the url, return the url as is.

    PS_VLOG(5) << "Given URL: " << adtech_url << ", Output Site: " << site;
    return std::string(site);
  } else {
    // Otherwise, subtract subdomain.
    absl::string_view schema(url, start_of_domain);
    absl::string_view domain_tld = site.substr((domain_pos + 1), site.length());

    PS_VLOG(5) << "Given URL: " << adtech_url << ", Output Site: " << schema
               << domain_tld;
    return absl::StrCat(schema, domain_tld);
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_ATTESTATION_ATTESTATION_UTIL_H_
