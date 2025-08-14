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

#include "services/common/attestation/attestation_util.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "include/gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kValidSiteWithoutSubdomain =
    "https://example.co.uk";
constexpr absl::string_view kValidSiteWithSubdomain =
    "https://www.example.co.uk";
constexpr absl::string_view kValidURLWithQuery =
    "https://www.example.co.uk?query=co.uk";
constexpr absl::string_view kValidURLWithPath =
    "https://www.example.co.uk/path.co.uk";
constexpr absl::string_view kInvalidTLD = "https://www.example.invalid";
constexpr absl::string_view kInvalidURL = "www.example.com";

TEST(GetValidAdTechSite, SuccessfullyParsesSiteFromValidURL) {
  auto output = GetValidAdTechSite(kValidSiteWithoutSubdomain);
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(output.value(), kValidSiteWithoutSubdomain);

  auto output2 = GetValidAdTechSite(kValidSiteWithSubdomain);
  ASSERT_TRUE(output2.ok()) << output2.status();
  EXPECT_EQ(output2.value(), kValidSiteWithoutSubdomain);
}

TEST(GetValidAdTechSite, SuccessfullyParsesSiteFromURL) {
  auto output = GetValidAdTechSite(kValidURLWithQuery);
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(output.value(), kValidSiteWithoutSubdomain);

  auto output2 = GetValidAdTechSite(kValidURLWithPath);
  ASSERT_TRUE(output2.ok()) << output2.status();
  EXPECT_EQ(output2.value(), kValidSiteWithoutSubdomain);
}

TEST(GetValidAdTechSite, ReturnsErrorForInvalidURL) {
  auto output = GetValidAdTechSite(kInvalidTLD);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(output.status().message(),
            absl::StrFormat(kInvalidTLDErrorMessage, kInvalidTLD));

  auto output2 = GetValidAdTechSite(kInvalidURL);
  ASSERT_FALSE(output2.ok());
  EXPECT_EQ(output2.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(output2.status().message(),
            absl::StrFormat(kInvalidURLErrorMessage, kInvalidURL));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
