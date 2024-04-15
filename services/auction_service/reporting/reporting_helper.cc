//  Copyright 2023 Google LLC
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
//  limitations under the License
#include "services/auction_service/reporting/reporting_helper.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.pb.h"
#include "rapidjson/document.h"
#include "services/auction_service/reporting/noiser_and_bucketer.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/util/json_util.h"
#include "services/common/util/post_auction_signals.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr int kStochasticalRoundingBits = 8;

// Collects log of the provided type into the output vector.
void ParseLogs(const rapidjson::Document& document, const std::string& log_type,
               std::vector<std::string>& out) {
  auto log_it = document.FindMember(log_type.c_str());
  if (log_it == document.MemberEnd()) {
    return;
  }
  for (const auto& log : log_it->value.GetArray()) {
    out.emplace_back(log.GetString());
  }
}

absl::StatusOr<ReportingResponse> ParseAndGetReportingResponse(
    bool enable_adtech_code_logging, const std::string& response) {
  ReportingResponse reporting_response{};
  PS_ASSIGN_OR_RETURN(rapidjson::Document document, ParseJsonString(response));
  if (!document.HasMember(kReportResultResponse)) {
    return reporting_response;
  }

  rapidjson::Value& response_obj = document[kReportResultResponse];
  auto& report_result_response = reporting_response.report_result_response;
  PS_ASSIGN_IF_PRESENT(report_result_response.report_result_url, response_obj,
                       kReportResultUrl, GetString);
  PS_ASSIGN_IF_PRESENT(report_result_response.send_report_to_invoked,
                       response_obj, kSendReportToInvoked, GetBool);
  PS_ASSIGN_IF_PRESENT(report_result_response.register_ad_beacon_invoked,
                       response_obj, kRegisterAdBeaconInvoked, GetBool);
  rapidjson::Value interaction_reporting_urls_map;
  PS_ASSIGN_IF_PRESENT(interaction_reporting_urls_map, response_obj,
                       kInteractionReportingUrlsWrapperResponse, GetObject);
  for (rapidjson::Value::MemberIterator it =
           interaction_reporting_urls_map.MemberBegin();
       it != interaction_reporting_urls_map.MemberEnd(); ++it) {
    const auto& [key, value] = *it;
    reporting_response.report_result_response.interaction_reporting_urls
        .try_emplace(key.GetString(), value.GetString());
  }
  if (enable_adtech_code_logging) {
    ParseLogs(document, kSellerLogs, reporting_response.seller_logs);
    ParseLogs(document, kSellerErrors, reporting_response.seller_error_logs);
    ParseLogs(document, kSellerWarnings,
              reporting_response.seller_warning_logs);
    ParseLogs(document, kBuyerLogs, reporting_response.buyer_logs);
    ParseLogs(document, kBuyerErrors, reporting_response.buyer_error_logs);
    ParseLogs(document, kBuyerWarnings, reporting_response.buyer_warning_logs);
  }
  if (!document.HasMember(kReportWinResponse)) {
    return reporting_response;
  }

  rapidjson::Value& report_win_obj = document[kReportWinResponse];
  auto& report_win_response = reporting_response.report_win_response;
  PS_ASSIGN_IF_PRESENT(report_win_response.report_win_url, report_win_obj,
                       kReportWinUrl, GetString);
  PS_ASSIGN_IF_PRESENT(report_win_response.send_report_to_invoked,
                       report_win_obj, kSendReportToInvoked, GetBool);
  PS_ASSIGN_IF_PRESENT(report_win_response.register_ad_beacon_invoked,
                       report_win_obj, kRegisterAdBeaconInvoked, GetBool);
  rapidjson::Value report_win_interaction_urls_map;
  PS_ASSIGN_IF_PRESENT(report_win_interaction_urls_map, report_win_obj,
                       kInteractionReportingUrlsWrapperResponse, GetObject);

  for (rapidjson::Value::MemberIterator it =
           report_win_interaction_urls_map.MemberBegin();
       it != report_win_interaction_urls_map.MemberEnd(); ++it) {
    const auto& [key, value] = *it;
    report_win_response.interaction_reporting_urls.try_emplace(
        key.GetString(), value.GetString());
  }
  return reporting_response;
}

