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

#ifndef SERVICES_BIDDING_SERVICE_BYOB_PROTO_UTILS_H_
#define SERVICES_BIDDING_SERVICE_BYOB_PROTO_UTILS_H_

#include <utility>

#include "absl/time/time.h"
#include "api/bidding_auction_servers.pb.h"
#include "api/udf/generate_bid_udf_interface.pb.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

inline void UpdateProtectedAudienceBidRequest(
    roma_service::GenerateProtectedAudienceBidRequest& bid_request,
    const GenerateBidsRequest::GenerateBidsRawRequest& raw_request,
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding&
        ig_for_bidding) {
  // Populate interest group.
  roma_service::ProtectedAudienceInterestGroup* interest_group =
      bid_request.mutable_interest_group();
  *interest_group->mutable_name() = std::move(*ig_for_bidding.mutable_name());
  interest_group->mutable_trusted_bidding_signals_keys()->Swap(
      ig_for_bidding.mutable_trusted_bidding_signals_keys());
  interest_group->mutable_ad_render_ids()->Swap(
      ig_for_bidding.mutable_ad_render_ids());
  interest_group->mutable_ad_component_render_ids()->Swap(
      ig_for_bidding.mutable_ad_component_render_ids());
  *interest_group->mutable_user_bidding_signals() =
      std::move(*ig_for_bidding.mutable_user_bidding_signals());

  *bid_request.mutable_trusted_bidding_signals() =
      std::move(*ig_for_bidding.mutable_trusted_bidding_signals());

  // Populate (oneof) device signals.
  if (ig_for_bidding.has_android_signals_for_bidding() &&
      ig_for_bidding.android_signals_for_bidding().IsInitialized()) {
    roma_service::ProtectedAudienceAndroidSignals* android_signals =
        bid_request.mutable_android_signals();
    android_signals->set_top_level_seller(raw_request.top_level_seller());
  } else if (ig_for_bidding.has_browser_signals_for_bidding() &&
             ig_for_bidding.browser_signals_for_bidding().IsInitialized()) {
    roma_service::ProtectedAudienceBrowserSignals* browser_signals =
        bid_request.mutable_browser_signals();
    browser_signals->set_top_window_hostname(raw_request.publisher_name());
    browser_signals->set_seller(raw_request.seller());
    browser_signals->set_top_level_seller(raw_request.top_level_seller());
    browser_signals->set_join_count(
        ig_for_bidding.browser_signals_for_bidding().join_count());
    browser_signals->set_bid_count(
        ig_for_bidding.browser_signals_for_bidding().bid_count());
    if (ig_for_bidding.browser_signals_for_bidding().has_recency_ms()) {
      browser_signals->set_recency(
          ig_for_bidding.browser_signals_for_bidding().recency_ms());
    } else {
      browser_signals->set_recency(
          ig_for_bidding.browser_signals_for_bidding().recency() * 1000);
    }
    *browser_signals->mutable_prev_wins() =
        std::move(*ig_for_bidding.mutable_browser_signals_for_bidding()
                       ->mutable_prev_wins());
    *browser_signals->mutable_prev_wins_ms() =
        std::move(*ig_for_bidding.mutable_browser_signals_for_bidding()
                       ->mutable_prev_wins_ms());
    browser_signals->set_multi_bid_limit(raw_request.multi_bid_limit());
    browser_signals->set_for_debugging_only_in_cooldown_or_lockout(
        raw_request.fdo_flags().in_cooldown_or_lockout());
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_BIDDING_SERVICE_BYOB_PROTO_UTILS_H_
