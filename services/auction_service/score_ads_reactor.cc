//  Copyright 2022 Google LLC
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

#include "score_ads_reactor.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_replace.h"
#include "rapidjson/document.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/private_aggregation/private_aggregation_manager.h"
#include "services/auction_service/reporting/buyer/buyer_reporting_helper.h"
#include "services/auction_service/reporting/buyer/pa_buyer_reporting_manager.h"
#include "services/auction_service/reporting/buyer/pas_buyer_reporting_manager.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/reporting/seller/component_seller_reporting_manager.h"
#include "services/auction_service/reporting/seller/seller_reporting_manager.h"
#include "services/auction_service/reporting/seller/top_level_seller_reporting_manager.h"
#include "services/auction_service/utils/proto_utils.h"
#include "services/common/attestation/adtech_enrollment_cache.h"
#include "services/common/attestation/attestation_util.h"
#include "services/common/constants/common_constants.h"
#include "services/common/metric/roma_metric_utils.h"
#include "services/common/private_aggregation/private_aggregation_post_auction_util.h"
#include "services/common/util/auction_scope_util.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;
using ::google::protobuf::RepeatedPtrField;
using HighestScoringOtherBidsMap =
    ::google::protobuf::Map<std::string, google::protobuf::ListValue>;
using server_common::log::PS_VLOG_IS_ON;

constexpr long kBytesMultiplyer = 1024;

inline void MayVlogRomaResponses(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses,
    RequestLogContext& log_context) {
  if (PS_VLOG_IS_ON(kDispatch)) {
    for (const auto& dispatch_response : responses) {
      PS_VLOG(kDispatch, log_context)
          << "ScoreAds V8 Response: " << dispatch_response.status();
      if (dispatch_response.ok()) {
        PS_VLOG(kDispatch, log_context) << dispatch_response->resp;
      }
    }
  }
}

inline void LogWarningForBadResponse(
    const absl::Status& status, const DispatchResponse& response,
    const AdWithBidMetadata* ad_with_bid_metadata,
    RequestLogContext& log_context) {
  PS_LOG(ERROR, log_context) << "Failed to parse response from Roma ",
      status.ToString(absl::StatusToStringMode::kWithEverything);
  if (ad_with_bid_metadata) {
    PS_LOG(WARNING, log_context)
        << "Invalid json output from code execution for interest group "
        << ad_with_bid_metadata->interest_group_name() << ": " << response.resp;
  } else {
    PS_LOG(WARNING, log_context)
        << "Invalid json output from code execution for protected app signals "
           "ad: "
        << response.resp;
  }
}

inline void UpdateHighestScoringOtherBidMap(
    float bid, absl::string_view owner,
    HighestScoringOtherBidsMap& highest_scoring_other_bids_map) {
  highest_scoring_other_bids_map.try_emplace(owner,
                                             google::protobuf::ListValue());
  highest_scoring_other_bids_map.at(owner).add_values()->set_number_value(bid);
}

// If the bid's original currency matches the seller currency, the
// incomingBidInSellerCurrency must be unchanged by scoreAd(). If it is
// changed, reject the bid.
inline bool IsIncomingBidInSellerCurrencyIllegallyModified(
    absl::string_view seller_currency, absl::string_view ad_with_bid_currency,
    float incoming_bid_in_seller_currency, float buyer_bid) {
  return  // First check if the original currency matches the seller
          // currency.
      !seller_currency.empty() && !ad_with_bid_currency.empty() &&
      seller_currency == ad_with_bid_currency &&
      // Then check that the incomingBidInSellerCurrency was set (non-zero).
      (incoming_bid_in_seller_currency > kCurrencyFloatComparisonEpsilon) &&
      // Only now do we check if it was modified.
      (fabsf(incoming_bid_in_seller_currency - buyer_bid) >
       kCurrencyFloatComparisonEpsilon);
}

// Generates GetComponentReportingDataInAuctionResult based on auction_result.
ComponentReportingDataInAuctionResult GetComponentReportingDataInAuctionResult(
    AuctionResult& auction_result) {
  ComponentReportingDataInAuctionResult
      component_reporting_data_in_auction_result;
  component_reporting_data_in_auction_result.component_seller =
      auction_result.auction_params().component_seller();
  component_reporting_data_in_auction_result.win_reporting_urls =
      std::move(*auction_result.mutable_win_reporting_urls());
  component_reporting_data_in_auction_result.buyer_reporting_id =
      auction_result.buyer_reporting_id();
  component_reporting_data_in_auction_result.buyer_and_seller_reporting_id =
      auction_result.buyer_and_seller_reporting_id();
  return component_reporting_data_in_auction_result;
}

// Populates reporting urls of component level seller and buyer in top level
// seller's ScoreAdResponse.
void PopulateComponentReportingUrlsInTopLevelResponse(
    absl::string_view dispatch_id,
    ScoreAdsResponse::ScoreAdsRawResponse& raw_response,
    absl::flat_hash_map<std::string, ComponentReportingDataInAuctionResult>&
        component_level_reporting_data) {
  if (auto ad_it = component_level_reporting_data.find(dispatch_id);
      ad_it != component_level_reporting_data.end()) {
    auto* seller_reporting_urls =
        ad_it->second.win_reporting_urls
            .mutable_component_seller_reporting_urls();
    raw_response.mutable_ad_score()
        ->mutable_win_reporting_urls()
        ->mutable_component_seller_reporting_urls()
        ->set_reporting_url(
            std::move(*seller_reporting_urls->mutable_reporting_url()));
    for (auto& [event, url] :
         *seller_reporting_urls->mutable_interaction_reporting_urls()) {
      raw_response.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_component_seller_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, std::move(url));
    }

    auto* component_buyer_reporting_urls =
        ad_it->second.win_reporting_urls.mutable_buyer_reporting_urls();
    raw_response.mutable_ad_score()
        ->mutable_win_reporting_urls()
        ->mutable_buyer_reporting_urls()
        ->set_reporting_url(std::move(
            *component_buyer_reporting_urls->mutable_reporting_url()));
    for (auto& [event, url] : *component_buyer_reporting_urls
                                   ->mutable_interaction_reporting_urls()) {
      raw_response.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_buyer_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, std::move(url));
    }
  }
}

// If this is a component auction, and a seller currency is set,
// and a modified bid is set, and a currency for that modified bid is set,
// it must match the seller currency. If not, reject the bid.
inline bool IsBidCurrencyMismatched(AuctionScope auction_scope,
                                    absl::string_view seller_currency,
                                    float modified_bid,
                                    absl::string_view modified_bid_currency) {
  return IsComponentAuction(auction_scope) && !seller_currency.empty() &&
         modified_bid > 0.0f && !modified_bid_currency.empty() &&
         modified_bid_currency != seller_currency;
}

// If this is a component auction and the modified bid was not set,
// the original buyer bid (and its currency) will be used.
// Returns whether the bid was updated.
bool CheckAndUpdateModifiedBid(AuctionScope auction_scope, float buyer_bid,
                               absl::string_view ad_with_bid_currency,
                               ScoreAdsResponse::AdScore* ad_score) {
  if (IsComponentAuction(auction_scope) && ad_score->bid() <= 0.0f) {
    if (buyer_bid > 0.0f) {
      ad_score->set_bid(buyer_bid);
      if (!ad_with_bid_currency.empty()) {
        ad_score->set_bid_currency(ad_with_bid_currency);
      } else {
        ad_score->clear_bid_currency();
      }
      return true;
    }
  }
  return false;
}

void SetSellerReportingUrlsInResponse(
    ReportResultResponse report_result_response,
    ScoreAdsResponse::ScoreAdsRawResponse& raw_response) {
  auto top_level_reporting_urls =
      raw_response.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_top_level_seller_reporting_urls();
  top_level_reporting_urls->set_reporting_url(
      std::move(report_result_response.report_result_url));
  auto mutable_interaction_url =
      top_level_reporting_urls->mutable_interaction_reporting_urls();
  for (const auto& [event, interactionReportingUrl] :
       report_result_response.interaction_reporting_urls) {
    mutable_interaction_url->try_emplace(event, interactionReportingUrl);
  }
}

void SetSellerReportingUrlsInResponseForComponentAuction(
    ReportResultResponse report_result_response,
    ScoreAdsResponse::ScoreAdsRawResponse& raw_response) {
  auto component_seller_reporting_urls =
      raw_response.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_component_seller_reporting_urls();
  component_seller_reporting_urls->set_reporting_url(
      std::move(report_result_response.report_result_url));
  auto mutable_interaction_url =
      component_seller_reporting_urls->mutable_interaction_reporting_urls();
  for (const auto& [event, interactionReportingUrl] :
       report_result_response.interaction_reporting_urls) {
    mutable_interaction_url->try_emplace(event, interactionReportingUrl);
  }
}

// Grouping of data derived from *AdWithBidMetadata that is needed to score
// the ads.
struct ScoringAdWithBidMetadata {
  float buyer_bid = 0.0;
  absl::string_view ad_with_bid_currency = "";
  absl::string_view interest_group_name = "";
  absl::string_view interest_group_owner = "";
  absl::string_view interest_group_origin = "";
  bool k_anon_status = true;
  AdType ad_type;
};

ScoringAdWithBidMetadata GetScoringDataFromAd(const AdWithBidMetadata& ad) {
  return {.buyer_bid = ad.bid(),
          .ad_with_bid_currency = ad.bid_currency(),
          .interest_group_name = ad.interest_group_name(),
          .interest_group_owner = ad.interest_group_owner(),
          .interest_group_origin = ad.interest_group_origin(),
          .k_anon_status = ad.k_anon_status(),
          .ad_type = AdType::AD_TYPE_PROTECTED_AUDIENCE_AD};
}

ScoringAdWithBidMetadata GetScoringDataFromAd(
    const ProtectedAppSignalsAdWithBidMetadata& ad) {
  return {.buyer_bid = ad.bid(),
          .ad_with_bid_currency = ad.bid_currency(),
          .interest_group_owner = ad.owner(),
          .k_anon_status = ad.k_anon_status(),
          .ad_type = AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD};
}

// Populates relevant data in the AdScore object. The input data is sourced
// from (ProtectedAppSignals)AdWithBidMetadata object.
ScoringAdWithBidMetadata PopulateAdScoreData(ScoredAdData& parsed_ad) {
  ScoringAdWithBidMetadata scoring_ad_with_bid_metadata;
  ScoreAdsResponse::AdScore& ad_score = parsed_ad.ad_score;
  if (parsed_ad.protected_audience_ad_with_bid) {
    scoring_ad_with_bid_metadata =
        GetScoringDataFromAd(*parsed_ad.protected_audience_ad_with_bid);
  } else {
    scoring_ad_with_bid_metadata =
        GetScoringDataFromAd(*parsed_ad.protected_app_signals_ad_with_bid);
  }
  ad_score.set_interest_group_name(
      scoring_ad_with_bid_metadata.interest_group_name);
  ad_score.set_interest_group_owner(
      scoring_ad_with_bid_metadata.interest_group_owner);
  ad_score.set_interest_group_origin(
      scoring_ad_with_bid_metadata.interest_group_origin);
  ad_score.set_ad_type(scoring_ad_with_bid_metadata.ad_type);
  ad_score.set_buyer_bid(scoring_ad_with_bid_metadata.buyer_bid);
  ad_score.set_buyer_bid_currency(
      scoring_ad_with_bid_metadata.ad_with_bid_currency);
  return scoring_ad_with_bid_metadata;
}

// Populates candidates for winning ads as well as ghost winning ads.
inline void AddIfCandsEmptyOrHasEqLastDesirability(
    int ind_to_add, int desirability,
    const std::vector<ScoredAdData>& parsed_ads, std::vector<int>& cands) {
  if (cands.empty() ||
      parsed_ads[cands.back()].ad_score.desirability() == desirability) {
    cands.push_back(ind_to_add);
  }
}

std::vector<int> ChooseRandomElements(const std::vector<int>& to_sample_from,
                                      int num_elements_to_get) {
  DCHECK(num_elements_to_get <= to_sample_from.size());
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  std::vector<int> sampled;
  sampled.reserve(num_elements_to_get);
  std::sample(to_sample_from.begin(), to_sample_from.end(),
              std::back_inserter(sampled), num_elements_to_get, generator);
  return sampled;
}

inline BaseValues GetPaggBaseValueWithoutRejectReason(
    const PostAuctionSignals& post_auction_signals) {
  BaseValues base_values;
  if (post_auction_signals.winning_bid_in_seller_currency > 0.0f) {
    base_values.winning_bid =
        post_auction_signals.winning_bid_in_seller_currency;
  } else if (post_auction_signals.winning_bid > 0.0f) {
    base_values.winning_bid = post_auction_signals.winning_bid;
  } else {
    PS_VLOG(kNoisyInfo)
        << "Invalid winning bid. Dropping Private Aggregation data";
  }
  base_values.highest_scoring_other_bid =
      post_auction_signals.highest_scoring_other_bid;
  return base_values;
}
}  // namespace