// Stochastically rounds floating point value to 8 bit mantissa and 8
// bit exponent. If there is an error while generating the rounded
// number, -1 will be returned.
double GetEightBitRoundedValue(bool enable_noising, double value) {
  if (!enable_noising) {
    return value;
  }
  absl::StatusOr<double> rounded_value =
      RoundStochasticallyToKBits(value, kStochasticalRoundingBits);
  if (rounded_value.ok()) {
    return rounded_value.value();
  }
  return -1;
}

absl::StatusOr<std::string> GetSellerReportingSignals(
    const ReportingDispatchRequestData& dispatch_request_data,
    const ReportingDispatchRequestConfig& dispatch_request_config) {
  rapidjson::Document document;
  document.SetObject();
  // Convert the std::string to a rapidjson::Value object.
  rapidjson::Value hostname_value;
  hostname_value.SetString(dispatch_request_data.publisher_hostname.data(),
                           document.GetAllocator());
  rapidjson::Value render_URL_value;
  render_URL_value.SetString(
      dispatch_request_data.post_auction_signals.winning_ad_render_url.c_str(),
      document.GetAllocator());
  rapidjson::Value render_url_value;
  render_url_value.SetString(
      dispatch_request_data.post_auction_signals.winning_ad_render_url.c_str(),
      document.GetAllocator());

  rapidjson::Value interest_group_owner;
  interest_group_owner.SetString(
      dispatch_request_data.post_auction_signals.winning_ig_owner.c_str(),
      document.GetAllocator());

  document.AddMember(kTopWindowHostname, hostname_value,
                     document.GetAllocator());
  document.AddMember(kInterestGroupOwner, interest_group_owner,
                     document.GetAllocator());
  document.AddMember(kRenderURL, render_URL_value, document.GetAllocator());
  document.AddMember(kRenderUrl, render_url_value, document.GetAllocator());
  double bid = GetEightBitRoundedValue(
      dispatch_request_config.enable_report_win_input_noising,
      dispatch_request_data.post_auction_signals.winning_bid);
  if (bid > -1) {
    document.AddMember(kBid, bid, document.GetAllocator());
  }
  double desirability = GetEightBitRoundedValue(
      dispatch_request_config.enable_report_win_input_noising,
      dispatch_request_data.post_auction_signals.winning_score);
  if (desirability > -1) {
    document.AddMember(kDesirability, desirability, document.GetAllocator());
  }
  document.AddMember(
      kHighestScoringOtherBid,
      dispatch_request_data.post_auction_signals.highest_scoring_other_bid,
      document.GetAllocator());

  if (!dispatch_request_data.component_reporting_metadata.top_level_seller
           .empty()) {
    rapidjson::Value top_level_seller;
    top_level_seller.SetString(
        dispatch_request_data.component_reporting_metadata.top_level_seller
            .c_str(),
        document.GetAllocator());
    rapidjson::Value component_seller;
    component_seller.SetString(
        dispatch_request_data.component_reporting_metadata.component_seller
            .c_str(),
        document.GetAllocator());
    document.AddMember(kTopLevelSellerTag, top_level_seller,
                       document.GetAllocator());
    double modified_bid = GetEightBitRoundedValue(
        dispatch_request_config.enable_report_win_input_noising,
        dispatch_request_data.component_reporting_metadata.modified_bid);
    if (modified_bid > -1) {
      document.AddMember(kModifiedBid, modified_bid, document.GetAllocator());
    }
    document.AddMember(kComponentSeller, component_seller,
                       document.GetAllocator());
  }
  return SerializeJsonDoc(document);
}

