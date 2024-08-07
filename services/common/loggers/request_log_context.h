/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_LOGGERS_REQUEST_LOG_CONTEXT_H_
#define SERVICES_COMMON_LOGGERS_REQUEST_LOG_CONTEXT_H_

#include <utility>

#include "api/bidding_auction_servers.pb.h"
#include "src/logger/request_context_impl.h"

#define EVENT_MESSAGE_PROVIDER_SET(T, field) \
  void Set(const T& _##field) { *event_message_.mutable_##field() = _##field; }

#define EVENT_MESSAGE_PROVIDER_SET_RESPONSE(T, field)        \
  void Set(T _##field) {                                     \
    _##field.clear_debug_info();                             \
    *event_message_.mutable_##field() = std::move(_##field); \
  }

namespace privacy_sandbox::bidding_auction_servers {

class EventMessageProvider {
 public:
  EventMessage Get() { return event_message_; }

  EVENT_MESSAGE_PROVIDER_SET(SelectAdRequest, select_ad_request);
  EVENT_MESSAGE_PROVIDER_SET(ProtectedAuctionInput, protected_auction);
  EVENT_MESSAGE_PROVIDER_SET(ProtectedAudienceInput, protected_audience);
  EVENT_MESSAGE_PROVIDER_SET(AuctionResult, auction_result);

  EVENT_MESSAGE_PROVIDER_SET(GetBidsRequest, get_bid_request);
  EVENT_MESSAGE_PROVIDER_SET(GetBidsRequest::GetBidsRawRequest,
                             get_bid_raw_request);
  EVENT_MESSAGE_PROVIDER_SET_RESPONSE(GetBidsResponse::GetBidsRawResponse,
                                      get_bid_raw_response);

  EVENT_MESSAGE_PROVIDER_SET(GenerateBidsRequest, generate_bid_request);
  EVENT_MESSAGE_PROVIDER_SET(GenerateBidsRequest::GenerateBidsRawRequest,
                             generate_bid_raw_request);
  EVENT_MESSAGE_PROVIDER_SET_RESPONSE(
      GenerateBidsResponse::GenerateBidsRawResponse, generate_bid_raw_response);

  EVENT_MESSAGE_PROVIDER_SET(GenerateProtectedAppSignalsBidsRequest,
                             generate_app_signal_request);
  EVENT_MESSAGE_PROVIDER_SET(GenerateProtectedAppSignalsBidsRequest::
                                 GenerateProtectedAppSignalsBidsRawRequest,
                             generate_app_signal_raw_request);
  EVENT_MESSAGE_PROVIDER_SET_RESPONSE(
      GenerateProtectedAppSignalsBidsResponse ::
          GenerateProtectedAppSignalsBidsRawResponse,
      generate_app_signal_raw_response);

  EVENT_MESSAGE_PROVIDER_SET(ScoreAdsRequest, score_ad_request);
  EVENT_MESSAGE_PROVIDER_SET(ScoreAdsRequest::ScoreAdsRawRequest,
                             score_ad_raw_request);
  EVENT_MESSAGE_PROVIDER_SET_RESPONSE(ScoreAdsResponse::ScoreAdsRawResponse,
                                      score_ad_raw_response);

  void Set(absl::string_view udf_log) { event_message_.add_udf_log(udf_log); }

 private:
  EventMessage event_message_;
};

using RequestLogContext = server_common::log::ContextImpl<EventMessageProvider>;

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_LOGGERS_REQUEST_LOG_CONTEXT_H_