void ScoringData::ChooseWinnerAndGhostWinners(size_t max_ghost_winners) {
  if (!winner_cand_indices.empty()) {
    std::vector<int> winner_ind =
        ChooseRandomElements(winner_cand_indices, /*num_elements_to_get=*/1);
    winner_index = winner_ind[0];
  }

  if (!ghost_winner_cand_indices.empty()) {
    if (ghost_winner_cand_indices.size() < max_ghost_winners) {
      ghost_winner_indices = ghost_winner_cand_indices;
    } else {
      ghost_winner_indices = ChooseRandomElements(
          ghost_winner_cand_indices,
          std::min(max_ghost_winners, ghost_winner_cand_indices.size()));
    }
  }
}

ScoreAdsReactor::ScoreAdsReactor(
    grpc::CallbackServerContext* context, V8DispatchClient& dispatcher,
    const ScoreAdsRequest* request, ScoreAdsResponse* response,
    std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const AsyncReporter& async_reporter,
    const AuctionServiceRuntimeConfig& runtime_config,
    AdtechEnrollmentCacheInterface* adtech_attestation_cache)
    : CodeDispatchReactor<ScoreAdsRequest, ScoreAdsRequest::ScoreAdsRawRequest,
                          ScoreAdsResponse,
                          ScoreAdsResponse::ScoreAdsRawResponse>(
          request, response, key_fetcher_manager, crypto_client,
          runtime_config.enable_cancellation, runtime_config.enable_kanon),
      context_(context),
      dispatcher_(dispatcher),
      benchmarking_logger_(std::move(benchmarking_logger)),
      async_reporter_(async_reporter),
      enable_seller_debug_url_generation_(
          runtime_config.enable_seller_debug_url_generation),
      roma_timeout_duration_(runtime_config.roma_timeout_duration),
      log_context_(
          GetLoggingContext(raw_request_),
          raw_request_.consented_debug_config(),
          [this]() { return raw_response_.mutable_debug_info(); },
          this->raw_request_.is_sampled_for_debug()),
      roma_request_context_factory_(
          GetLoggingContext(raw_request_),
          raw_request_.consented_debug_config(),
          [this]() { return raw_response_.mutable_debug_info(); }),
      enable_adtech_code_logging_(log_context_.is_consented() ||
                                  this->raw_request_.is_sampled_for_debug()),
      enable_report_result_url_generation_(
          runtime_config.enable_report_result_url_generation),
      enable_protected_app_signals_(
          runtime_config.enable_protected_app_signals),
      enable_private_aggregate_reporting_(
          runtime_config.enable_private_aggregate_reporting),
      enable_report_win_input_noising_(
          runtime_config.enable_report_win_input_noising),
      max_allowed_size_debug_url_chars_(
          runtime_config.max_allowed_size_debug_url_bytes),
      max_allowed_size_all_debug_urls_chars_(
          kBytesMultiplyer * runtime_config.max_allowed_size_all_debug_urls_kb),
      debug_reporting_sampling_upper_bound_(
          runtime_config.debug_reporting_sampling_upper_bound),
      auction_scope_(GetAuctionScope(raw_request_)),
      enable_report_win_url_generation_(
          runtime_config.enable_report_win_url_generation &&
          auction_scope_ !=
              AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER),
      enable_seller_and_buyer_code_isolation_(
          runtime_config.enable_seller_and_buyer_udf_isolation),
      require_scoring_signals_for_scoring_(
          runtime_config.require_scoring_signals_for_scoring),
      enable_enforce_kanon_(runtime_config.enable_kanon &&
                            raw_request_.enforce_kanon()),
      buyers_with_report_win_enabled_(
          runtime_config.buyers_with_report_win_enabled),
      protected_app_signals_buyers_with_report_win_enabled_(
          runtime_config.protected_app_signals_buyers_with_report_win_enabled),
      adtech_attestation_cache_(adtech_attestation_cache) {
  PS_CHECK_OK(
      [this]() {
        PS_ASSIGN_OR_RETURN(metric_context_,
                            metric::AuctionContextMap()->Remove(request_));
        if (log_context_.is_consented()) {
          metric_context_->SetConsented(
              raw_request_.log_context().generation_id());
        } else if (log_context_.is_prod_debug()) {
          metric_context_->SetConsented(kProdDebug.data());
        }
        return absl::OkStatus();
      }(),
      log_context_)
      << "AuctionContextMap()->Get(request) should have been called";

  if (runtime_config.use_per_request_udf_versioning &&
      !raw_request_.score_ad_version().empty()) {
    code_version_ = raw_request_.score_ad_version();
  } else {
    code_version_ = runtime_config.default_score_ad_version;
  }
}

absl::btree_map<std::string, std::string> ScoreAdsReactor::GetLoggingContext(
    const ScoreAdsRequest::ScoreAdsRawRequest& score_ads_request) {
  const auto& log_context = score_ads_request.log_context();
  return {{kGenerationId, log_context.generation_id()},
          {kSellerDebugId, log_context.adtech_debug_id()}};
}

void ScoreAdsReactor::BuildScoreAdRequestForComponentGhostWinners(
    bool enable_debug_reporting,
    const std::shared_ptr<std::string>& auction_config,
    AuctionResult& auction_result,
    std::vector<DispatchRequest>& dispatch_requests,
    RomaRequestSharedContext& shared_context) {
  PS_VLOG(8, log_context_) << __func__;
  for (auto& ghost_winner : *auction_result.mutable_k_anon_ghost_winners()) {
    if (!ghost_winner.has_ghost_winner_for_top_level_auction()) {
      continue;
    }

    auto& ghost_winner_for_top_level_auction =
        *ghost_winner.mutable_ghost_winner_for_top_level_auction();
    auto dispatch_request = BuildScoreAdRequest(
        ghost_winner_for_top_level_auction.ad_render_url(),
        ghost_winner_for_top_level_auction.ad_metadata(),
        /*scoring_signals = */ "{}",
        ghost_winner_for_top_level_auction.modified_bid(), auction_config,
        MakeBidMetadataForTopLevelAuction(
            raw_request_.publisher_hostname(), ghost_winner.owner(),
            ghost_winner_for_top_level_auction.ad_render_url(),
            ghost_winner_for_top_level_auction.ad_component_render_urls(),
            auction_result.auction_params().component_seller(),
            ghost_winner_for_top_level_auction.bid_currency(),
            raw_request_.seller_data_version()),
        log_context_, enable_adtech_code_logging_, enable_debug_reporting,
        code_version_);
    if (!dispatch_request.ok()) {
      PS_VLOG(kDispatch, log_context_)
          << "Failed to create scoring request for component ghost winner: "
          << dispatch_request.status();
      continue;
    }

    bool insertion_success = false;
    switch (ghost_winner_for_top_level_auction.ad_type()) {
      case AdType::AD_TYPE_PROTECTED_AUDIENCE_AD: {
        auto [unused_it, inserted] = ad_data_.try_emplace(
            dispatch_request->id,
            MapKAnonGhostWinnerToAdWithBidMetadata(
                ghost_winner.owner(), ghost_winner.ig_name(),
                ghost_winner_for_top_level_auction));
        insertion_success = inserted;
        break;
      }
      case AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD: {
        auto [unused_it, inserted] = protected_app_signals_ad_data_.try_emplace(
            dispatch_request->id,
            MapKAnonGhostWinnerToProtectedAppSignalsAdWithBidMetadata(
                ghost_winner.owner(), ghost_winner_for_top_level_auction));
        insertion_success = inserted;
        break;
      }
      default:
        PS_VLOG(kNoisyWarn, log_context_)
            << "Skipped ghost winner ad with unknown ad type "
            << ghost_winner_for_top_level_auction.DebugString();
        continue;
    }

    if (!insertion_success) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Component ghost winner ScoreAd Request id "
          << "conflict detected: " << dispatch_request->id;
      LogIfError(
          metric_context_
              ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                  1, metric::kAuctionScoreAdsFailedToInsertDispatchRequest));
      continue;
    }
    PS_VLOG(kDispatch, log_context_)
        << "Created scoring request for component ghost winner: "
        << ghost_winner_for_top_level_auction;
    ad_k_anon_join_cand_.try_emplace(
        dispatch_request->id,
        std::move(*ghost_winner.mutable_k_anon_join_candidates()));

    dispatch_request->metadata = shared_context;
    dispatch_request->tags[kRomaTimeoutTag] = roma_timeout_duration_;
    dispatch_requests.push_back(*std::move(dispatch_request));
  }
}

void ScoreAdsReactor::BuildScoreAdRequestForComponentWinner(
    bool enable_debug_reporting,
    const std::shared_ptr<std::string>& auction_config,
    AuctionResult& auction_result,
    std::vector<DispatchRequest>& dispatch_requests,
    RomaRequestSharedContext& shared_context) {
  auto dispatch_request = BuildScoreAdRequest(
      auction_result.ad_render_url(), auction_result.ad_metadata(),
      /*scoring_signals = */ "{}", auction_result.bid(), auction_config,
      MakeBidMetadataForTopLevelAuction(
          raw_request_.publisher_hostname(),
          auction_result.interest_group_owner(), auction_result.ad_render_url(),
          auction_result.ad_component_render_urls(),
          auction_result.auction_params().component_seller(),
          auction_result.bid_currency(), raw_request_.seller_data_version()),
      log_context_, enable_adtech_code_logging_, enable_debug_reporting,
      code_version_);
  if (!dispatch_request.ok()) {
    PS_VLOG(kDispatch, log_context_)
        << "Failed to create scoring request for component candidate: "
        << dispatch_request.status();
    return;
  }

  auto [unused_it_reporting, reporting_inserted] =
      component_level_reporting_data_.emplace(
          dispatch_request->id,
          GetComponentReportingDataInAuctionResult(auction_result));
  if (!reporting_inserted) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Could not insert reporting data for component candidate. ScoreAd "
           "Request id "
           "conflict detected: "
        << dispatch_request->id;
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                1, metric::kAuctionScoreAdsFailedToInsertDispatchRequest));
    return;
  }

  // Map all fields from a component auction result to a
  // ProtectedAppSignals/ProtectedAudience AdWithBidMetadata used in this
  // reactor. This way all the parsing and result handling logic for single
  // seller auctions can be re-used for top-level auctions.
  bool insertion_success = false;
  switch (auction_result.ad_type()) {
    case AdType::AD_TYPE_PROTECTED_AUDIENCE_AD: {
      auto [unused_it, inserted] = ad_data_.try_emplace(
          dispatch_request->id, MapAuctionResultToAdWithBidMetadata(
                                    auction_result, /*k_anon_status=*/true));
      insertion_success = inserted;
      break;
    }
    case AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD: {
      auto [unused_it, inserted] = protected_app_signals_ad_data_.try_emplace(
          dispatch_request->id,
          MapAuctionResultToProtectedAppSignalsAdWithBidMetadata(
              auction_result,
              /*k_anon_status=*/true));
      insertion_success = inserted;
      break;
    }
    default:
      PS_VLOG(kNoisyWarn, log_context_)
          << "Skipped ad with unknown ad type " << auction_result.DebugString();
      return;
  }

  if (!insertion_success) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Component candidate ScoreAd Request id "
           "conflict detected: "
        << dispatch_request->id;
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                1, metric::kAuctionScoreAdsFailedToInsertDispatchRequest));
    return;
  }

  component_ad_seller_.try_emplace(
      dispatch_request->id, std::move(*auction_result.mutable_auction_params()
                                           ->mutable_component_seller()));

  ad_k_anon_join_cand_.try_emplace(
      dispatch_request->id,
      std::move(*auction_result.mutable_k_anon_winner_join_candidates()));

  dispatch_request->metadata = shared_context;
  dispatch_request->tags[kRomaTimeoutTag] = roma_timeout_duration_;
  dispatch_requests.push_back(*std::move(dispatch_request));
}

absl::StatusOr<std::vector<DispatchRequest>>
ScoreAdsReactor::PopulateTopLevelAuctionDispatchRequests(
    bool enable_debug_reporting,
    const std::shared_ptr<std::string>& auction_config,
    google::protobuf::RepeatedPtrField<AuctionResult>&
        component_auction_results,
    RomaRequestSharedContext& shared_context) {
  absl::string_view generation_id;
  PS_VLOG(8, log_context_) << __func__;
  std::vector<DispatchRequest> dispatch_requests;
  dispatch_requests.reserve(component_auction_results.size());
  for (auto& auction_result : component_auction_results) {
    if (auction_result.is_chaff() ||
        auction_result.auction_params().component_seller().empty()) {
      continue;
    }
    if (generation_id.empty()) {
      generation_id =
          auction_result.auction_params().ciphertext_generation_id();
    } else if (generation_id !=
               auction_result.auction_params().ciphertext_generation_id()) {
      // Ignore auction result for different generation id.
      return absl::Status(
          absl::StatusCode::kInvalidArgument,
          "Mismatched generation ids in component auction results.");
    }

    if (!auction_result.ad_render_url().empty()) {
      BuildScoreAdRequestForComponentWinner(enable_debug_reporting,
                                            auction_config, auction_result,
                                            dispatch_requests, shared_context);
    }

    if (enable_enforce_kanon_ &&
        !auction_result.k_anon_ghost_winners().empty()) {
      BuildScoreAdRequestForComponentGhostWinners(
          enable_debug_reporting, auction_config, auction_result,
          dispatch_requests, shared_context);
    }
  }

  return dispatch_requests;
}