std::string GetBuyerMetadataJson(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const ReportingDispatchRequestData& dispatch_request_data) {
  if (!dispatch_request_config.enable_report_win_url_generation) {
    return kDefaultBuyerReportingMetadata;
  }

  rapidjson::Document buyer_reporting_signals_obj;
  buyer_reporting_signals_obj.SetObject();
  buyer_reporting_signals_obj.AddMember(
      kEnableReportWinUrlGeneration,
      dispatch_request_config.enable_report_win_url_generation,
      buyer_reporting_signals_obj.GetAllocator());
  buyer_reporting_signals_obj.AddMember(
      kEnableProtectedAppSignals,
      dispatch_request_config.enable_protected_app_signals,
      buyer_reporting_signals_obj.GetAllocator());
  absl::StatusOr<rapidjson::Document> buyer_signals_obj;
  if (!dispatch_request_data.buyer_reporting_metadata.buyer_signals.empty()) {
    buyer_signals_obj = ParseJsonString(
        dispatch_request_data.buyer_reporting_metadata.buyer_signals);
    if (!buyer_signals_obj.ok()) {
      PS_VLOG(1, dispatch_request_data.log_context)
          << "Error parsing buyer signals to Json object";
    } else {
      buyer_reporting_signals_obj.AddMember(
          kBuyerSignals, *std::move(buyer_signals_obj),
          buyer_reporting_signals_obj.GetAllocator());
    }
  }
  rapidjson::Value buyer_origin;
  buyer_origin.SetString(
      dispatch_request_data.post_auction_signals.winning_ig_owner.c_str(),
      buyer_reporting_signals_obj.GetAllocator());
  buyer_reporting_signals_obj.AddMember(
      kBuyerOriginTag, std::move(buyer_origin),
      buyer_reporting_signals_obj.GetAllocator());
  buyer_reporting_signals_obj.AddMember(
      kMadeHighestScoringOtherBid,
      dispatch_request_data.post_auction_signals.made_highest_scoring_other_bid,
      buyer_reporting_signals_obj.GetAllocator());
  if (dispatch_request_data.buyer_reporting_metadata.join_count.has_value()) {
    int join_count =
        dispatch_request_data.buyer_reporting_metadata.join_count.value();
    if (dispatch_request_config.enable_report_win_input_noising) {
      absl::StatusOr<int> noised_join_count =
          NoiseAndBucketJoinCount(join_count);
      if (noised_join_count.ok()) {
        join_count = noised_join_count.value();
      }
    }
    buyer_reporting_signals_obj.AddMember(
        kJoinCount, join_count, buyer_reporting_signals_obj.GetAllocator());
  }
  if (dispatch_request_data.buyer_reporting_metadata.recency.has_value()) {
    PS_VLOG(1, dispatch_request_data.log_context)
        << "BuyerReportingMetadata: Recency:"
        << dispatch_request_data.buyer_reporting_metadata.recency.value();
    long recency =
        dispatch_request_data.buyer_reporting_metadata.recency.value();
    if (dispatch_request_config.enable_report_win_input_noising) {
      absl::StatusOr<long> noised_recency = NoiseAndBucketRecency(recency);
      if (noised_recency.ok()) {
        recency = noised_recency.value();
      }
    }
    buyer_reporting_signals_obj.AddMember(
        kRecency, recency, buyer_reporting_signals_obj.GetAllocator());
  }
  if (dispatch_request_data.buyer_reporting_metadata.modeling_signals
          .has_value()) {
    int modeling_signals =
        dispatch_request_data.buyer_reporting_metadata.modeling_signals.value();
    if (dispatch_request_config.enable_report_win_input_noising) {
      absl::StatusOr<int> noised_modeling_signals =
          NoiseAndMaskModelingSignals(modeling_signals);
      if (noised_modeling_signals.ok()) {
        modeling_signals = noised_modeling_signals.value();
        buyer_reporting_signals_obj.AddMember(
            kModelingSignalsTag, modeling_signals,
            buyer_reporting_signals_obj.GetAllocator());
      }
    } else {
      buyer_reporting_signals_obj.AddMember(
          kModelingSignalsTag, modeling_signals,
          buyer_reporting_signals_obj.GetAllocator());
    }
  }
  rapidjson::Value seller;
  if (!dispatch_request_data.buyer_reporting_metadata.seller.empty()) {
    seller.SetString(
        dispatch_request_data.buyer_reporting_metadata.seller.c_str(),
        buyer_reporting_signals_obj.GetAllocator());
    buyer_reporting_signals_obj.AddMember(
        kSellerTag, std::move(seller),
        buyer_reporting_signals_obj.GetAllocator());
  }
  rapidjson::Value interest_group_name;
  interest_group_name.SetString(dispatch_request_data.buyer_reporting_metadata
                                    .interest_group_name.c_str(),
                                buyer_reporting_signals_obj.GetAllocator());
  buyer_reporting_signals_obj.AddMember(
      kInterestGroupName, std::move(interest_group_name),
      buyer_reporting_signals_obj.GetAllocator());
  double ad_cost = GetEightBitRoundedValue(
      dispatch_request_config.enable_report_win_input_noising,
      dispatch_request_data.buyer_reporting_metadata.ad_cost);
  if (ad_cost > -1) {
    buyer_reporting_signals_obj.AddMember(
        kAdCostTag, ad_cost, buyer_reporting_signals_obj.GetAllocator());
  }
  absl::StatusOr<std::string> buyer_reporting_metadata_json =
      SerializeJsonDoc(buyer_reporting_signals_obj);
  if (!buyer_reporting_metadata_json.ok()) {
    PS_VLOG(2, dispatch_request_data.log_context)
        << "Error constructing buyer_reporting_metadata_input for "
           "reportingEntryFunction";
    return kDefaultBuyerReportingMetadata;
  }
  return buyer_reporting_metadata_json.value();
}

