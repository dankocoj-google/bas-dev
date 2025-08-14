//  Copyright 2025 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/bidding_service/utils/browser_signals_util.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
constexpr char kTopWindowHostname[] = "topWindowHostname";
constexpr char kSeller[] = "seller";
constexpr char kTopLevelSeller[] = "topLevelSeller";
constexpr char kTestStringWithQuote[] = "stop\", \"custom_key\": \"val";
TEST(MakeBrowserSignalsForScriptTest, SetsEscapedValuesInStringFields) {
  google::protobuf::RepeatedPtrField<std::string> component_urls;
  component_urls.Add(kTestStringWithQuote);
  std::string output = MakeBrowserSignalsForScript(
      kTestStringWithQuote, kTestStringWithQuote, kTestStringWithQuote,
      BrowserSignalsForBidding(), 1, 1, ForDebuggingOnlyFlags());

  auto parsed_output_status = ParseJsonString(output);
  ASSERT_TRUE(parsed_output_status.ok());
  const rapidjson::Document& parsed_output = *parsed_output_status;
  EXPECT_TRUE(parsed_output.FindMember("custom_key") ==
              parsed_output.MemberEnd());
  EXPECT_STREQ(parsed_output[kTopWindowHostname].GetString(),
               kTestStringWithQuote)
      << output;
  EXPECT_STREQ(parsed_output[kSeller].GetString(), kTestStringWithQuote);
  EXPECT_STREQ(parsed_output[kTopLevelSeller].GetString(),
               kTestStringWithQuote);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