void ScoreAdsReactor::PopulateProtectedAudienceDispatchRequests(
    std::vector<DispatchRequest>& dispatch_requests,
    bool enable_debug_reporting,
    absl::Nullable<
        const absl::flat_hash_map<std::string, rapidjson::StringBuffer>*>
        scoring_signals,
    const std::shared_ptr<std::string>& auction_config,
    google::protobuf::RepeatedPtrField<AdWithBidMetadata>& ads,
    RomaRequestSharedContext& shared_context) {
  if (require_scoring_signals_for_scoring_ && scoring_signals == nullptr) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Skipping ALL protected audience ads since scoring signals are "
           "required but null.";
    return;
  }
  while (!ads.empty()) {
    std::unique_ptr<AdWithBidMetadata> ad(ads.ReleaseLast());
    absl::string_view scoring_signals_str = kNullScoringSignalsJson;
    if (scoring_signals != nullptr) {
      auto scoring_signals_it = scoring_signals->find(ad->render());
      if (require_scoring_signals_for_scoring_ &&
          scoring_signals_it == scoring_signals->end()) {
        PS_VLOG(kNoisyWarn, log_context_)
            << "Skipping protected audience ad since render "
               "URL is not found in the scoring signals: "
            << ad->render();
        continue;
      }
      if (scoring_signals_it != scoring_signals->end()) {
        // Iterators hold references to the underlying elements in their
        // collection, they do not make copies - and .GetString() is a rapidjson
        // method returning a c-style char *, so it should not be making a copy
        // either. Therefore taking a reference to this (which is what a
        // string_view is) is safe.
        scoring_signals_str = scoring_signals_it->second.GetString();
      }
    }
    ReportingIdsParamForBidMetadata reporting_id_param;
    if (ad->has_selected_buyer_and_seller_reporting_id()) {
      reporting_id_param.selected_buyer_and_seller_reporting_id =
          ad->selected_buyer_and_seller_reporting_id();
    }
    if (ad->has_buyer_and_seller_reporting_id()) {
      reporting_id_param.buyer_and_seller_reporting_id =
          ad->buyer_and_seller_reporting_id();
    }
    if (ad->has_buyer_reporting_id()) {
      reporting_id_param.buyer_reporting_id = ad->buyer_reporting_id();
    }
    auto dispatch_request = BuildScoreAdRequest(
        *ad, auction_config, scoring_signals_str, enable_debug_reporting,
        log_context_, enable_adtech_code_logging_,
        MakeBidMetadata(raw_request_.publisher_hostname(),
                        ad->interest_group_owner(), ad->render(),
                        ad->ad_components(), raw_request_.top_level_seller(),
                        ad->bid_currency(), raw_request_.seller_data_version(),
                        reporting_id_param, raw_request_.fdo_flags()),
        code_version_);
    if (!dispatch_request.ok()) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Failed to create scoring request for protected audience: "
          << dispatch_request.status();
      continue;
    }
    auto [unused_it, inserted] =
        ad_data_.emplace(dispatch_request->id, std::move(ad));
    if (!inserted) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Protected Audience ScoreAd Request id "
             "conflict detected: "
          << dispatch_request->id;
      continue;
    }

    dispatch_request->metadata = shared_context;
    dispatch_request->tags[kRomaTimeoutTag] = roma_timeout_duration_;
    dispatch_requests.push_back(*std::move(dispatch_request));
  }
}

void ScoreAdsReactor::MayPopulateProtectedAppSignalsDispatchRequests(
    std::vector<DispatchRequest>& dispatch_requests,
    bool enable_debug_reporting,
    absl::Nullable<
        const absl::flat_hash_map<std::string, rapidjson::StringBuffer>*>
        scoring_signals,
    const std::shared_ptr<std::string>& auction_config,
    RepeatedPtrField<ProtectedAppSignalsAdWithBidMetadata>&
        protected_app_signals_ad_bids,
    RomaRequestSharedContext& shared_context) {
  PS_VLOG(8, log_context_) << __func__;
  if (require_scoring_signals_for_scoring_ && scoring_signals == nullptr) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Skipping ALL protected app signals ads since scoring signals are "
           "required but null.";
    return;
  }
  while (!protected_app_signals_ad_bids.empty()) {
    std::unique_ptr<ProtectedAppSignalsAdWithBidMetadata> pas_ad_with_bid(
        protected_app_signals_ad_bids.ReleaseLast());
    absl::string_view scoring_signals_str = kNullScoringSignalsJson;
    if (scoring_signals != nullptr) {
      auto scoring_signals_it =
          scoring_signals->find(pas_ad_with_bid->render());
      if (require_scoring_signals_for_scoring_ &&
          scoring_signals_it == scoring_signals->end()) {
        PS_VLOG(kNoisyWarn, log_context_)
            << "Skipping protected app signals ad since render "
               "URL is not found in the scoring signals: "
            << pas_ad_with_bid->render();
        continue;
      }
      if (scoring_signals_it != scoring_signals->end()) {
        // Iterators hold references to the underlying elements in their
        // collection, they do not make copies - and .GetString() is a rapidjson
        // method returning a c-style char *, so it should not be making a copy
        // either. Therefore taking a reference to this (which is what a
        // string_view is) is safe.
        scoring_signals_str = scoring_signals_it->second.GetString();
      }
    }

    auto dispatch_request = BuildScoreAdRequest(
        *pas_ad_with_bid, auction_config, scoring_signals_str,
        enable_debug_reporting, log_context_, enable_adtech_code_logging_,
        MakeBidMetadata(
            raw_request_.publisher_hostname(), pas_ad_with_bid->owner(),
            pas_ad_with_bid->render(), GetEmptyAdComponentRenderUrls(),
            raw_request_.top_level_seller(), pas_ad_with_bid->bid_currency(),
            raw_request_.seller_data_version()),
        code_version_);
    if (!dispatch_request.ok()) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Failed to create scoring request for protected app signals ad: "
          << dispatch_request.status();
      continue;
    }

    auto [unused_it, inserted] = protected_app_signals_ad_data_.emplace(
        dispatch_request->id, std::move(pas_ad_with_bid));
    if (!inserted) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "ProtectedAppSignals ScoreAd Request id conflict detected: "
          << dispatch_request->id;
      continue;
    }

    dispatch_request->metadata = shared_context;
    dispatch_request->tags[kRomaTimeoutTag] = roma_timeout_duration_;
    dispatch_requests.push_back(*std::move(dispatch_request));
  }
}

void ScoreAdsReactor::Execute() {
  if (enable_cancellation_ && context_->IsCancelled()) {
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    if (server_common::log::PS_VLOG_IS_ON(kEncrypted)) {
      PS_VLOG(kEncrypted, log_context_)
          << "Encrypted ScoreAdsRequest exported in EventMessage if consented";
      log_context_.SetEventMessageField(*request_);
    }
    PS_VLOG(kPlain, log_context_)
        << "ScoreAdsRawRequest exported in EventMessage if consented";
    log_context_.SetEventMessageField(raw_request_);
  }
  if (!raw_request_.protected_app_signals_ad_bids().empty() &&
      !enable_protected_app_signals_) {
    PS_VLOG(kNoisyWarn, log_context_) << "Found PAS bids even when feature is "
                                      << "disabled, discarding these bids";
    raw_request_.clear_protected_app_signals_ad_bids();
  }

  benchmarking_logger_->BuildInputBegin();
  std::shared_ptr<std::string> auction_config =
      BuildAuctionConfig(raw_request_);

  // Debug urls are collected from scoreAd() and PerformDebugReporting() is
  // called only when:
  // i) The server has enabled debug url generation, and
  // ii) The request has enabled debug reporting, and
  // iii) The seller is not in cooldown or lockout if downsampling is enabled.
  bool enable_debug_reporting =
      enable_seller_debug_url_generation_ &&
      raw_request_.enable_debug_reporting() &&
      !(raw_request_.fdo_flags().enable_sampled_debug_reporting() &&
        raw_request_.fdo_flags().in_cooldown_or_lockout());

  std::vector<DispatchRequest> dispatch_requests;
  RomaRequestSharedContext shared_context =
      RomaSharedContextWithMetric<ScoreAdsRequest>(
          request_, roma_request_context_factory_.Create(), log_context_);
  if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    if (raw_request_.component_auction_results_size() == 0) {
      // Internal error so that this is not propagated back to ad server.
      FinishWithStatus(::grpc::Status(grpc::StatusCode::INTERNAL,
                                      kNoValidComponentAuctions));
      return;
    }
    auto dispatch_requests_or = PopulateTopLevelAuctionDispatchRequests(
        enable_debug_reporting, auction_config,
        *raw_request_.mutable_component_auction_results(), shared_context);
    if (!dispatch_requests_or.ok()) {
      // Invalid Argument error so that this is propagated back to ad server.
      FinishWithStatus(
          ::grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                         dispatch_requests_or.status().message().data()));
      return;
    }
    dispatch_requests = *std::move(dispatch_requests_or);
  } else {
    absl::StatusOr<absl::flat_hash_map<std::string, rapidjson::StringBuffer>>
        scoring_signals = BuildTrustedScoringSignals(
            raw_request_, log_context_, require_scoring_signals_for_scoring_);

    if (require_scoring_signals_for_scoring_ && !scoring_signals.ok()) {
      PS_LOG(ERROR, log_context_) << "No scoring signals found, finishing RPC: "
                                  << scoring_signals.status();
      FinishWithStatus(server_common::FromAbslStatus(scoring_signals.status()));
      return;
    }
    const int num_protected_audience_ads = raw_request_.ad_bids_size();
    const int num_protected_app_signals_ads =
        raw_request_.protected_app_signals_ad_bids_size();
    dispatch_requests.reserve(num_protected_audience_ads +
                              num_protected_app_signals_ads);
    if (num_protected_audience_ads > 0) {
      PopulateProtectedAudienceDispatchRequests(
          dispatch_requests, enable_debug_reporting,
          (scoring_signals.ok()) ? &(*scoring_signals) : nullptr,
          auction_config, *raw_request_.mutable_ad_bids(), shared_context);
    }
    if (num_protected_app_signals_ads) {
      MayPopulateProtectedAppSignalsDispatchRequests(
          dispatch_requests, enable_debug_reporting,
          (scoring_signals.ok()) ? &(*scoring_signals) : nullptr,
          auction_config, *raw_request_.mutable_protected_app_signals_ad_bids(),
          shared_context);
    }
  }

  benchmarking_logger_->BuildInputEnd();

  if (dispatch_requests.empty()) {
    // Internal error so that this is not propagated back to ad server.
    FinishWithStatus(::grpc::Status(grpc::StatusCode::INTERNAL,
                                    kNoAdsWithValidDispatchRequests));
    return;
  }
  absl::Time start_js_execution_time = absl::Now();
  auto status = dispatcher_.BatchExecute(
      dispatch_requests,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this, start_js_execution_time, enable_debug_reporting](
              const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            int js_execution_time_ms =
                (absl::Now() - start_js_execution_time) / absl::Milliseconds(1);
            LogIfError(
                metric_context_->LogHistogram<metric::kUdfExecutionDuration>(
                    js_execution_time_ms));
            // Must be before ScoreAdsCallback so that metric context doesn't go
            // out of scope
            LogRomaMetrics(result, metric_context_.get());
            ScoreAdsCallback(result, enable_debug_reporting);
          },
          [this]() {
            FinishWithStatus(
                grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
          }));

  if (!status.ok()) {
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                       1, metric::kAuctionScoreAdsFailedToDispatchCode));
    PS_LOG(ERROR, log_context_)
        << "Execution request failed: "
        << status.ToString(absl::StatusToStringMode::kWithEverything);
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::UNKNOWN, status.ToString()));
  }
}

void ScoreAdsReactor::InitializeBuyerReportingDispatchRequestData(
    const ScoreAdsResponse::AdScore& winning_ad_score) {
  auto ad_it = raw_request_.per_buyer_signals().find(
      winning_ad_score.interest_group_owner());
  if (ad_it != raw_request_.per_buyer_signals().end()) {
    buyer_reporting_dispatch_request_data_.buyer_signals = ad_it->second;
  } else {
    PS_LOG(ERROR, log_context_) << "Found empty per_buyer_signals for winner:"
                                << winning_ad_score.interest_group_owner();
  }
  buyer_reporting_dispatch_request_data_.seller = raw_request_.seller();
  buyer_reporting_dispatch_request_data_.interest_group_name =
      winning_ad_score.interest_group_name();
  buyer_reporting_dispatch_request_data_.buyer_origin =
      winning_ad_score.interest_group_owner();
  buyer_reporting_dispatch_request_data_.made_highest_scoring_other_bid =
      post_auction_signals_.made_highest_scoring_other_bid;
}