std::vector<std::shared_ptr<std::string>> GetReportingInput(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const ReportingDispatchRequestData& dispatch_request_data) {
  absl::StatusOr<std::string> seller_reporting_signals =
      GetSellerReportingSignals(dispatch_request_data, dispatch_request_config);
  if (!seller_reporting_signals.ok()) {
    PS_VLOG(2, dispatch_request_data.log_context)
        << "Error generating Seller Reporting Signals for Reporting input";
    return {};
  }
  std::vector<std::shared_ptr<std::string>> input(
      dispatch_request_config.enable_protected_app_signals
          ? kReportingArgSizeWithProtectedAppSignals
          : kReportingArgSize);  // ReportingArgs size

  input[ReportingArgIndex(ReportingArgs::kAuctionConfig)] =
      dispatch_request_data.auction_config;
  input[ReportingArgIndex(ReportingArgs::kSellerReportingSignals)] =
      std::make_shared<std::string>(seller_reporting_signals.value());
  // This is only added to prevent errors in the reporting ad script, and
  // will always be an empty object.
  input[ReportingArgIndex(ReportingArgs::kDirectFromSellerSignals)] =
      std::make_shared<std::string>("{}");
  input[ReportingArgIndex(ReportingArgs::kEnableAdTechCodeLogging)] =
      std::make_shared<std::string>(absl::StrFormat(
          "%v", dispatch_request_config.enable_adtech_code_logging));
  std::string buyer_reporting_metadata_json =
      GetBuyerMetadataJson(dispatch_request_config, dispatch_request_data);
  input[ReportingArgIndex(ReportingArgs::kBuyerReportingMetadata)] =
      std::make_shared<std::string>(buyer_reporting_metadata_json);
  if (dispatch_request_config.enable_protected_app_signals) {
    input[ReportingArgIndex(ReportingArgs::kEgressFeatures)] =
        std::make_shared<std::string>(dispatch_request_data.egress_features);
  }

  PS_VLOG(2, dispatch_request_data.log_context)
      << "\n\nReporting Input Args:"
      << "\nAuction Config:\n"
      << *(input[ReportingArgIndex(ReportingArgs::kAuctionConfig)])
      << "\nSeller Reporting Signals:\n"
      << *(input[ReportingArgIndex(ReportingArgs::kSellerReportingSignals)])
      << "\nEnable AdTech Code Logging:\n"
      << *(input[ReportingArgIndex(ReportingArgs::kEnableAdTechCodeLogging)])
      << "\nDirect from Seller Signals:\n"
      << *(input[ReportingArgIndex(ReportingArgs::kDirectFromSellerSignals)])
      << "\nBuyer Reporting Metadata:\n"
      << *(input[ReportingArgIndex(ReportingArgs::kBuyerReportingMetadata)]);
  return input;
}

DispatchRequest GetReportingDispatchRequest(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const ReportingDispatchRequestData& dispatch_request_data) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  return {
      .id = dispatch_request_data.post_auction_signals.winning_ad_render_url,
      .version_string = kDispatchRequestVersion,
      .handler_name = dispatch_request_data.handler_name,
      .input =
          GetReportingInput(dispatch_request_config, dispatch_request_data),
  };
}

}  // namespace privacy_sandbox::bidding_auction_servers
