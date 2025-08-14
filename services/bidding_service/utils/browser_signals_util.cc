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
#include "services/bidding_service/utils/browser_signals_util.h"

#include "absl/status/statusor.h"
#include "include/rapidjson/document.h"
#include "include/rapidjson/stringbuffer.h"
#include "services/common/constants/common_constants.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"
namespace privacy_sandbox::bidding_auction_servers {
namespace {
constexpr char kTopWindowHostname[] = "topWindowHostname";
constexpr char kSeller[] = "seller";
constexpr char kMultiBidLimit[] = "multiBidLimit";
constexpr char kPrevWinsMs[] = "prevWinsMs";
constexpr char kForDebuggingOnlyInCooldownOrLockout[] =
    "forDebuggingOnlyInCooldownOrLockout";
}  // namespace

std::string MakeBrowserSignalsForScript(
    absl::string_view publisher_name, absl::string_view seller,
    absl::string_view top_level_seller,
    const BrowserSignalsForBidding& browser_signals, uint32_t data_version,
    int32_t multi_bid_limit, const ForDebuggingOnlyFlags& fdo_flags) {
  rapidjson::Document device_signals_doc(rapidjson::kObjectType);
  rapidjson::Document::AllocatorType& allocator =
      device_signals_doc.GetAllocator();

  device_signals_doc.AddMember(
      rapidjson::StringRef(kTopWindowHostname),
      rapidjson::StringRef(publisher_name.data(), publisher_name.size()),
      allocator);
  device_signals_doc.AddMember(
      rapidjson::StringRef(kSeller),
      rapidjson::StringRef(seller.data(), seller.size()), allocator);

  if (!top_level_seller.empty()) {
    device_signals_doc.AddMember(
        rapidjson::StringRef(kTopLevelSeller),
        rapidjson::StringRef(top_level_seller.data(), top_level_seller.size()),
        allocator);
  }
  device_signals_doc.AddMember(rapidjson::StringRef(kJoinCount),
                               browser_signals.join_count(), allocator);
  device_signals_doc.AddMember(rapidjson::StringRef(kBidCount),
                               browser_signals.bid_count(), allocator);

  // recency is expected to be in milli seconds.
  int64_t recency_ms;
  if (browser_signals.has_recency_ms()) {
    recency_ms = browser_signals.recency_ms();
  } else {
    recency_ms = static_cast<int64_t>(browser_signals.recency()) * 1000;
  }
  device_signals_doc.AddMember(rapidjson::StringRef(kRecency), recency_ms,
                               allocator);

  // TODO(b/394397742): Deprecate prevWins in favor of prevWinsMs.
  if (browser_signals.prev_wins().empty()) {
    device_signals_doc.AddMember(rapidjson::StringRef(kPrevWins),
                                 rapidjson::Value(rapidjson::kArrayType),
                                 allocator);
  } else if (auto prev_wins_doc = ParseJsonString(browser_signals.prev_wins());
             prev_wins_doc.ok()) {
    device_signals_doc.AddMember(
        rapidjson::StringRef(kPrevWins),
        // moving causes memory corruption
        rapidjson::Value().CopyFrom(*prev_wins_doc, allocator), allocator);
  } else {
    PS_LOG(ERROR) << "Error parsing prev wins array";
    device_signals_doc.AddMember(rapidjson::StringRef(kPrevWins),
                                 rapidjson::Value(rapidjson::kArrayType),
                                 allocator);
  }

  if (browser_signals.prev_wins_ms().empty()) {
    device_signals_doc.AddMember(rapidjson::StringRef(kPrevWinsMs),
                                 rapidjson::Value(rapidjson::kArrayType),
                                 allocator);
  } else if (absl::StatusOr<rapidjson::Document> prev_wins_ms_doc =
                 ParseJsonString(browser_signals.prev_wins_ms());
             prev_wins_ms_doc.ok()) {
    device_signals_doc.AddMember(
        rapidjson::StringRef(kPrevWinsMs),
        // moving causes memory corruption
        rapidjson::Value().CopyFrom(*prev_wins_ms_doc, allocator), allocator);
  } else {
    PS_LOG(ERROR) << "Error parsing prev wins ms array";
    device_signals_doc.AddMember(rapidjson::StringRef(kPrevWinsMs),
                                 rapidjson::Value(rapidjson::kArrayType),
                                 allocator);
  }

  device_signals_doc.AddMember(rapidjson::StringRef(kDataVersion), data_version,
                               allocator);
  device_signals_doc.AddMember(
      rapidjson::StringRef(kForDebuggingOnlyInCooldownOrLockout),
      fdo_flags.in_cooldown_or_lockout(), allocator);
  device_signals_doc.AddMember(
      rapidjson::StringRef(kMultiBidLimit),
      multi_bid_limit > 0 ? multi_bid_limit : kDefaultMultiBidLimit, allocator);

  absl::StatusOr<std::string> result = SerializeJsonDoc(device_signals_doc);
  // SerializeJsonDoc should not fail for a document constructed this way.
  return result.ok() ? *result : kEmptyDeviceSignals;
}

}  // namespace privacy_sandbox::bidding_auction_servers