void ScoreAdsReactor::SetReportingIdsInRawResponseAndDispatchRequestData(
    const std::unique_ptr<AdWithBidMetadata>& ad) {
  if (ad->has_buyer_reporting_id()) {
    // Set buyer_reporting_id in the response.
    // If this id doesn't match with the buyerReportingId on-device, reportWin
    // urls will be dropped.
    raw_response_.mutable_ad_score()->set_buyer_reporting_id(
        ad->buyer_reporting_id());
    buyer_reporting_dispatch_request_data_.buyer_reporting_id =
        ad->buyer_reporting_id();
  }
  if (ad->has_buyer_and_seller_reporting_id()) {
    // Set buyer_and_seller_reporting_id in the response.
    // If this id doesn't match with the buyerAndSellerReportingId on-device,
    // reportWin urls will be dropped.
    raw_response_.mutable_ad_score()->set_buyer_and_seller_reporting_id(
        ad->buyer_and_seller_reporting_id());
    buyer_reporting_dispatch_request_data_.buyer_and_seller_reporting_id =
        ad->buyer_and_seller_reporting_id();
  }
  if (ad->has_selected_buyer_and_seller_reporting_id()) {
    // Set buyer_and_seller_reporting_id in the response.
    // If this id doesn't match with the buyerAndSellerReportingId on-device,
    // reportWin urls will be dropped.
    raw_response_.mutable_ad_score()
        ->set_selected_buyer_and_seller_reporting_id(
            ad->selected_buyer_and_seller_reporting_id());
    buyer_reporting_dispatch_request_data_
        .selected_buyer_and_seller_reporting_id =
        ad->selected_buyer_and_seller_reporting_id();
  }
}

void ScoreAdsReactor::PerformReportingWithSellerAndBuyerCodeIsolation(
    const ScoreAdsResponse::AdScore& winning_ad_score, absl::string_view id) {
  reporting_dispatch_request_config_ = {
      .enable_report_win_url_generation = enable_report_win_url_generation_,
      .enable_protected_app_signals = enable_protected_app_signals_,
      .enable_report_win_input_noising = enable_report_win_input_noising_,
      .enable_adtech_code_logging = enable_adtech_code_logging_,
      .roma_timeout_duration = roma_timeout_duration_,
      .report_result_udf_version = code_version_};
  buyer_reporting_dispatch_request_data_.auction_config =
      BuildAuctionConfig(raw_request_);

  if (auto ad_it = ad_data_.find(id); ad_it != ad_data_.end()) {
    if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
      DispatchReportResultRequestForTopLevelAuction(id);
      return;
    }
    InitializeBuyerReportingDispatchRequestData(winning_ad_score);

    const auto& ad = ad_it->second;
    buyer_reporting_dispatch_request_data_.join_count = ad->join_count();
    buyer_reporting_dispatch_request_data_.recency = ad->recency();
    buyer_reporting_dispatch_request_data_.modeling_signals =
        ad->modeling_signals();
    buyer_reporting_dispatch_request_data_.ad_cost = ad->ad_cost();
    buyer_reporting_dispatch_request_data_.winning_ad_render_url = ad->render();
    buyer_reporting_dispatch_request_data_.data_version = ad->data_version();

    std::string& k_anon_rep_status =
        buyer_reporting_dispatch_request_data_.k_anon_status;
    // The default status is 'notCalculated'.
    if (enable_enforce_kanon_) {
      DCHECK(ad->k_anon_status())
          << "This ad should not be considered a winner, since it does not "
             "meet the k-anon threshold but k-anon is enabled and enforced."
             "This is a logic error / bug.";
      if (ad->k_anon_status()) {
        k_anon_rep_status = kPassedAndEnforcedKAnonStatus;
      }
    }
    SetReportingIdsInRawResponseAndDispatchRequestData(std::move(ad));

    if (IsComponentAuction(auction_scope_)) {
      DispatchReportResultRequestForComponentAuction(winning_ad_score);
    } else {
      DispatchReportResultRequest();
    }
  } else if (auto protected_app_signals_ad_it =
                 protected_app_signals_ad_data_.find(id);
             protected_app_signals_ad_it !=
             protected_app_signals_ad_data_.end()) {
    if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
      DispatchReportResultRequestForTopLevelAuction(id);
      return;
    }
    InitializeBuyerReportingDispatchRequestData(winning_ad_score);

    const auto& protected_app_signals_ad = protected_app_signals_ad_it->second;
    buyer_reporting_dispatch_request_data_.ad_cost =
        protected_app_signals_ad->ad_cost();
    buyer_reporting_dispatch_request_data_.winning_ad_render_url =
        protected_app_signals_ad->render();
    buyer_reporting_dispatch_request_data_.egress_payload =
        protected_app_signals_ad->egress_payload();
    buyer_reporting_dispatch_request_data_.temporary_unlimited_egress_payload =
        protected_app_signals_ad->temporary_unlimited_egress_payload();
    if (IsComponentAuction(auction_scope_)) {
      DispatchReportResultRequestForComponentAuction(winning_ad_score);
    } else {
      DispatchReportResultRequest();
    }
  } else {
    PS_LOG(ERROR, log_context_)
        << "Following id didn't map to any ProtectedAudience or "
           "ProtectedAppSignals Ad: "
        << id;
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return;
  }
}

void ScoreAdsReactor::PerformPrivateAggregationReporting(
    absl::string_view most_desirable_ad_score_id) {
  PrivateAggregationHandlerMetadata metadata;
  metadata.most_desirable_ad_score_id = most_desirable_ad_score_id;
  BaseValues base_values =
      GetPaggBaseValueWithoutRejectReason(post_auction_signals_);
  for (auto& [ad_id, contribution_obj_doc] : paapi_contributions_docs_) {
    if (const auto ad_it = ad_data_.find(ad_id); ad_it != ad_data_.end()) {
      // Reset the optional value in base_values.reject_reason
      // since base_values is initialized outside the loop and
      // we need to re-initiliaze the reject_reason for this Ad.
      base_values.reject_reason.reset();
      absl::string_view ig_owner = ad_it->second->interest_group_owner();
      absl::string_view ig_name = ad_it->second->interest_group_name();

      // Get rejection reason from rejection_reason_map, which is keyed on
      // interest_group_owner and interest_group_name.
      if (const auto& ig_owner_it =
              post_auction_signals_.rejection_reason_map.find(ig_owner);
          ig_owner_it != post_auction_signals_.rejection_reason_map.end()) {
        if (const auto& ig_name_it = ig_owner_it->second.find(ig_name);
            ig_name_it != ig_owner_it->second.end()) {
          base_values.reject_reason = ig_name_it->second;
        }
      } else if (!ghost_winner_interest_group_data_.empty() &&
                 ghost_winner_interest_group_data_.find(ig_owner) !=
                     ghost_winner_interest_group_data_.end()) {
        if (ghost_winner_interest_group_data_.at(ig_owner) == ig_name) {
          continue;
        }
      }
      metadata.ad_id = ad_id;
      metadata.base_values = base_values;
      metadata.seller_origin = raw_request_.seller();
      HandlePrivateAggregationReporting(metadata, contribution_obj_doc,
                                        *raw_response_.mutable_ad_score());
    }
  }
}

void ScoreAdsReactor::PerformReporting(
    const ScoreAdsResponse::AdScore& winning_ad_score, absl::string_view id) {
  if (auto ad_it = ad_data_.find(id); ad_it != ad_data_.end()) {
    const auto& ad = ad_it->second;
    BuyerReportingMetadata buyer_reporting_metadata;
    if (enable_report_win_url_generation_) {
      buyer_reporting_metadata = {
          .buyer_signals = raw_request_.per_buyer_signals().at(
              winning_ad_score.interest_group_owner()),
          .join_count = ad->join_count(),
          .recency = ad->recency(),
          .modeling_signals = ad->modeling_signals(),
          .seller = raw_request_.seller(),
          .interest_group_name = winning_ad_score.interest_group_name(),
          .ad_cost = ad->ad_cost(),
          .data_version = ad->data_version()};
      if (!ad->buyer_reporting_id().empty()) {
        raw_response_.mutable_ad_score()->set_buyer_reporting_id(
            ad->buyer_reporting_id());
        buyer_reporting_metadata.buyer_reporting_id = ad->buyer_reporting_id();
      }
    }
    DispatchReportingRequestForPA(id, winning_ad_score,
                                  BuildAuctionConfig(raw_request_),
                                  buyer_reporting_metadata);

  } else if (auto protected_app_signals_ad_it =
                 protected_app_signals_ad_data_.find(id);
             protected_app_signals_ad_it !=
             protected_app_signals_ad_data_.end()) {
    const auto& ad = protected_app_signals_ad_it->second;
    BuyerReportingMetadata buyer_reporting_metadata;
    if (enable_report_win_url_generation_) {
      buyer_reporting_metadata = {
          .buyer_signals = raw_request_.per_buyer_signals().at(
              winning_ad_score.interest_group_owner()),
          .seller = raw_request_.seller(),
          .interest_group_name = winning_ad_score.interest_group_name(),
          .ad_cost = ad->ad_cost()};
    }
    DispatchReportingRequestForPAS(
        winning_ad_score, BuildAuctionConfig(raw_request_),
        buyer_reporting_metadata, ad->egress_payload(),
        ad->temporary_unlimited_egress_payload());

  } else {
    PS_LOG(ERROR, log_context_)
        << "Following id didn't map to any ProtectedAudience or "
           "ProtectedAppSignals Ad: "
        << id;
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return;
  }
}

ScoreAdsReactor::OptionalAdRejectionReason
ScoreAdsReactor::GetAdRejectionReason(
    const rapidjson::Document& response_json,
    const ScoreAdsResponse::AdScore& ad_score) {
  absl::string_view interest_group_owner = ad_score.interest_group_owner();
  absl::string_view interest_group_name = ad_score.interest_group_name();
  absl::string_view ad_with_bid_currency = ad_score.buyer_bid_currency();
  absl::string_view bid_currency = ad_score.bid_currency();
  float bid = ad_score.bid();
  // Get ad rejection reason before updating the scoring data.
  std::optional<ScoreAdsResponse::AdScore::AdRejectionReason>
      ad_rejection_reason;
  if (!response_json.IsNumber()) {
    // Parse Ad rejection reason and store only if it has value.
    ad_rejection_reason = ParseAdRejectionReason(
        response_json, interest_group_owner, interest_group_name, log_context_);
  }

  if (IsBidCurrencyMismatched(auction_scope_, raw_request_.seller_currency(),
                              bid, bid_currency)) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Skipping component bid as it does not match seller_currency: "
        << interest_group_name << ": " << ad_score.DebugString();
    return BuildAdRejectionReason(
        interest_group_owner, interest_group_name,
        SellerRejectionReason::BID_FROM_SCORE_AD_FAILED_CURRENCY_CHECK);
  }

  if (IsIncomingBidInSellerCurrencyIllegallyModified(
          raw_request_.seller_currency(), ad_with_bid_currency,
          ad_score.incoming_bid_in_seller_currency(), ad_score.buyer_bid())) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Skipping ad_score as its incomingBidInSellerCurrency was "
        << "modified when it should not have been: " << interest_group_name
        << ": " << ad_score.DebugString();
    return BuildAdRejectionReason(interest_group_owner, interest_group_name,
                                  SellerRejectionReason::INVALID_BID);
  }

  // Populate a default rejection reason if needed when we didn't get a positive
  // desirability.
  if (ad_score.desirability() <= 0) {
    PS_VLOG(5, log_context_)
        << "Non-positive desirability for ad and no rejection reason "
           "populated by seller, providing a default rejection reason";
    return BuildAdRejectionReason(
        interest_group_owner, interest_group_name,
        SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  }

  return ad_rejection_reason;
}

void ScoreAdsReactor::FindScoredAdType(
    absl::string_view response_id, AdWithBidMetadata** ad_with_bid_metadata,
    ProtectedAppSignalsAdWithBidMetadata**
        protected_app_signals_ad_with_bid_metadata) {
  if (auto ad_it = ad_data_.find(response_id); ad_it != ad_data_.end()) {
    *ad_with_bid_metadata = ad_it->second.get();
  } else if (auto protected_app_signals_ad_it =
                 protected_app_signals_ad_data_.find(response_id);
             protected_app_signals_ad_it !=
             protected_app_signals_ad_data_.end()) {
    *protected_app_signals_ad_with_bid_metadata =
        protected_app_signals_ad_it->second.get();
  }
}

void ScoreAdsReactor::MayAddKAnonJoinCandidate(
    absl::string_view request_id, ScoreAdsResponse::AdScore& ad_score) {
  PS_VLOG(5, log_context_) << __func__ << ": request_id: " << request_id;
  if (auction_scope_ != AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    PS_VLOG(5, log_context_) << __func__ << ": Not a top-level auction, "
                             << "skipping k-anon join candidate population";
    return;
  }

  auto it = ad_k_anon_join_cand_.find(request_id);
  if (it == ad_k_anon_join_cand_.end()) {
    PS_VLOG(5, log_context_)
        << __func__ << ": Did not find request_id: " << request_id
        << ", in k-anon join candidates";
    return;
  }

  PS_VLOG(5, log_context_) << __func__
                           << ": Found k-anon join candidate for request_id: "
                           << request_id << ":\n"
                           << it->second.DebugString();
  *ad_score.mutable_k_anon_join_candidate() = std::move(it->second);
}

std::vector<ScoredAdData> ScoreAdsReactor::CollectValidRomaResponses(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  std::vector<ScoredAdData> valid_scored_ads;
  valid_scored_ads.reserve(responses.size());
  for (int index = 0; index < responses.size(); ++index) {
    const auto& response = responses[index];
    if (!response.ok()) {
      PS_LOG(WARNING, log_context_)
          << "Invalid execution (possibly invalid input): "
          << response.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
      LogIfError(
          metric_context_->AccumulateMetric<metric::kUdfExecutionErrorCount>(
              1));
      continue;
    }

    // Determine what type of ad was scored in this response.
    AdWithBidMetadata* protected_audience_ad_with_bid = nullptr;
    ProtectedAppSignalsAdWithBidMetadata* protected_app_signals_ad_with_bid =
        nullptr;
    FindScoredAdType(response->id, &protected_audience_ad_with_bid,
                     &protected_app_signals_ad_with_bid);
    if (!protected_audience_ad_with_bid && !protected_app_signals_ad_with_bid) {
      // This should never happen but we log here in case there is a bug in
      // our implementation.
      PS_LOG(ERROR, log_context_)
          << "Scored ad is neither a protected audience ad, nor a "
             "protected app "
             "signals ad: "
          << response->resp;
      continue;
    }
    absl::StatusOr<rapidjson::Document> score_ads_wrapper_response =
        ParseJsonString(response->resp);
    if (!score_ads_wrapper_response.ok()) {
      LogWarningForBadResponse(score_ads_wrapper_response.status(), *response,
                               protected_audience_ad_with_bid, log_context_);
      LogIfError(
          metric_context_->AccumulateMetric<metric::kUdfExecutionErrorCount>(
              1));
      continue;
    }
    absl::StatusOr<rapidjson::Document> response_json =
        ParseAndGetScoreAdResponseJson(enable_adtech_code_logging_,
                                       log_context_,
                                       *score_ads_wrapper_response);

    if (!response_json.ok()) {
      LogWarningForBadResponse(response_json.status(), *response,
                               protected_audience_ad_with_bid, log_context_);
      LogIfError(
          metric_context_->AccumulateMetric<metric::kUdfExecutionErrorCount>(
              1));
      continue;
    }

    auto ad_score = ScoreAdResponseJsonToProto(
        *response_json, max_allowed_size_debug_url_chars_,
        IsComponentAuction(auction_scope_));

    if (!ad_score.ok()) {
      LogWarningForBadResponse(ad_score.status(), *response,
                               protected_audience_ad_with_bid, log_context_);
      continue;
    }

    MayAddKAnonJoinCandidate(response->id, *ad_score);

    if (enable_private_aggregate_reporting_) {
      rapidjson::Document paapi_response_obj;
      auto iterator =
          score_ads_wrapper_response->FindMember("paapiContributions");
      if (iterator != score_ads_wrapper_response->MemberEnd() &&
          iterator->value.IsObject()) {
        paapi_response_obj.CopyFrom(iterator->value,
                                    paapi_response_obj.GetAllocator());
        paapi_contributions_docs_.emplace(response->id,
                                          std::move(paapi_response_obj));
      }
    }

    ScoredAdData scored_ad_data = {
        .ad_score = *std::move(ad_score),
        .response_json = *std::move(response_json),
        .protected_audience_ad_with_bid = protected_audience_ad_with_bid,
        .protected_app_signals_ad_with_bid = protected_app_signals_ad_with_bid,
        .id = response->id};
    ScoringAdWithBidMetadata scoring_ad_with_bid_metadata =
        PopulateAdScoreData(scored_ad_data);
    // If k-anon is not enabled or enforced, we consider the ad k-anonymous
    // by default.
    scored_ad_data.k_anon_status =
        !enable_enforce_kanon_ || scoring_ad_with_bid_metadata.k_anon_status;
    valid_scored_ads.push_back(std::move(scored_ad_data));
  }

  return valid_scored_ads;
}

void ScoredAdData::Swap(ScoredAdData& other) {
  ad_score.Swap(&other.ad_score);
  response_json.Swap(other.response_json);
  id.swap(other.id);

  auto* tmp_protected_audience_ad_with_bid =
      other.protected_audience_ad_with_bid;
  other.protected_audience_ad_with_bid = protected_audience_ad_with_bid;
  protected_audience_ad_with_bid = tmp_protected_audience_ad_with_bid;

  std::swap(protected_app_signals_ad_with_bid,
            other.protected_app_signals_ad_with_bid);
  std::swap(k_anon_status, other.k_anon_status);
}

bool ScoredAdData::operator>(const ScoredAdData& other) const {
  const auto& other_ad_score = other.ad_score;
  if (ad_score.desirability() > other_ad_score.desirability()) {
    return true;
  }

  if (ad_score.desirability() == other_ad_score.desirability()) {
    if (ad_score.buyer_bid() > other_ad_score.buyer_bid()) {
      return true;
    }

    if (ad_score.buyer_bid() == other_ad_score.buyer_bid()) {
      if (k_anon_status && !other.k_anon_status) {
        return true;
      }

      return false;
    }

    return false;
  }

  return false;
}

ScoringData ScoreAdsReactor::FindWinningAd(
    std::vector<ScoredAdData>& parsed_ads) {
  ScoringData scoring_data;

  int index = 0;
  int end = parsed_ads.size();
  PS_VLOG(kDispatch, log_context_)
      << "Finding winner in " << end << " parsed ads.";
  while (index < end) {
    auto& parsed_ad = parsed_ads[index];

    // Used for debug reporting.
    // Should include bids rejected by bid currency mismatch
    // and those not allowed in component auctions.
    auto& ad_score = parsed_ad.ad_score;
    ad_scores_.emplace(parsed_ad.id,
                       std::make_unique<ScoreAdsResponse::AdScore>(ad_score));
    if (CheckAndUpdateModifiedBid(auction_scope_, ad_score.buyer_bid(),
                                  ad_score.buyer_bid_currency(), &ad_score)) {
      PS_VLOG(kNoisyInfo, log_context_)
          << "Setting modified bid value to original buyer bid value (and "
             "currency) as the "
             "modified bid is not set. For interest group: "
          << ad_score.interest_group_name() << ": " << ad_score.DebugString();
    }
    OptionalAdRejectionReason ad_rejection_reason =
        GetAdRejectionReason(parsed_ad.response_json, ad_score);
    if (ad_rejection_reason &&
        ad_rejection_reason->rejection_reason() !=
            SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE) {
      scoring_data.seller_rejected_bid_count += 1;
      LogIfError(
          metric_context_->AccumulateMetric<metric::kAuctionBidRejectedCount>(
              1, ToSellerRejectionReasonString(
                     ad_rejection_reason->rejection_reason())));
      PS_VLOG(kNoisyInfo, log_context_)
          << "Skipping bid with rejection reason "
          << ad_rejection_reason->rejection_reason();
      *scoring_data.ad_rejection_reasons.Add() =
          *std::move(ad_rejection_reason);
      // Move the rejected ad towards the end of the vector.
      // Don't enhance index becuase we can have a potentially valid ad on this
      // index after swap.
      --end;
      parsed_ads[end].Swap(parsed_ads[index]);
      continue;
    }
    // Filter out component ads if not allowed.
    if (auction_scope_ ==
            AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER &&
        !ad_score.allow_component_auction()) {
      // Ignore component level ads if it is not allowed to
      // participate in the top level auction.
      LogIfError(
          metric_context_->AccumulateMetric<metric::kAuctionBidRejectedCount>(
              1, metric::kSellerComponentAuctionNotAllowed));
      PS_VLOG(kNoisyInfo, log_context_)
          << "Skipping component bid as it is not allowed for "
          << "owner: " << ad_score.interest_group_owner()
          << ", render: " << ad_score.render();
      --end;
      parsed_ads[end].Swap(parsed_ads[index]);
      continue;
    }
    ++index;
  }

  // Drop all the rejected bids from the vector.
  parsed_ads.resize(end);
  PS_VLOG(kStats, log_context_)
      << "Number of valid positive score ads: " << parsed_ads.size();

  // Sorts based on score/desirability, bid and k-anon status.
  std::sort(parsed_ads.begin(), parsed_ads.end(), std::greater<>());

  for (int ind = 0; ind < parsed_ads.size(); ++ind) {
    ScoredAdData& parsed_ad = parsed_ads[ind];
    ScoreAdsResponse::AdScore& ad_score = parsed_ad.ad_score;
    auto& winner_indices = scoring_data.winner_cand_indices;
    auto& ghost_winner_indices = scoring_data.ghost_winner_cand_indices;
    PS_VLOG(kStats, log_context_)
        << "Parsed ad score at index: " << ind
        << " (in order) k-anon status: " << parsed_ad.k_anon_status << ", "
        << ad_score.DebugString()
        << ", winner_cand_indices: " << winner_indices.size()
        << ", ghost_winner_cand_indices: " << ghost_winner_indices.size();
    // Consider only ads that are not explicitly rejected and the ones that have
    // a positive desirability score.
    if (ad_score.desirability() <= 0) {
      continue;
    }

    if (parsed_ad.k_anon_status) {
      // Winner is chosen randomly from top-scoring ads that are k-anonymous.
      AddIfCandsEmptyOrHasEqLastDesirability(ind, ad_score.desirability(),
                                             parsed_ads, winner_indices);
    } else if (winner_indices.empty()) {
      ad_score.clear_debug_report_urls();
      // Any ads that have higher scores than first k-anon ad are ghost winner
      // candidates.
      AddIfCandsEmptyOrHasEqLastDesirability(ind, ad_score.desirability(),
                                             parsed_ads, ghost_winner_indices);
    }
  }

  const auto& ghost_winner_cand_indices =
      scoring_data.ghost_winner_cand_indices;
  PS_VLOG(kStats, log_context_)
      << "Num winner candidates: " << scoring_data.winner_cand_indices.size()
      << ", num ghost winner candidates: " << ghost_winner_cand_indices.size();
  // TODO(b/372097452): If we end up using num_allowd_ghost_winners > 1, then we
  // should revise the random selection to prefer high scoring ghost winning ads
  // over low scoring ghost winning ads.
  scoring_data.ChooseWinnerAndGhostWinners(
      raw_request_.num_allowed_ghost_winners());

  absl::flat_hash_set<int> ghost_winner_cands(ghost_winner_cand_indices.begin(),
                                              ghost_winner_cand_indices.end());
  for (int ind = 0; ind < parsed_ads.size(); ++ind) {
    const ScoredAdData& parsed_ad = parsed_ads[ind];
    const ScoreAdsResponse::AdScore& ad_score = parsed_ad.ad_score;
    // Consider scored ad as valid (i.e. not rejected) when it has a
    // positive desirability and either:
    // 1. scoreAd returned a number.
    // 2. scoreAd returned an object but the reject reason was not
    // populated.
    // 3. scoreAd returned an object and the reject reason was explicitly
    // set to "not-available".
    // 4. Ad under consideration is not one of k-anon ghost winners.
    // Only consider valid bids for populating other highest bids.
    if (!ghost_winner_cands.contains(ind)) {
      scoring_data.score_ad_map[ad_score.desirability()].push_back(ind);
    }
  }
  return scoring_data;
}

void ScoreAdsReactor::PopulateRelevantFieldsInResponse(
    int index, std::vector<ScoredAdData>& parsed_ads) {
  auto& parsed_ad = parsed_ads[index];
  absl::string_view request_id = parsed_ad.id;
  AdWithBidMetadata* ad = nullptr;
  ProtectedAppSignalsAdWithBidMetadata* protected_app_signals_ad_with_bid =
      nullptr;
  FindScoredAdType(request_id, &ad, &protected_app_signals_ad_with_bid);
  // Note: Before the call flow gets here, we would have already verified the
  // winning ad type is one of the expected types and hence we only do a DCHECK
  // here.
  DCHECK(ad || protected_app_signals_ad_with_bid);

  auto& ad_score = parsed_ad.ad_score;
  if (ad) {
    ad_score.set_render(ad->render());
    ad_score.mutable_component_renders()->Swap(ad->mutable_ad_components());
  } else {
    ad_score.set_render(protected_app_signals_ad_with_bid->render());
  }

  if (auction_scope_ == AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    if (auto it = component_ad_seller_.find(request_id);
        it != component_ad_seller_.end()) {
      ad_score.set_seller(it->second);
    }
  }
}

void ScoreAdsReactor::PopulateHighestScoringOtherBidsData(
    int index_of_most_desirable_ad_score,
    const absl::flat_hash_map<float, std::list<int>>& score_ad_map,
    const std::vector<ScoredAdData>& responses,
    ScoreAdsResponse::AdScore& winning_ad_score) {
  if (auction_scope_ == AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    return;
  }
  std::vector<float> scores_list;
  scores_list.reserve(score_ad_map.size());
  // Logic to calculate the list of highest scoring other bids and
  // corresponding IG owners.
  for (const auto& [score, unused_ad_indices] : score_ad_map) {
    scores_list.push_back(score);
  }

  // Sort the scores in descending order.
  std::sort(scores_list.begin(), scores_list.end(),
            [](int a, int b) { return a > b; });

  // Add all the bids with the top 2 scores (excluding the winner and bids
  // with 0 score) and corresponding interest group owners to
  // ig_owner_highest_scoring_other_bids_map.
  for (int i = 0; i < 2 && i < scores_list.size(); i++) {
    if (scores_list.at(i) == 0) {
      break;
    }

    for (int current_index : score_ad_map.at(scores_list.at(i))) {
      if (index_of_most_desirable_ad_score == current_index) {
        continue;
      }

      AdWithBidMetadata* ad_with_bid_metadata_from_buyer = nullptr;
      ProtectedAppSignalsAdWithBidMetadata* protected_app_signals_ad_with_bid =
          nullptr;
      FindScoredAdType(responses[current_index].id,
                       &ad_with_bid_metadata_from_buyer,
                       &protected_app_signals_ad_with_bid);
      DCHECK(ad_with_bid_metadata_from_buyer ||
             protected_app_signals_ad_with_bid);
      auto* highest_scoring_other_bids_map =
          winning_ad_score.mutable_ig_owner_highest_scoring_other_bids_map();

      std::string owner;
      float bid = 0.0;
      if (ad_with_bid_metadata_from_buyer != nullptr) {
        bid = ad_with_bid_metadata_from_buyer->bid();
        owner = ad_with_bid_metadata_from_buyer->interest_group_owner();
      } else {
        bid = protected_app_signals_ad_with_bid->bid();
        owner = protected_app_signals_ad_with_bid->owner();
      }
      if (!raw_request_.seller_currency().empty()) {
        auto ad_score_it = ad_scores_.find(responses[current_index].id);
        if (ad_score_it != ad_scores_.end()) {
          bid = ad_score_it->second->incoming_bid_in_seller_currency();
        }
      }
      UpdateHighestScoringOtherBidMap(bid, owner,
                                      *highest_scoring_other_bids_map);
    }
  }
}

void ScoreAdsReactor::AddGhostWinnersToResponse(
    const std::vector<int>& ghost_winner_indices,
    std::vector<ScoredAdData>& parsed_responses) {
  for (int ghost_winner_ind : ghost_winner_indices) {
    PopulateRelevantFieldsInResponse(ghost_winner_ind, parsed_responses);
    auto& ghost_winner = parsed_responses[ghost_winner_ind].ad_score;
    ghost_winner_interest_group_data_.emplace(
        ghost_winner.interest_group_owner(),
        ghost_winner.interest_group_name());
    *raw_response_.mutable_ghost_winning_ad_scores()->Add() =
        std::move(ghost_winner);
  }
}

void ScoreAdsReactor::SetPostAuctionSignals() {
  if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    post_auction_signals_ =
        GeneratePostAuctionSignalsForTopLevelSeller(raw_response_.ad_score());
  } else {
    post_auction_signals_ = GeneratePostAuctionSignals(
        raw_response_.ad_score(), raw_request_.seller_currency(),
        raw_request_.seller_data_version());
  }
}

void ScoreAdsReactor::AddWinnerToResponse(
    int winner_index, ScoringData& scoring_data,
    std::vector<ScoredAdData>& parsed_responses) {
  auto& winning_ad = parsed_responses[winner_index].ad_score;
  // Set the render URL in overall response for the winning ad.
  PopulateRelevantFieldsInResponse(winner_index, parsed_responses);
  // TODO: Check if this needs an adjustment based on k-anon status.
  PopulateHighestScoringOtherBidsData(winner_index, scoring_data.score_ad_map,
                                      parsed_responses, winning_ad);
  *winning_ad.mutable_ad_rejection_reasons() =
      std::move(scoring_data.ad_rejection_reasons);
  *raw_response_.mutable_ad_score() = std::move(winning_ad);
  SetPostAuctionSignals();
}

void ScoreAdsReactor::DoWinAndDebugReporting(
    bool enable_debug_reporting, int winner_index,
    const std::vector<ScoredAdData>& parsed_responses, absl::string_view id) {
  const ScoreAdsResponse::AdScore& winning_ad = raw_response_.ad_score();

  if (enable_private_aggregate_reporting_) {
    PerformPrivateAggregationReporting(id);
  }

  if (enable_debug_reporting) {
    PerformDebugReporting();
  }

  absl::string_view winner_id = parsed_responses[winner_index].id;
  if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    PopulateComponentReportingUrlsInTopLevelResponse(
        winner_id, raw_response_, component_level_reporting_data_);
  }

  if (!enable_report_result_url_generation_) {
    EncryptAndFinishOK();
    return;
  }

  // Todo(b/357146634): Execute reportWin even if
  // enable_report_result_url_generation is disabled or reportResult execution
  // fails.
  if (enable_seller_and_buyer_code_isolation_) {
    PerformReportingWithSellerAndBuyerCodeIsolation(winning_ad, winner_id);
    return;
  }

  PerformReporting(winning_ad, winner_id);
}

// Handles the output of the code execution dispatch.
// Note that the dispatch response value is expected to be a json string
// conforming to the scoreAd function output described here:
// https://github.com/WICG/turtledove/blob/main/FLEDGE.md#23-scoring-bids
void ScoreAdsReactor::ScoreAdsCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses,
    bool enable_debug_reporting) {
  MayVlogRomaResponses(responses, log_context_);
  benchmarking_logger_->HandleResponseBegin();
  int total_bid_count = static_cast<int>(responses.size());
  LogIfError(metric_context_->AccumulateMetric<metric::kAuctionTotalBidsCount>(
      total_bid_count));
  auto parsed_responses = CollectValidRomaResponses(responses);
  ScoringData scoring_data = FindWinningAd(parsed_responses);
  LogIfError(metric_context_->LogHistogram<metric::kAuctionBidRejectedPercent>(
      (static_cast<double>(scoring_data.seller_rejected_bid_count)) /
      total_bid_count));

  // Add ghost winners to the response (if any).
  AddGhostWinnersToResponse(scoring_data.ghost_winner_indices,
                            parsed_responses);

  const int winner_index = scoring_data.winner_index;
  const bool has_winning_ad = winner_index != -1;
  // No ad won.
  if (!has_winning_ad) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "No ad was selected as most desirable and no ghost winners found";
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                       1, metric::kAuctionScoreAdsNoAdSelected));
    if (enable_debug_reporting) {
      PerformDebugReporting();
    }
    EncryptAndFinishOK();
    return;
  }
  std::string id = responses[winner_index]->id;
  winning_ad_dispatch_id_ = responses[winner_index]->id;
  AddWinnerToResponse(winner_index, scoring_data, parsed_responses);
  DoWinAndDebugReporting(enable_debug_reporting, winner_index, parsed_responses,
                         id);
}

void ScoreAdsReactor::ReportingCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  if (PS_VLOG_IS_ON(kDispatch)) {
    for (const auto& dispatch_response : responses) {
      PS_VLOG(kDispatch, log_context_)
          << "Reporting V8 Response: " << dispatch_response.status();
      if (dispatch_response.ok()) {
        PS_VLOG(kDispatch, log_context_) << dispatch_response.value().resp;
      }
    }
  }
  for (const auto& response : responses) {
    if (response.ok()) {
      absl::StatusOr<ReportingResponse> reporting_response =
          ParseAndGetReportingResponse(enable_adtech_code_logging_,
                                       response.value().resp);
      if (!reporting_response.ok()) {
        PS_LOG(ERROR, log_context_) << "Failed to parse response from Roma ",
            reporting_response.status().ToString(
                absl::StatusToStringMode::kWithEverything);
        continue;
      }

      auto LogUdf = [this](const std::vector<std::string>& adtech_logs,
                           absl::string_view prefix) {
        if (!enable_adtech_code_logging_) {
          return;
        }
        if (!AllowAnyUdfLogging(log_context_)) {
          return;
        }
        for (const std::string& log : adtech_logs) {
          std::string log_str =
              absl::StrCat(prefix, " execution script: ", log);
          PS_VLOG(kUdfLog, log_context_) << log_str;
          log_context_.SetEventMessageField(log_str);
        }
      };
      LogUdf(reporting_response->seller_logs, "Log from Seller's");
      LogUdf(reporting_response->seller_error_logs, "Error Log from Seller's");
      LogUdf(reporting_response->seller_warning_logs,
             "Warning Log from Seller's");
      LogUdf(reporting_response->buyer_logs, "Log from Buyer's");
      LogUdf(reporting_response->buyer_error_logs, "Error Log from Buyer's");
      LogUdf(reporting_response->buyer_warning_logs,
             "Warning Log from Buyer's");

      // For component auctions, the reporting urls for seller are set in
      // the component_seller_reporting_urls field. For single seller
      // auctions and top level auctions, the reporting urls for the top level
      // seller are set in the top_level_seller_reporting_urls field. For top
      // level auctions, the component_seller_reporting_urls and
      // buyer_reporting_urls are set based on the urls obtained from the
      // component auction whose buyer won the top level auction.
      if (auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER) {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_component_seller_reporting_urls()
            ->set_reporting_url(
                reporting_response->report_result_response.report_result_url);
        for (const auto& [event, interactionReportingUrl] :
             reporting_response->report_result_response
                 .interaction_reporting_urls) {
          raw_response_.mutable_ad_score()
              ->mutable_win_reporting_urls()
              ->mutable_component_seller_reporting_urls()
              ->mutable_interaction_reporting_urls()
              ->try_emplace(event, interactionReportingUrl);
        }

      } else if (auction_scope_ ==
                 AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_top_level_seller_reporting_urls()
            ->set_reporting_url(
                reporting_response->report_result_response.report_result_url);
        for (const auto& [event, interactionReportingUrl] :
             reporting_response->report_result_response
                 .interaction_reporting_urls) {
          raw_response_.mutable_ad_score()
              ->mutable_win_reporting_urls()
              ->mutable_top_level_seller_reporting_urls()
              ->mutable_interaction_reporting_urls()
              ->try_emplace(event, interactionReportingUrl);
        }
      } else {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_top_level_seller_reporting_urls()
            ->set_reporting_url(
                reporting_response->report_result_response.report_result_url);
        for (const auto& [event, interactionReportingUrl] :
             reporting_response->report_result_response
                 .interaction_reporting_urls) {
          raw_response_.mutable_ad_score()
              ->mutable_win_reporting_urls()
              ->mutable_top_level_seller_reporting_urls()
              ->mutable_interaction_reporting_urls()
              ->try_emplace(event, interactionReportingUrl);
        }
      }
      if (enable_report_win_url_generation_) {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_buyer_reporting_urls()
            ->set_reporting_url(
                reporting_response->report_win_response.report_win_url);

        for (const auto& [event, interactionReportingUrl] :
             reporting_response->report_win_response
                 .interaction_reporting_urls) {
          raw_response_.mutable_ad_score()
              ->mutable_win_reporting_urls()
              ->mutable_buyer_reporting_urls()
              ->mutable_interaction_reporting_urls()
              ->try_emplace(event, interactionReportingUrl);
        }
      }
    } else {
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                         1, metric::kAuctionScoreAdsDispatchResponseError));
      PS_LOG(WARNING, log_context_)
          << "Invalid execution (possibly invalid input): "
          << response.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    }
  }
  EncryptAndFinishOK();
}

void ScoreAdsReactor::ReportWinCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  for (const auto& response : responses) {
    if (!response.ok()) {
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                         1, metric::kAuctionScoreAdsDispatchResponseError));
      PS_VLOG(kDispatch, log_context_)
          << "Error executing reportWin udf:"
          << response.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
      continue;
    }
    BaseValues base_values =
        GetPaggBaseValueWithoutRejectReason(post_auction_signals_);
    PS_VLOG(kDispatch, log_context_)
        << "Successfully executed reportWin udf. Response:"
        << response.value().resp;
    absl::StatusOr<ReportWinResponse> report_win_response =
        ParseReportWinResponse(reporting_dispatch_request_config_,
                               response.value().resp, base_values,
                               log_context_);
    if (!report_win_response.ok()) {
      PS_LOG(ERROR, log_context_)
          << "Failed to parse reportWin response from Roma ",
          report_win_response.status().ToString(
              absl::StatusToStringMode::kWithEverything);
      continue;
    }
    raw_response_.mutable_ad_score()
        ->mutable_win_reporting_urls()
        ->mutable_buyer_reporting_urls()
        ->set_reporting_url(report_win_response->report_win_url);
    for (const auto& [event, interactionReportingUrl] :
         report_win_response->interaction_reporting_urls) {
      raw_response_.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_buyer_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, interactionReportingUrl);
    }
    SetPrivateAggregationContributionsInResponseForBuyer(
        std::move(report_win_response->pagg_response));
  }
  EncryptAndFinishOK();
}

void ScoreAdsReactor::CancellableReportResultCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  for (const auto& response : responses) {
    if (!response.ok()) {
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                         1, metric::kAuctionScoreAdsDispatchResponseError));
      PS_VLOG(kDispatch, log_context_)
          << "Error executing reportResult udf:"
          << response.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    } else {  // Parse ReportResultResponse and set it in scoreAd response.
      BaseValues base_values =
          GetPaggBaseValueWithoutRejectReason(post_auction_signals_);
      absl::StatusOr<ReportResultResponse> report_result_response =
          ParseReportResultResponse(reporting_dispatch_request_config_,
                                    response.value().resp, base_values,
                                    log_context_);
      if (!report_result_response.ok()) {
        PS_LOG(ERROR, log_context_)
            << "Failed to parse report result response from Roma ",
            report_result_response.status().ToString(
                absl::StatusToStringMode::kWithEverything);
      } else {
        buyer_reporting_dispatch_request_data_.signals_for_winner =
            report_result_response->signals_for_winner;
        if (IsComponentAuction(auction_scope_)) {
          SetSellerReportingUrlsInResponseForComponentAuction(
              std::move(*report_result_response), raw_response_);
        } else {
          SetPrivateAggregationContributionsInResponseForSeller(
              std::move(report_result_response->pagg_response));
          SetSellerReportingUrlsInResponse(std::move(*report_result_response),
                                           raw_response_);
        }
      }
    }
    // Execute reportWin()
    if (!enable_report_win_url_generation_) {
      EncryptAndFinishOK();
      return;
    }
    absl::Status report_win_status;
    if (!buyer_reporting_dispatch_request_data_.egress_payload) {
      // No reportWin call should be dispatched if no UDF endpoint was
      // configured.
      if (!buyers_with_report_win_enabled_.contains(
              buyer_reporting_dispatch_request_data_.buyer_origin)) {
        EncryptAndFinishOK();
        return;
      }
      absl::Time start_report_win_time = absl::Now();
      report_win_status = PerformPAReportWin(
          reporting_dispatch_request_config_,
          buyer_reporting_dispatch_request_data_, seller_device_signals_,
          [this, start_report_win_time](
              const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            int report_win_execution_time_ms =
                (absl::Now() - start_report_win_time) / absl::Milliseconds(1);
            LogIfError(metric_context_
                           ->LogHistogram<metric::kReportWinExecutionDuration>(
                               report_win_execution_time_ms));
            ReportWinCallback(result);
          },
          dispatcher_);
    } else {
      // No reportWin call should be dispatched if no UDF endpoint was
      // configured.
      if (!protected_app_signals_buyers_with_report_win_enabled_.contains(
              buyer_reporting_dispatch_request_data_.buyer_origin)) {
        EncryptAndFinishOK();
        return;
      }
      absl::Time start_report_win_time = absl::Now();
      report_win_status = PerformPASReportWin(
          reporting_dispatch_request_config_,
          buyer_reporting_dispatch_request_data_, seller_device_signals_,
          [this, start_report_win_time](
              const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            int report_win_execution_time_ms =
                (absl::Now() - start_report_win_time) / absl::Milliseconds(1);
            LogIfError(metric_context_
                           ->LogHistogram<metric::kReportWinExecutionDuration>(
                               report_win_execution_time_ms));
            ReportWinCallback(result);
          },
          dispatcher_);
    }
    if (!report_win_status.ok()) {
      PS_VLOG(kDispatch, log_context_)
          << "Execution failed for reportWin: " << report_win_status.message();
      EncryptAndFinishOK();
      return;
    }
    return;
  }
  EncryptAndFinishOK();
}

void ScoreAdsReactor::SetPrivateAggregationContributionsInResponseForSeller(
    PrivateAggregateReportingResponse pagg_response) {
  if (!pagg_response.contributions().empty()) {
    pagg_response.set_adtech_origin(raw_request_.seller());
    raw_response_.mutable_ad_score()->add_top_level_contributions()->Swap(
        &pagg_response);
  }
}

void ScoreAdsReactor::SetPrivateAggregationContributionsInResponseForBuyer(
    PrivateAggregateReportingResponse pagg_response) {
  if (!pagg_response.contributions().empty()) {
    // ig_idx and adtech_origin should be set for the buyer
    if (const auto ad_it = ad_data_.find(winning_ad_dispatch_id_);
        ad_it != ad_data_.end()) {
      for (auto& contribution : *pagg_response.mutable_contributions()) {
        contribution.set_ig_idx(ad_it->second->interest_group_idx());
      }
      pagg_response.set_adtech_origin(ad_it->second->interest_group_owner());
    }
    raw_response_.mutable_ad_score()->add_top_level_contributions()->Swap(
        &pagg_response);
  }
}

void ScoreAdsReactor::PerformDebugReporting() {
  if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Skipping debug reporting since it is not yet supported for server "
           "orchestated multi-seller auctions.";
    raw_response_.mutable_ad_score()->clear_debug_report_urls();
    return;
  }
  AttestDebugUrls();
  if (!raw_response_.ad_score().has_debug_report_urls()) {
    return;
  }
  if (raw_request_.fdo_flags().enable_sampled_debug_reporting()) {
    PerformSampledDebugReporting();
  } else {
    PerformUnsampledDebugReporting();
  }
  // Clear debug urls in ad_score now that they have been pinged or added to the
  // response. This reduces the payload size.
  raw_response_.mutable_ad_score()->clear_debug_report_urls();
}

void ScoreAdsReactor::PerformSampledDebugReporting() {
  if (IsComponentAuction(auction_scope_)) {
    PerformSampledDebugReportingForComponentAuction();
    return;
  }

  // Single-seller auction.
  long current_size_all_debug_urls_chars = 0;
  bool put_seller_in_cooldown = false;
  bool cooldown_is_for_debug_win = false;

  // Iterate over all ads.
  for (const auto& [id, ad_score] : ad_scores_) {
    absl::string_view ig_owner = ad_score->interest_group_owner();
    absl::string_view ig_name = ad_score->interest_group_name();
    bool is_winning_ig = (ig_owner == post_auction_signals_.winning_ig_owner &&
                          ig_name == post_auction_signals_.winning_ig_name);

    // Consider the debug win url for the winning ad, and the debug loss url for
    // the losing ad.
    absl::string_view debug_url =
        is_winning_ig ? ad_score->debug_report_urls().auction_debug_win_url()
                      : ad_score->debug_report_urls().auction_debug_loss_url();
    if (debug_url.empty()) {
      continue;
    }

    // Case 1. Debug url passes sampling and is added to the response. Skip all
    // other ads since there is a limit of one debug url per adtech in
    // single-seller auctions.
    if (DebugUrlPassesSampling(debug_reporting_sampling_upper_bound_)) {
      AddSellerDebugReportToResponse(
          debug_url, /*is_win_report=*/is_winning_ig,
          /*is_component_win=*/false, current_size_all_debug_urls_chars,
          GetPlaceholderDataForInterestGroup(ig_owner, ig_name,
                                             post_auction_signals_));
      return;
    }

    // Case 2. Debug url is not selected during sampling. The adtech needs to be
    // put in cooldown for calling the forDebuggingOnly API.
    put_seller_in_cooldown = true;
    cooldown_is_for_debug_win = is_winning_ig;
  }

  // If no debug url was selected during sampling, add a corresponding debug
  // report with an empty url to pass cooldown information to the client.
  if (put_seller_in_cooldown) {
    AddSellerDebugReportToResponse(
        "", /*is_win_report=*/cooldown_is_for_debug_win,
        /*is_component_win=*/false, current_size_all_debug_urls_chars);
  }
}

void ScoreAdsReactor::PerformSampledDebugReportingForComponentAuction() {
  long current_size_all_debug_urls_chars = 0;

  // Early exit condition variables:
  //
  // 1. Handled the debug win url for the component auction winning ad.
  bool win_url_checked = false;
  // 2. Found a debug loss url that was selected during sampling and added to
  // the response (there is a limit of max one debug loss url per adtech).
  bool loss_url_selected = false;
  // 3. Found a debug loss url for a losing ad, guaranteeing that the adtech
  // needs to be put in cooldown.
  bool put_seller_in_cooldown_for_confirmed_loss = false;

  // Iterate over all ads.
  for (const auto& [id, ad_score] : ad_scores_) {
    // Early exit.
    if (win_url_checked && loss_url_selected &&
        put_seller_in_cooldown_for_confirmed_loss) {
      break;
    }
    absl::string_view ig_owner = ad_score->interest_group_owner();
    absl::string_view ig_name = ad_score->interest_group_name();
    bool is_winning_ig = (ig_owner == post_auction_signals_.winning_ig_owner &&
                          ig_name == post_auction_signals_.winning_ig_name);

    // Sample debug win url for component auction winning ad.
    if (is_winning_ig) {
      SampleDebugWinReportForComponentAuctionWinner(
          current_size_all_debug_urls_chars);
      win_url_checked = true;
    }

    // Sample debug loss url for all ads (since component auction winning ad can
    // still lose in the top-level auction).
    SampleDebugLossReportForComponentAuctionAd(
        *ad_score, is_winning_ig, loss_url_selected,
        put_seller_in_cooldown_for_confirmed_loss,
        current_size_all_debug_urls_chars);
  }

  // If a confirmed debug loss url was selected during sampling, add a
  // corresponding debug report with an empty url to pass cooldown information
  // to the client.
  if (put_seller_in_cooldown_for_confirmed_loss) {
    AddSellerDebugReportToResponse("", /*is_win_report=*/false,
                                   /*is_component_win=*/false,
                                   current_size_all_debug_urls_chars);
  }
}

void ScoreAdsReactor::SampleDebugWinReportForComponentAuctionWinner(
    long& current_size_all_debug_urls_chars) {
  const ScoreAdsResponse::AdScore& winning_ad_score = raw_response_.ad_score();
  absl::string_view debug_win_url =
      winning_ad_score.debug_report_urls().auction_debug_win_url();
  if (debug_win_url.empty()) {
    return;
  }

  // Sample the debug win url and add it to the response if selected.
  if (DebugUrlPassesSampling(debug_reporting_sampling_upper_bound_)) {
    AddSellerDebugReportToResponse(
        debug_win_url, /*is_win_report=*/true, /*is_component_win=*/true,
        current_size_all_debug_urls_chars,
        GetPlaceholderDataForInterestGroup(
            winning_ad_score.interest_group_owner(),
            winning_ad_score.interest_group_name(), post_auction_signals_));
    return;
  }

  // If not selected, add a corresponding debug report with an empty url to
  // pass the cooldown information to the client.
  AddSellerDebugReportToResponse("", /*is_win_report=*/true,
                                 /*is_component_win=*/true,
                                 current_size_all_debug_urls_chars);
}

void ScoreAdsReactor::SampleDebugLossReportForComponentAuctionAd(
    const ScoreAdsResponse::AdScore& ad_score, bool is_winning_ig,
    bool& loss_url_selected, bool& put_seller_in_cooldown_for_confirmed_loss,
    long& current_size_all_debug_urls_chars) {
  absl::string_view debug_loss_url =
      ad_score.debug_report_urls().auction_debug_loss_url();
  if (debug_loss_url.empty()) {
    return;
  }

  // If no debug loss url has been selected yet, and this one passes sampling,
  // add this to the response. No more debug loss urls can be selected now.
  if (!loss_url_selected &&
      DebugUrlPassesSampling(debug_reporting_sampling_upper_bound_)) {
    AddSellerDebugReportToResponse(
        debug_loss_url, /*is_win_report=*/false,
        /*is_component_win=*/is_winning_ig, current_size_all_debug_urls_chars,
        GetPlaceholderDataForInterestGroup(ad_score.interest_group_owner(),
                                           ad_score.interest_group_name(),
                                           post_auction_signals_));
    loss_url_selected = true;
    return;
  }

  // If this debug loss url is for the component auction winning ad and did
  // not get selected, add a corresponding debug report with an empty url to the
  // response. This will enable the client to put the adtech in cooldown if this
  // ad goes on to lose in the top-level auction.
  if (is_winning_ig) {
    AddSellerDebugReportToResponse("", /*is_win_report=*/false,
                                   /*is_component_win=*/true,
                                   current_size_all_debug_urls_chars);
    return;
  }

  // If this debug loss url is for a losing ad and did not get selected, this
  // is a confirmed loss url that will put the adtech in cooldown.
  put_seller_in_cooldown_for_confirmed_loss = true;
}

void ScoreAdsReactor::PerformUnsampledDebugReporting() {
  // Iterate over all ads.
  for (const auto& [id, ad_score] : ad_scores_) {
    if (!ad_score->has_debug_report_urls()) {
      continue;
    }
    absl::string_view ig_owner = ad_score->interest_group_owner();
    absl::string_view ig_name = ad_score->interest_group_name();
    bool is_winning_ig = (ig_owner == post_auction_signals_.winning_ig_owner &&
                          ig_name == post_auction_signals_.winning_ig_name);

    // For component auction winning ad, skip seller debug pings and instead
    // add both win and loss debug reports to the response.
    if (is_winning_ig && IsComponentAuction(auction_scope_)) {
      PerformUnsampledDebugReportingForComponentAuctionWinner();
      continue;
    }

    // For all other ads, send debug pings from the server.
    absl::string_view debug_url =
        is_winning_ig ? ad_score->debug_report_urls().auction_debug_win_url()
                      : ad_score->debug_report_urls().auction_debug_loss_url();
    SendDebugReportingPing(async_reporter_, debug_url, ig_owner, ig_name,
                           is_winning_ig, post_auction_signals_);
  }
}

void ScoreAdsReactor::
    PerformUnsampledDebugReportingForComponentAuctionWinner() {
  long current_size_all_debug_urls_chars = 0;
  const ScoreAdsResponse::AdScore& winning_ad_score = raw_response_.ad_score();
  DebugReportingPlaceholder placeholder_data =
      GetPlaceholderDataForInterestGroup(
          winning_ad_score.interest_group_owner(),
          winning_ad_score.interest_group_name(), post_auction_signals_);

  // Add debug win url for component auction winning ad to response if present.
  absl::string_view debug_win_url =
      winning_ad_score.debug_report_urls().auction_debug_win_url();
  if (!debug_win_url.empty()) {
    AddSellerDebugReportToResponse(debug_win_url, /*is_win_report=*/true,
                                   /*is_component_win=*/true,
                                   current_size_all_debug_urls_chars,
                                   placeholder_data);
  }

  // Add debug loss url for component auction winning ad to response if present.
  absl::string_view debug_loss_url =
      winning_ad_score.debug_report_urls().auction_debug_loss_url();
  if (!debug_loss_url.empty()) {
    AddSellerDebugReportToResponse(debug_loss_url, /*is_win_report=*/false,
                                   /*is_component_win=*/true,
                                   current_size_all_debug_urls_chars,
                                   placeholder_data);
  }
}

void ScoreAdsReactor::AddSellerDebugReportToResponse(
    absl::string_view debug_url, bool is_win_report, bool is_component_win,
    long& current_size_all_debug_urls_chars,
    std::optional<DebugReportingPlaceholder> placeholder_data) {
  // Skip debug url if total size of all debug urls in response exceeds max
  // allowed total size.
  if (debug_url.size() + current_size_all_debug_urls_chars >
      max_allowed_size_all_debug_urls_chars_) {
    // TODO(b/399172783): Add debug_url_failure_count metric to indicate status
    // as kDebugUrlRejectedForExceedingTotalSize.
    return;
  }
  current_size_all_debug_urls_chars += debug_url.size();

  // The debug url is for the winning interest group if it is a debug win url,
  // or if it is a debug loss url for the component auction winner.
  bool is_winning_ig = is_win_report || is_component_win;

  // Add debug report object to the response.
  auto* debug_report =
      raw_response_.mutable_seller_debug_reports()->add_reports();
  debug_report->set_url((placeholder_data.has_value())
                            ? CreateDebugReportingUrlForInterestGroup(
                                  debug_url, *placeholder_data, is_winning_ig)
                            : debug_url);
  debug_report->set_is_seller_report(true);
  debug_report->set_is_win_report(is_win_report);
  debug_report->set_is_component_win(is_component_win);
}

void ScoreAdsReactor::EncryptAndFinishOK() {
  raw_response_.set_auction_export_debug(log_context_.ShouldExportEvent());
  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    PS_VLOG(kPlain, log_context_)
        << "ScoreAdsRawResponse exported in EventMessage if consented";
    log_context_.SetEventMessageField(raw_response_);
  }
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(/*if_export_consented=*/true,
                                  log_context_.ShouldExportEvent());
  EncryptResponse();
  PS_VLOG(kEncrypted, log_context_) << "Encrypted ScoreAdsResponse\n"
                                    << response_->ShortDebugString();
  benchmarking_logger_->HandleResponseEnd();
  FinishWithStatus(grpc::Status::OK);
}

void ScoreAdsReactor::FinishWithStatus(const grpc::Status& status) {
  if (status.error_code() != grpc::StatusCode::OK) {
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  }
  Finish(status);
}

void ScoreAdsReactor::DispatchReportingRequestForPA(
    absl::string_view dispatch_id,
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::shared_ptr<std::string>& auction_config,
    const BuyerReportingMetadata& buyer_reporting_metadata) {
  ReportingDispatchRequestData dispatch_request_data = {
      .handler_name = kReportingDispatchHandlerFunctionName,
      .auction_config = auction_config,
      .post_auction_signals = post_auction_signals_,
      .publisher_hostname = raw_request_.publisher_hostname(),
      .log_context = log_context_,
      .buyer_reporting_metadata = buyer_reporting_metadata};
  if (auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
      auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
    dispatch_request_data.component_reporting_metadata = {
        .top_level_seller = raw_request_.top_level_seller(),
        .component_seller = raw_request_.seller()};
  } else if (auction_scope_ == AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    if (auto ad_it = component_level_reporting_data_.find(dispatch_id);
        ad_it != component_level_reporting_data_.end()) {
      dispatch_request_data.component_reporting_metadata = {
          .component_seller = ad_it->second.component_seller};
    } else {
      PS_LOG(ERROR, log_context_) << "Could not find component reporting data "
                                     "for winner ad with dispatch id: "
                                  << dispatch_id;
    }
  }
  if (winning_ad_score.bid() > 0) {
    dispatch_request_data.component_reporting_metadata.modified_bid =
        winning_ad_score.bid();
  } else {
    dispatch_request_data.component_reporting_metadata.modified_bid =
        winning_ad_score.buyer_bid();
  }
  // By this point in the logic, the bid currency has already been set
  // to refer to the modified bid if present or the buyer_bid if not (by
  // CheckAndUpdateModifiedBid() in HandleScoredAd()), so no logic is needed
  // to find the right currency.
  dispatch_request_data.component_reporting_metadata.modified_bid_currency =
      winning_ad_score.bid_currency();
  DispatchReportingRequest(dispatch_request_data);
}

void ScoreAdsReactor::DispatchReportResultRequest() {
  SellerReportingDispatchRequestData dispatch_request_data = {
      .auction_config = buyer_reporting_dispatch_request_data_.auction_config,
      .post_auction_signals = post_auction_signals_,
      .publisher_hostname = raw_request_.publisher_hostname(),
      .log_context = log_context_,
      .buyer_and_seller_reporting_id =
          buyer_reporting_dispatch_request_data_.buyer_and_seller_reporting_id,
      .selected_buyer_and_seller_reporting_id =
          buyer_reporting_dispatch_request_data_
              .selected_buyer_and_seller_reporting_id};
  seller_device_signals_ = GenerateSellerDeviceSignals(dispatch_request_data);
  absl::Time start_report_result_time = absl::Now();
  absl::Status dispatched = PerformReportResult(
      reporting_dispatch_request_config_, seller_device_signals_,
      dispatch_request_data,
      [this, start_report_result_time](
          const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        int report_result_execution_time_ms =
            (absl::Now() - start_report_result_time) / absl::Milliseconds(1);
        LogIfError(metric_context_
                       ->LogHistogram<metric::kReportResultExecutionDuration>(
                           report_result_execution_time_ms));
        ReportResultCallback(result);
      },
      dispatcher_);
  if (!dispatched.ok()) {
    PS_VLOG(kDispatch, log_context_)
        << "Perform report result for failed" << dispatched.message();
    EncryptAndFinishOK();
    return;
  }
  if (PS_VLOG_IS_ON(kNoisyInfo)) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Performed report result for seller. Winning ad:"
        << post_auction_signals_.winning_ad_render_url;
  }
}

void ScoreAdsReactor::DispatchReportResultRequestForComponentAuction(
    const ScoreAdsResponse::AdScore& winning_ad_score) {
  SellerReportingDispatchRequestData dispatch_request_data = {
      .auction_config = buyer_reporting_dispatch_request_data_.auction_config,
      .post_auction_signals = post_auction_signals_,
      .publisher_hostname = raw_request_.publisher_hostname(),
      .log_context = log_context_,
      .buyer_and_seller_reporting_id =
          buyer_reporting_dispatch_request_data_.buyer_and_seller_reporting_id,
      .selected_buyer_and_seller_reporting_id =
          buyer_reporting_dispatch_request_data_
              .selected_buyer_and_seller_reporting_id};
  dispatch_request_data.component_reporting_metadata = {
      .top_level_seller = raw_request_.top_level_seller()};
  if (winning_ad_score.bid() > 0) {
    dispatch_request_data.component_reporting_metadata.modified_bid =
        winning_ad_score.bid();
  } else {
    dispatch_request_data.component_reporting_metadata.modified_bid =
        winning_ad_score.buyer_bid();
  }
  seller_device_signals_ =
      GenerateSellerDeviceSignalsForComponentAuction(dispatch_request_data);
  absl::Status dispatched = PerformReportResult(
      reporting_dispatch_request_config_, seller_device_signals_,
      dispatch_request_data,
      [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        ReportResultCallback(result);
      },
      dispatcher_);
  if (!dispatched.ok()) {
    PS_VLOG(kDispatch, log_context_)
        << "Perform report result for failed" << dispatched.message();
    EncryptAndFinishOK();
    return;
  }
  if (PS_VLOG_IS_ON(kNoisyInfo)) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Perform report result for seller. Winning ad:"
        << post_auction_signals_.winning_ad_render_url;
  }
}

void ScoreAdsReactor::DispatchReportResultRequestForTopLevelAuction(
    absl::string_view dispatch_id) {
  SellerReportingDispatchRequestData dispatch_request_data = {
      .auction_config = BuildAuctionConfig(raw_request_),
      .post_auction_signals = post_auction_signals_,
      .publisher_hostname = raw_request_.publisher_hostname(),
      .log_context = log_context_};
  if (auto ad_it = component_level_reporting_data_.find(dispatch_id);
      ad_it != component_level_reporting_data_.end()) {
    dispatch_request_data.component_reporting_metadata = {
        .component_seller = ad_it->second.component_seller};
    // Set buyer_reporting_id in the response.
    // If this id doesn't match with the buyerReportingId on-device, reportWin
    // urls will be dropped.
    raw_response_.mutable_ad_score()->set_buyer_reporting_id(
        ad_it->second.buyer_reporting_id);
    raw_response_.mutable_ad_score()->set_buyer_and_seller_reporting_id(
        ad_it->second.buyer_and_seller_reporting_id);
  } else {
    PS_VLOG(kDispatch, log_context_)
        << "Could not find component reporting data "
           "for winner ad with dispatch id: "
        << dispatch_id;
  }
  seller_device_signals_ =
      GenerateSellerDeviceSignalsForTopLevelAuction(dispatch_request_data);
  absl::Status dispatched = PerformReportResult(
      reporting_dispatch_request_config_, seller_device_signals_,
      dispatch_request_data,
      [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        ReportResultCallback(result);
      },
      dispatcher_);
  if (!dispatched.ok()) {
    PS_VLOG(kDispatch, log_context_)
        << "Perform report result for failed" << dispatched.message();
    EncryptAndFinishOK();
    return;
  }
  if (PS_VLOG_IS_ON(kNoisyInfo)) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Performed report result for seller. Winning ad:"
        << post_auction_signals_.winning_ad_render_url;
  }
}

void ScoreAdsReactor::DispatchReportingRequestForPAS(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::shared_ptr<std::string>& auction_config,
    const BuyerReportingMetadata& buyer_reporting_metadata,
    std::string_view egress_payload,
    absl::string_view temporary_egress_payload) {
  DispatchReportingRequest(
      {.handler_name = kReportingProtectedAppSignalsFunctionName,
       .auction_config = auction_config,
       .post_auction_signals = post_auction_signals_,
       .publisher_hostname = raw_request_.publisher_hostname(),
       .log_context = log_context_,
       .buyer_reporting_metadata = buyer_reporting_metadata,
       .egress_payload = egress_payload,
       .temporary_egress_payload = temporary_egress_payload});
}

void ScoreAdsReactor::DispatchReportingRequest(
    const ReportingDispatchRequestData& dispatch_request_data) {
  if (enable_cancellation_ && context_->IsCancelled()) {
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  ReportingDispatchRequestConfig dispatch_request_config = {
      .enable_report_win_url_generation = enable_report_win_url_generation_,
      .enable_protected_app_signals = enable_protected_app_signals_,
      .enable_report_win_input_noising = enable_report_win_input_noising_,
      .enable_adtech_code_logging = enable_adtech_code_logging_,
      .roma_timeout_duration = roma_timeout_duration_};
  DispatchRequest dispatch_request = GetReportingDispatchRequest(
      dispatch_request_config, dispatch_request_data);
  dispatch_request.tags[kRomaTimeoutTag] = roma_timeout_duration_;

  std::vector<DispatchRequest> dispatch_requests = {dispatch_request};
  auto status = dispatcher_.BatchExecute(
      dispatch_requests,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            ReportingCallback(result);
          },
          [this]() {
            FinishWithStatus(
                grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
          }));

  if (!status.ok()) {
    std::string original_request;
    google::protobuf::TextFormat::PrintToString(raw_request_,
                                                &original_request);
    PS_LOG(ERROR, log_context_)
        << "Reporting execution request failed for batch: " << original_request
        << status.ToString(absl::StatusToStringMode::kWithEverything);
    EncryptAndFinishOK();
  }
}

void ScoreAdsReactor::AttestDebugUrls() {
  if (adtech_attestation_cache_ == nullptr) {
    return;
  }
  auto* debug_urls =
      raw_response_.mutable_ad_score()->mutable_debug_report_urls();
  auto win_url = debug_urls->auction_debug_win_url();
  auto loss_url = debug_urls->auction_debug_loss_url();
  if (win_url.empty() && loss_url.empty()) {
    return;
  }
  if (auto adtech_site = GetValidAdTechSite(win_url);
      (!adtech_site.ok() || !adtech_attestation_cache_->Query(*adtech_site))) {
    debug_urls->clear_auction_debug_win_url();
    PS_VLOG(kNoisyInfo) << "Debug win URL dropped due to attestation.";
  }
  if (auto adtech_site = GetValidAdTechSite(loss_url);
      (!adtech_site.ok() || !adtech_attestation_cache_->Query(*adtech_site))) {
    debug_urls->clear_auction_debug_loss_url();
    PS_VLOG(kNoisyInfo) << "Debug loss URL dropped due to attestation.";
  }
  if (debug_urls->auction_debug_win_url().empty() &&
      debug_urls->auction_debug_loss_url().empty()) {
    raw_response_.mutable_ad_score()->clear_debug_report_urls();
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
