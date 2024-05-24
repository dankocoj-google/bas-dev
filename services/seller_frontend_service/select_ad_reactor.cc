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

#include "services/seller_frontend_service/select_ad_reactor.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/numeric/bits.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/constants/user_error_strings.h"
#include "services/common/reporters/async_reporter.h"
#include "services/common/util/auction_scope_util.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/key_fetcher_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/communication/ohttp_utils.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr int kMillisInMinute = 60000;
constexpr int kSecsInMinute = 60;
using ScoreAdsRawRequest = ScoreAdsRequest::ScoreAdsRawRequest;
using AdScore = ScoreAdsResponse::AdScore;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
}  // namespace

SelectAdReactor::SelectAdReactor(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client, bool fail_fast,
    int max_buyers_solicited)
    : context_(context),
      request_(request),
      response_(response),
      clients_(clients),
      config_client_(config_client),
      auction_scope_(GetAuctionScope(*request_)),
      // TODO(b/278039901): Add integration test for metadata forwarding.
      buyer_metadata_(GrpcMetadataToRequestMetadata(context->client_metadata(),
                                                    kBuyerMetadataKeysMap)),
      log_context_({}, server_common::ConsentedDebugConfiguration(),
                   [this]() { return response_->mutable_debug_info(); }),
      error_accumulator_(&log_context_),
      fail_fast_(fail_fast),
      is_protected_auction_request_(false),
      // PAS should only be enabled for single seller auctions.
      is_pas_enabled_(
          config_client_.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS) &&
          (auction_scope_ == AuctionScope::AUCTION_SCOPE_SINGLE_SELLER)),
      is_protected_audience_enabled_(
          config_client_.GetBooleanParameter(ENABLE_PROTECTED_AUDIENCE)),
      max_buyers_solicited_(max_buyers_solicited),
      async_task_tracker_(
          request->auction_config().buyer_list_size(), log_context_,
          [this](bool successful) { OnAllBidsDone(successful); }) {
  if (config_client_.GetBooleanParameter(ENABLE_SELLER_FRONTEND_BENCHMARKING)) {
    benchmarking_logger_ =
        std::make_unique<BuildInputProcessResponseBenchmarkingLogger>(
            FormatTime(absl::Now()));
  } else {
    benchmarking_logger_ = std::make_unique<NoOpsLogger>();
  }
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::SfeContextMap()->Remove(request_));
    return absl::OkStatus();
  }()) << "SfeContextMap()->Get(request) should have been called";
}

AdWithBidMetadata SelectAdReactor::BuildAdWithBidMetadata(
    const AdWithBid& input, absl::string_view interest_group_owner) {
  AdWithBidMetadata result;
  // First copy all the fields that can be copied directly.
  if (input.has_ad()) {
    *result.mutable_ad() = input.ad();
  }
  result.set_bid(input.bid());
  result.set_render(input.render());
  result.set_allow_component_auction(input.allow_component_auction());
  result.mutable_ad_components()->CopyFrom(input.ad_components());
  result.set_interest_group_name(input.interest_group_name());
  result.set_ad_cost(input.ad_cost());
  result.set_modeling_signals(input.modeling_signals());
  result.set_bid_currency(input.bid_currency());

  // Then set fields passed in externally.
  result.set_interest_group_owner(interest_group_owner);

  // Finally, find the AdWithBid's IG and copy the last fields from there.
  const BuyerInput& buyer_input =
      buyer_inputs_->find(interest_group_owner)->second;
  for (const auto& interest_group : buyer_input.interest_groups()) {
    if (interest_group.name() == result.interest_group_name()) {
      result.set_interest_group_origin(interest_group.origin());
      if (request_->client_type() == CLIENT_TYPE_BROWSER) {
        result.set_join_count(interest_group.browser_signals().join_count());
        PS_VLOG(kNoisyInfo, log_context_)
            << "BrowserSignal: Recency:"
            << interest_group.browser_signals().recency();
        if (interest_group.browser_signals().has_recency_ms()) {
          // ScoreAdsRequest requires recency to be in minutes.
          result.set_recency(static_cast<int>(
              interest_group.browser_signals().recency_ms() / kMillisInMinute));
        } else {
          // ScoreAdsRequest requires recency to be in minutes.
          result.set_recency(static_cast<int>(
              interest_group.browser_signals().recency() / kSecsInMinute));
        }
      }
      // May as well skip further iterations.
      break;
    }
  }
  if (!input.buyer_reporting_id().empty()) {
    result.set_buyer_reporting_id(input.buyer_reporting_id());
  }
  return result;
}

bool SelectAdReactor::HaveClientVisibleErrors() {
  return !error_accumulator_.GetErrors(ErrorVisibility::CLIENT_VISIBLE).empty();
}

bool SelectAdReactor::HaveAdServerVisibleErrors() {
  return !error_accumulator_.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
              .empty();
}

void SelectAdReactor::MayPopulateClientVisibleErrors() {
  if (!HaveClientVisibleErrors()) {
    return;
  }

  error_.set_code(static_cast<int>(ErrorCode::CLIENT_SIDE));
  error_.set_message(error_accumulator_.GetAccumulatedErrorString(
      ErrorVisibility::CLIENT_VISIBLE));
}

grpc::Status SelectAdReactor::DecryptRequest() {
  if (request_->protected_auction_ciphertext().empty() &&
      request_->protected_audience_ciphertext().empty()) {
    return {grpc::StatusCode::INVALID_ARGUMENT,
            kEmptyProtectedAuctionCiphertextError};
  }
  is_protected_auction_request_ =
      !request_->protected_auction_ciphertext().empty();

  absl::string_view encapsulated_req;
  if (is_protected_auction_request_) {
    encapsulated_req = request_->protected_auction_ciphertext();
  } else {
    encapsulated_req = request_->protected_audience_ciphertext();
  }

  PS_VLOG(5) << "Protected "
             << (is_protected_auction_request_ ? "auction" : "audience")
             << " ciphertext: " << absl::Base64Escape(encapsulated_req);

  auto decrypted_hpke_req = DecryptOHTTPEncapsulatedHpkeCiphertext(
      encapsulated_req, clients_.key_fetcher_manager_);
  if (!decrypted_hpke_req.ok()) {
    PS_VLOG(kNoisyWarn) << "Error decrypting the protected "
                        << (is_protected_auction_request_ ? "auction"
                                                          : "audience")
                        << " input ciphertext" << decrypted_hpke_req.status();
    return {grpc::StatusCode::INVALID_ARGUMENT,
            absl::StrFormat(kErrorDecryptingCiphertextError,
                            decrypted_hpke_req.status().message())};
  }

  PS_VLOG(5) << "Successfully decrypted the protected "
             << (is_protected_auction_request_ ? "auction" : "audience")
             << " input ciphertext";

  decrypted_request_ = std::move(*decrypted_hpke_req);
  if (is_protected_auction_request_) {
    protected_auction_input_ =
        GetDecodedProtectedAuctionInput(decrypted_request_->plaintext);
  } else {
    protected_auction_input_ =
        GetDecodedProtectedAudienceInput(decrypted_request_->plaintext);
  }
  std::visit(
      [this](auto& input) {
        buyer_inputs_ = GetDecodedBuyerinputs(input.buyer_input());
        for (auto& each_buyer_input : *input.mutable_buyer_input()) {
          each_buyer_input.second = "encoded buyer input erased";
        }
      },
      protected_auction_input_);
  return grpc::Status::OK;
}

void SelectAdReactor::MayPopulateAdServerVisibleErrors() {
  if (request_->auction_config().seller_signals().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptySellerSignals,
                ErrorCode::CLIENT_SIDE);
  }

  if (request_->auction_config().auction_signals().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptyAuctionSignals,
                ErrorCode::CLIENT_SIDE);
  }

  if (request_->auction_config().buyer_list().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptyBuyerList,
                ErrorCode::CLIENT_SIDE);
  }

  if (request_->auction_config().seller().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptySeller,
                ErrorCode::CLIENT_SIDE);
  }

  if (!request_->auction_config().seller_currency().empty()) {
    if (!std::regex_match(request_->auction_config().seller_currency(),
                          kValidCurrencyCodeRegex)) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kInvalidSellerCurrency,
                  ErrorCode::CLIENT_SIDE);
    }
  }

  if (config_client_.GetStringParameter(SELLER_ORIGIN_DOMAIN) !=
      request_->auction_config().seller()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kWrongSellerDomain,
                ErrorCode::CLIENT_SIDE);
  }

  for (const auto& [buyer, per_buyer_config] :
       request_->auction_config().per_buyer_config()) {
    if (buyer.empty()) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                  kEmptyBuyerInPerBuyerConfig, ErrorCode::CLIENT_SIDE);
    }
    if (per_buyer_config.buyer_signals().empty()) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                  absl::StrFormat(kEmptyBuyerSignals, buyer),
                  ErrorCode::CLIENT_SIDE);
    }
    if (!per_buyer_config.buyer_currency().empty()) {
      if (!std::regex_match(per_buyer_config.buyer_currency(),
                            kValidCurrencyCodeRegex)) {
        ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kInvalidBuyerCurrency,
                    ErrorCode::CLIENT_SIDE);
      }
    }
  }

  if (request_->client_type() == CLIENT_TYPE_UNKNOWN) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kUnknownClientType,
                ErrorCode::CLIENT_SIDE);
  }

  // Device Component Auction not allowed with Android client type.
  if (request_->client_type() == CLIENT_TYPE_ANDROID &&
      auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                kDeviceComponentAuctionWithAndroid, ErrorCode::CLIENT_SIDE);
  }
}

void SelectAdReactor::MayLogBuyerInput() {
  if (!buyer_inputs_.ok()) {
    PS_LOG(ERROR, log_context_) << "Failed to decode buyer inputs";
  } else {
    PS_VLOG(6, log_context_)
        << "Decoded BuyerInput:\n"
        << absl::StrJoin(
               *buyer_inputs_, "\n",
               absl::PairFormatter(absl::AlphaNumFormatter(), " : ",
                                   [](std::string* out, const auto& bi) {
                                     absl::StrAppend(
                                         out, "{", bi.ShortDebugString(), "}");
                                   }));
  }
}

void SelectAdReactor::Execute() {
  grpc::Status decrypt_status = DecryptRequest();

  // Populates the logging context needed for request tracing. should be called
  // after decrypting and decoding the request.
  log_context_.Update(
      std::visit(
          [this](const auto& protected_input)
              -> absl::btree_map<std::string, std::string> {
            return {
                {kGenerationId, protected_input.generation_id()},
                {kSellerDebugId, request_->auction_config().seller_debug_id()}};
          },
          protected_auction_input_),
      std::visit(
          [](const auto& protected_input) {
            return protected_input.consented_debug_config();
          },
          protected_auction_input_));

  if (log_context_.is_consented()) {
    std::string generation_id = std::visit(
        [](const auto& protected_input) -> std::string {
          return {protected_input.generation_id()};
        },
        protected_auction_input_);
    metric_context_->SetConsented(generation_id);
  }

  if (is_protected_auction_request_) {
    LogIfError(metric_context_->LogHistogram<metric::kProtectedCiphertextSize>(
        (int)request_->protected_auction_ciphertext().size()));
  } else {
    LogIfError(metric_context_->LogHistogram<metric::kProtectedCiphertextSize>(
        (int)request_->protected_audience_ciphertext().size()));
  }
  LogIfError(metric_context_->LogHistogram<metric::kAuctionConfigSize>(
      (int)request_->auction_config().ByteSizeLong()));

  PS_VLOG(kEncrypted, log_context_) << "Encrypted SelectAdRequest:\n"
                                    << request_->ShortDebugString();
  PS_VLOG(kPlain, log_context_)
      << "Headers:\n"
      << absl::StrJoin(context_->client_metadata(), "\n",
                       absl::PairFormatter(absl::StreamFormatter(), " : ",
                                           absl::StreamFormatter()));
  if (!decrypt_status.ok()) {
    FinishWithStatus(decrypt_status);
    PS_LOG(ERROR, log_context_) << "SelectAdRequest decryption failed:"
                                << server_common::ToAbslStatus(decrypt_status);
    return;
  }
  std::visit(
      [this](const auto& input) {
        PS_VLOG(kPlain, log_context_)
            << (is_protected_auction_request_ ? "ProtectedAuctionInput"
                                              : "ProtectedAudienceInput")
            << ":\n"
            << input.ShortDebugString();
      },
      protected_auction_input_);
  MayLogBuyerInput();
  MayPopulateAdServerVisibleErrors();
  if (HaveAdServerVisibleErrors()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeSelectAdRequestBadInput));
    PS_LOG(ERROR, log_context_) << "AdServerVisible errors found, failing now";

    // Finish the GRPC request if we have received bad data from the ad tech
    // server.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }

  // Validate mandatory fields if decoding went through fine.
  if (!HaveClientVisibleErrors()) {
    PS_VLOG(5, log_context_)
        << "No ClientVisible errors found, validating input now";

    std::visit(
        [this](const auto& protected_auction_input) {
          ValidateProtectedAuctionInput(protected_auction_input);
        },
        protected_auction_input_);
  }

  // Populate errors on the response immediately after decoding and input
  // validation so that when we stop processing the request due to errors, we
  // have correct errors set in the response.
  MayPopulateClientVisibleErrors();

  if (error_accumulator_.HasErrors()) {
    PS_LOG(ERROR, log_context_) << "Some errors found, failing now";

    // Finish the GRPC request now.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  PS_VLOG(3, log_context_) << "No client / Adtech server errors found";

  benchmarking_logger_->Begin();

  PS_VLOG(6, log_context_) << "Buyer list size: "
                           << request_->auction_config().buyer_list().size();

  int num_buyers_solicited = 0;
  absl::flat_hash_set<absl::string_view> buyer_set(
      request_->auction_config().buyer_list().begin(),
      request_->auction_config().buyer_list().end());

  for (const auto& buyer_ig_owner : request_->auction_config().buyer_list()) {
    if (buyer_set.erase(buyer_ig_owner) == 0) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Duplicate buyer found " << buyer_ig_owner << ", skipping buyer";
      async_task_tracker_.TaskCompleted(TaskStatus::SKIPPED);
      continue;
    }

    if (num_buyers_solicited >= max_buyers_solicited_) {
      // Skipped buyers should not be left pending.
      async_task_tracker_.TaskCompleted(TaskStatus::SKIPPED);
      PS_VLOG(kNoisyWarn, log_context_)
          << "Exceeded cap of " << max_buyers_solicited_
          << " buyers called. Skipping buyer";
      continue;
    }

    const auto& buyer_input_iterator = buyer_inputs_->find(buyer_ig_owner);
    if (buyer_input_iterator == buyer_inputs_->end()) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "No buyer input found for buyer: " << buyer_ig_owner
          << ", skipping buyer";

      // Pending bids count is set on reactor construction to
      // buyer_list_size(). If no BuyerInput is found for a buyer in
      // buyer_list, must decrement pending bids count.
      async_task_tracker_.TaskCompleted(TaskStatus::SKIPPED);
      continue;
    }

    FetchBid(buyer_ig_owner, buyer_input_iterator->second);
    num_buyers_solicited++;
  }
  PS_VLOG(5, log_context_)
      << "Finishing execute call, response may be available later";
}

std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
SelectAdReactor::CreateGetBidsRequest(const std::string& buyer_ig_owner,
                                      const BuyerInput& buyer_input) {
  auto get_bids_request = std::make_unique<GetBidsRequest::GetBidsRawRequest>();
  get_bids_request->set_is_chaff(false);
  get_bids_request->set_seller(request_->auction_config().seller());
  get_bids_request->set_client_type(request_->client_type());
  get_bids_request->set_auction_signals(
      request_->auction_config().auction_signals());
  std::string buyer_debug_id;
  const auto& per_buyer_config_itr =
      request_->auction_config().per_buyer_config().find(buyer_ig_owner);
  if (per_buyer_config_itr !=
      request_->auction_config().per_buyer_config().end()) {
    buyer_debug_id = per_buyer_config_itr->second.buyer_debug_id();
    if (per_buyer_config_itr->second.has_buyer_kv_experiment_group_id()) {
      get_bids_request->set_buyer_kv_experiment_group_id(
          per_buyer_config_itr->second.buyer_kv_experiment_group_id());
    }
    if (!per_buyer_config_itr->second.buyer_signals().empty()) {
      get_bids_request->set_buyer_signals(
          per_buyer_config_itr->second.buyer_signals());
    }
  }
  *get_bids_request->mutable_buyer_input() = buyer_input;
  get_bids_request->set_top_level_seller(
      request_->auction_config().top_level_seller());
  std::visit(
      [&get_bids_request,
       &buyer_debug_id](const auto& protected_auction_input) {
        get_bids_request->set_publisher_name(
            protected_auction_input.publisher_name());
        get_bids_request->set_enable_debug_reporting(
            protected_auction_input.enable_debug_reporting());
        auto* log_context = get_bids_request->mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(buyer_debug_id);
        if (protected_auction_input.has_consented_debug_config()) {
          *get_bids_request->mutable_consented_debug_config() =
              protected_auction_input.consented_debug_config();
        }
        get_bids_request->set_enable_unlimited_egress(
            protected_auction_input.enable_unlimited_egress());
      },
      protected_auction_input_);

  if (!is_protected_audience_enabled_ &&
      get_bids_request->mutable_buyer_input()->interest_groups_size() > 0) {
    PS_VLOG(3) << "Clearing interest groups in the input since protected "
                  "audience support is disabled";
    get_bids_request->mutable_buyer_input()->clear_interest_groups();
  }
  return get_bids_request;
}

void SelectAdReactor::FetchBid(const std::string& buyer_ig_owner,
                               const BuyerInput& buyer_input) {
  auto buyer_client = clients_.buyer_factory.Get(buyer_ig_owner);
  if (buyer_client == nullptr) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "No buyer client found for buyer: " << buyer_ig_owner;

    async_task_tracker_.TaskCompleted(TaskStatus::SKIPPED);
  } else {
    PS_VLOG(6, log_context_) << "Getting bid from a BFE";
    absl::Duration timeout = absl::Milliseconds(
        config_client_.GetIntParameter(GET_BID_RPC_TIMEOUT_MS));
    if (request_->auction_config().buyer_timeout_ms() > 0) {
      timeout =
          absl::Milliseconds(request_->auction_config().buyer_timeout_ms());
    }
    auto get_bids_request = CreateGetBidsRequest(buyer_ig_owner, buyer_input);
    auto bfe_request =
        metric::MakeInitiatedRequest(metric::kBfe, metric_context_.get());
    bfe_request->SetBuyer(buyer_ig_owner);
    bfe_request->SetRequestSize((int)get_bids_request->ByteSizeLong());
    absl::Status execute_result = buyer_client->ExecuteInternal(
        std::move(get_bids_request), buyer_metadata_,
        [buyer_ig_owner, this, bfe_request = std::move(bfe_request)](
            absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
                response) mutable {
          {
            int response_size =
                response.ok() ? (int)response->get()->ByteSizeLong() : 0;
            bfe_request->SetResponseSize(response_size);

            // destruct bfe_request, destructor measures request time
            auto not_used = std::move(bfe_request);
          }
          PS_VLOG(6, log_context_) << "Received a response from a BFE";
          OnFetchBidsDone(std::move(response), buyer_ig_owner);
        },
        timeout);
    if (!execute_result.ok()) {
      LogIfError(
          metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
              1, metric::kSfeGetBidsFailedToCall));
      PS_LOG(ERROR, log_context_) << absl::StrFormat(
          "Failed to make async GetBids call: (buyer: %s, "
          "seller: %s, error: "
          "%s)",
          buyer_ig_owner, request_->auction_config().seller(),
          execute_result.ToString());
      async_task_tracker_.TaskCompleted(TaskStatus::ERROR);
    }
  }
}

void SelectAdReactor::LogInitiatedRequestErrorMetrics(
    absl::string_view server_name, const absl::Status& status,
    absl::string_view buyer) {
  if (server_name == metric::kAs) {
    LogIfError(metric_context_->AccumulateMetric<
               metric::kInitiatedRequestAuctionErrorCountByStatus>(
        1, StatusCodeToString(status.code())));

  } else if (server_name == metric::kKv) {
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kInitiatedRequestKVErrorCountByStatus>(
                1, StatusCodeToString(status.code())));

  } else if (server_name == metric::kBfe) {
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kSfeInitiatedRequestErrorsCountByBuyer>(
                1, buyer));
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kInitiatedRequestBfeErrorCountByStatus>(
                1, StatusCodeToString(status.code())));
  }
}

void SelectAdReactor::OnFetchBidsDone(
    absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
        response,
    const std::string& buyer_ig_owner) {
  PS_VLOG(5, log_context_) << "Received response from a BFE ... ";
  if (response.ok()) {
    auto& found_response = *response;
    PS_VLOG(kOriginated, log_context_) << "\nGetBidsResponse:\n"
                                       << found_response->DebugString();
    if (found_response->has_debug_info()) {
      server_common::DebugInfo& bfe_log =
          *response_->mutable_debug_info()->add_downstream_servers();
      bfe_log = std::move(*found_response->mutable_debug_info());
      bfe_log.set_server_name(buyer_ig_owner);
    }
    if ((!is_protected_audience_enabled_ || found_response->bids().empty()) &&
        (!is_pas_enabled_ ||
         found_response->protected_app_signals_bids().empty())) {
      PS_VLOG(kNoisyWarn, log_context_) << "Skipping buyer " << buyer_ig_owner
                                        << " due to empty GetBidsResponse.";

      async_task_tracker_.TaskCompleted(TaskStatus::EMPTY_RESPONSE);
    } else {
      async_task_tracker_.TaskCompleted(
          TaskStatus::SUCCESS,
          [this, &buyer_ig_owner, response = *std::move(response)]() mutable {
            shared_buyer_bids_map_.try_emplace(buyer_ig_owner,
                                               std::move(response));
          });
    }
  } else {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeGetBidsResponseError));
    LogInitiatedRequestErrorMetrics(metric::kBfe, response.status(),
                                    buyer_ig_owner);
    PS_LOG(ERROR, log_context_)
        << "GetBidsRequest failed for buyer " << buyer_ig_owner
        << "\nresponse status: " << response.status();

    async_task_tracker_.TaskCompleted(TaskStatus::ERROR);
  }
}

void SelectAdReactor::OnAllBidsDone(bool any_successful_bids) {
  if (context_->IsCancelled()) {
    // Early return if request is cancelled. DO NOT move to next step.
    FinishWithStatus(grpc::Status(grpc::ABORTED, kRequestCancelled));
    return;
  }

  // No successful bids received.
  if (shared_buyer_bids_map_.empty()) {
    PS_VLOG(kNoisyWarn, log_context_) << kNoBidsReceived;

    if (!any_successful_bids) {
      LogIfError(
          metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
              1, metric::kSfeSelectAdNoSuccessfulBid));
      PS_VLOG(3, log_context_)
          << "Finishing the SelectAdRequest RPC with an error";

      FinishWithStatus(grpc::Status(grpc::INTERNAL, kInternalServerError));
      return;
    }
    // Since no buyers have returned bids, we would still finish the call RPC
    // call here and send a chaff back.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }

  // Must fulfill preconditions:
  // That the shared_buyer_bids_map_ is non-empty was just checked above
  // That each buyer has bids was checked in OnFetchBidsDone().
  // Thus the preconditions of the following method are satisfied.
  if (!FilterBidsWithMismatchingCurrency()) {
    PS_VLOG(kNoisyWarn, log_context_) << kAllBidsRejectedBuyerCurrencyMismatch;
    FinishWithStatus(grpc::Status(grpc::INVALID_ARGUMENT,
                                  kAllBidsRejectedBuyerCurrencyMismatch));
  } else {
    FetchScoringSignals();
  }
}

template <typename T>
void SelectAdReactor::FilterBidsWithMismatchingCurrencyHelper(
    google::protobuf::RepeatedPtrField<T>* ads_with_bids,
    absl::string_view buyer_currency) {
  int i = 0;
  int remove_starting_at = ads_with_bids->size();
  while (i < remove_starting_at) {
    const T& ad_with_bid = (*ads_with_bids)[i];
    if (!ad_with_bid.bid_currency().empty() &&
        buyer_currency != ad_with_bid.bid_currency()) {
      // Swap to last. Mark for removal and make sure we don't check it again
      ads_with_bids->SwapElements(i, --remove_starting_at);
      LogIfError(
          metric_context_->AccumulateMetric<metric::kAuctionBidRejectedCount>(
              1, ToSellerRejectionReasonString(
                     SellerRejectionReason::
                         BID_FROM_GENERATE_BID_FAILED_CURRENCY_CHECK)));
      // Leave index un-incremented so swapped element is checked.
    } else {
      i++;
    }
  }
  // Delete all mismatched pas_bids.
  if (remove_starting_at < ads_with_bids->size()) {
    ads_with_bids->DeleteSubrange(remove_starting_at,
                                  ads_with_bids->size() - remove_starting_at);
  }
}

bool SelectAdReactor::FilterBidsWithMismatchingCurrency() {
  DCHECK(!shared_buyer_bids_map_.empty())
      << "PRECONDITION 1: shared_buyer_bids_map_ must have at least one entry.";
  DCHECK(([shared_buyer_bids_map_ptr = &(shared_buyer_bids_map_),
           is_protected_audience_enabled = is_protected_audience_enabled_,
           is_pas_enabled = is_pas_enabled_]() {
    for (const auto& [buyer_ig_owner, get_bid_response] :
         *shared_buyer_bids_map_ptr) {
      // Check if no bids are present.
      if ((!is_protected_audience_enabled ||
           get_bid_response->bids().empty()) &&
          (!is_pas_enabled ||
           get_bid_response->protected_app_signals_bids().empty())) {
        return false;
      }
    }
    return true;
  }()))
      << "PRECONDITION 2: each buyer in shared_buyer_bids_map must be "
         "non-empty.";
  bool any_valid_bids = false;
  // Check that each AdWithBid's currency is as-expected.
  // Throw out the AdWithBids which do not match.
  for (auto& [buyer_ig_owner, get_bids_raw_response] : shared_buyer_bids_map_) {
    // It is possible for a buyer to have no buyer_config.
    const auto& buyer_config_itr =
        request_->auction_config().per_buyer_config().find(buyer_ig_owner);
    if (buyer_config_itr ==
        request_->auction_config().per_buyer_config().end()) {
      // Preconditions mean bids must be present.
      any_valid_bids = true;
      continue;
    }
    // Not all buyers have an expected currency specified.
    absl::string_view buyer_currency =
        buyer_config_itr->second.buyer_currency();
    if (buyer_currency.empty()) {
      // Preconditions mean bids must be present.
      any_valid_bids = true;
      continue;
    }

    FilterBidsWithMismatchingCurrencyHelper<AdWithBid>(
        get_bids_raw_response->mutable_bids(), buyer_currency);
    FilterBidsWithMismatchingCurrencyHelper<ProtectedAppSignalsAdWithBid>(
        get_bids_raw_response->mutable_protected_app_signals_bids(),
        buyer_currency);

    // Check if any bids remain.
    if ((is_protected_audience_enabled_ &&
         !get_bids_raw_response->bids().empty()) ||
        (is_pas_enabled_ &&
         !get_bids_raw_response->protected_app_signals_bids().empty())) {
      any_valid_bids = true;
    }
  }
  // Clear buyers with no remaining bids.
  absl::erase_if(
      shared_buyer_bids_map_,
      [is_p_aud_enabled = is_protected_audience_enabled_,
       is_pas_enabled = is_pas_enabled_](const auto& buyer_to_bids_pair) {
        const auto& get_bids_raw_response = buyer_to_bids_pair.second;
        // Check if any bids remain.
        return ((!is_p_aud_enabled || get_bids_raw_response->bids().empty()) &&
                (!is_pas_enabled ||
                 get_bids_raw_response->protected_app_signals_bids().empty()));
      });
  return any_valid_bids;
}

void SelectAdReactor::FetchScoringSignals() {
  ScoringSignalsRequest scoring_signals_request(
      shared_buyer_bids_map_, buyer_metadata_, request_->client_type());
  if (request_->auction_config().has_code_experiment_spec() &&
      request_->auction_config()
          .code_experiment_spec()
          .has_seller_kv_experiment_group_id()) {
    scoring_signals_request.seller_kv_experiment_group_id_ =
        absl::StrCat(request_->auction_config()
                         .code_experiment_spec()
                         .seller_kv_experiment_group_id());
  }
  auto kv_request =
      metric::MakeInitiatedRequest(metric::kKv, metric_context_.get());
  clients_.scoring_signals_async_provider.Get(
      scoring_signals_request,
      [this, kv_request = std::move(kv_request)](
          absl::StatusOr<std::unique_ptr<ScoringSignals>> result,
          GetByteSize get_byte_size) mutable {
        {
          // Only logs KV request and response sizes if fetching signals
          // succeeds.
          if (result.ok()) {
            kv_request->SetRequestSize(static_cast<int>(get_byte_size.request));
            kv_request->SetResponseSize(
                static_cast<int>(get_byte_size.response));
          }
          // destruct kv_request, destructor measures request time
          auto not_used = std::move(kv_request);
        }
        OnFetchScoringSignalsDone(std::move(result));
      },
      absl::Milliseconds(config_client_.GetIntParameter(
          KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS)));
}

void SelectAdReactor::OnFetchScoringSignalsDone(
    absl::StatusOr<std::unique_ptr<ScoringSignals>> result) {
  if (!result.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeScoringSignalsResponseError));
    LogInitiatedRequestErrorMetrics(metric::kKv, result.status());
    PS_LOG(ERROR, log_context_)
        << "Scoring signals fetch from key-value server failed: "
        << result.status();

    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kInternalError,
                ErrorCode::SERVER_SIDE);
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  scoring_signals_ = std::move(result).value();
  // If signals are empty, return chaff.
  if (scoring_signals_->scoring_signals->empty()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Scoring signals fetch from key-value server "
           "succeeded but were empty.";

    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  ScoreAds();
}

std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
SelectAdReactor::CreateScoreAdsRequest() {
  auto raw_request = std::make_unique<ScoreAdsRequest::ScoreAdsRawRequest>();
  if (is_protected_audience_enabled_) {
    // If protected audience support is not enabled then buyers should not
    // be returning bids anyway for Protected Audience but this check is placed
    // here for safety as well as efficiency.
    for (const auto& [buyer_ig_owner, get_bid_response] :
         shared_buyer_bids_map_) {
      for (const AdWithBid& ad_with_bid : get_bid_response->bids()) {
        raw_request->mutable_ad_bids()->Add(
            BuildAdWithBidMetadata(ad_with_bid, buyer_ig_owner));
      }
    }
  }
  *raw_request->mutable_auction_signals() =
      request_->auction_config().auction_signals();
  raw_request->set_seller_currency(
      request_->auction_config().seller_currency());
  *raw_request->mutable_seller_signals() =
      request_->auction_config().seller_signals();
  raw_request->set_top_level_seller(
      request_->auction_config().top_level_seller());
  if (scoring_signals_ != nullptr) {
    // Ad scoring signals cannot be used after this.
    raw_request->set_allocated_scoring_signals(
        scoring_signals_->scoring_signals.release());
  }
  std::visit(
      [&raw_request, this](const auto& protected_auction_input) {
        raw_request->set_publisher_hostname(
            protected_auction_input.publisher_name());
        raw_request->set_enable_debug_reporting(
            protected_auction_input.enable_debug_reporting());
        auto* log_context = raw_request->mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(
            request_->auction_config().seller_debug_id());
        if (protected_auction_input.has_consented_debug_config()) {
          *raw_request->mutable_consented_debug_config() =
              protected_auction_input.consented_debug_config();
        }
      },
      protected_auction_input_);

  for (const auto& [buyer, per_buyer_config] :
       request_->auction_config().per_buyer_config()) {
    raw_request->mutable_per_buyer_signals()->try_emplace(
        buyer, per_buyer_config.buyer_signals());
  }
  raw_request->set_seller(request_->auction_config().seller());
  return raw_request;
}

void SelectAdReactor::ScoreAds() {
  auto raw_request = CreateScoreAdsRequest();
  if (raw_request->ad_bids().empty() &&
      raw_request->protected_app_signals_ad_bids().empty()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "No Protected Audience or Protected App "
           "Signals ads to score, sending chaff response";
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  PS_VLOG(kOriginated, log_context_) << "\nScoreAdsRawRequest:\n"
                                     << raw_request->DebugString();

  auto auction_request =
      metric::MakeInitiatedRequest(metric::kAs, metric_context_.get());
  auction_request->SetRequestSize((int)raw_request->ByteSizeLong());
  auto on_scoring_done =
      [this, auction_request = std::move(auction_request)](
          absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
              result) mutable {
        {
          int response_size =
              result.ok() ? (int)result->get()->ByteSizeLong() : 0;
          auction_request->SetResponseSize(response_size);
          // destruct auction_request, destructor measures request time
          auto not_used = std::move(auction_request);
        }
        OnScoreAdsDone(std::move(result));
      };
  absl::Status execute_result = clients_.scoring.ExecuteInternal(
      std::move(raw_request), {}, std::move(on_scoring_done),
      absl::Milliseconds(
          config_client_.GetIntParameter(SCORE_ADS_RPC_TIMEOUT_MS)));
  if (!execute_result.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeScoreAdsFailedToCall));
    PS_LOG(ERROR, log_context_)
        << absl::StrFormat("Failed to make async ScoreAds call: (error: %s)",
                           execute_result.ToString());
    FinishWithStatus(grpc::Status(grpc::INTERNAL, kInternalServerError));
  }
}

void SelectAdReactor::FinishWithStatus(const grpc::Status& status) {
  if (status.error_code() != grpc::StatusCode::OK) {
    PS_LOG(ERROR, log_context_) << "RPC failed: " << status.error_message();
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  }
  if (metric_context_->CustomState(kWinningAd).ok()) {
    LogIfError(metric_context_->LogHistogram<metric::kSfeWithWinnerTimeMs>(
        static_cast<int>((absl::Now() - start_) / absl::Milliseconds(1))));
  }
  benchmarking_logger_->End();
  Finish(status);
}

void SelectAdReactor::OnScoreAdsDone(
    absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
        response) {
  std::optional<AdScore> high_score;
  if (HaveAdServerVisibleErrors()) {
    PS_VLOG(3, log_context_)
        << "Finishing the SelectAdRequest RPC with ad server visible error";

    FinishWithStatus(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                  error_accumulator_.GetAccumulatedErrorString(
                                      ErrorVisibility::AD_SERVER_VISIBLE)));
    return;
  }

  PS_VLOG(kOriginated, log_context_)
      << "ScoreAdsResponse status:" << response.status();
  auto scoring_return_code =
      static_cast<grpc::StatusCode>(response.status().code());
  if (!response.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeScoreAdsResponseError));
    LogInitiatedRequestErrorMetrics(metric::kAs, response.status());
    benchmarking_logger_->End();
    // Any INTERNAL errors from auction service will be suppressed by SFE and
    // will cause a chaff to be sent back. Non-INTERNAL errors on the other hand
    // are propagated back the seller ad service.
    if (scoring_return_code != grpc::StatusCode::INTERNAL) {
      FinishWithStatus(grpc::Status(scoring_return_code,
                                    std::string(response.status().message())));
      return;
    }
  }

  if (scoring_return_code == grpc::StatusCode::OK) {
    const auto& found_response = *response;
    if (found_response->has_ad_score() &&
        found_response->ad_score().buyer_bid() > 0) {
      high_score = found_response->ad_score();
      LogIfError(
          metric_context_->LogUpDownCounter<metric::kRequestWithWinnerCount>(
              1));
      metric_context_->SetCustomState(kWinningAd, "");
    }
    if (found_response->has_debug_info()) {
      server_common::DebugInfo& auction_log =
          *response_->mutable_debug_info()->add_downstream_servers();
      auction_log = std::move(*found_response->mutable_debug_info());
      auction_log.set_server_name("auction");
    }
    PerformDebugReporting(high_score);
  }

  std::optional<AuctionResult::Error> error;
  if (HaveClientVisibleErrors()) {
    error = std::move(error_);
  }
  absl::StatusOr<std::string> non_encrypted_response =
      GetNonEncryptedResponse(high_score, error);
  if (!non_encrypted_response.ok()) {
    return;
  }

  if (!EncryptResponse(*std::move(non_encrypted_response))) {
    return;
  }

  PS_VLOG(kEncrypted, log_context_) << "Encrypted SelectAdResponse:\n"
                                    << response_->ShortDebugString();

  FinishWithStatus(grpc::Status::OK);
}

DecodedBuyerInputs SelectAdReactor::GetDecodedBuyerinputs(
    const EncodedBuyerInputs& encoded_buyer_inputs) {
  return DecodeBuyerInputs(encoded_buyer_inputs, error_accumulator_,
                           fail_fast_);
}

bool SelectAdReactor::EncryptResponse(std::string plaintext_response) {
  absl::StatusOr<std::string> encapsulated_response;
  if (auction_scope_ ==
      AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
    // If this will be decrypted by another SFE, encrypt with public key.
    auto encrypted_request =
        HpkeEncrypt(plaintext_response, *clients_.crypto_client_ptr_,
                    clients_.key_fetcher_manager_,
                    ProtoCloudPlatformToScpCloudPlatform(
                        request_->auction_config().top_level_cloud_platform()));
    if (!encrypted_request.ok()) {
      PS_LOG(ERROR, log_context_) << "Error while encrypting response: "
                                  << encrypted_request.status().message();
      FinishWithStatus(
          grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
      return false;
    }
    encapsulated_response = std::move(encrypted_request->ciphertext);
    *response_->mutable_key_id() = std::move(encrypted_request->key_id);
  } else {
    // Decrypted by device -> Encrypt with corresponding private key.
    encapsulated_response = server_common::EncryptAndEncapsulateResponse(
        std::move(plaintext_response), decrypted_request_->private_key,
        decrypted_request_->context, decrypted_request_->request_label);
  }

  if (!encapsulated_response.ok()) {
    PS_VLOG(4, log_context_)
        << absl::StrFormat("Error during response encryption/encapsulation: %s",
                           encapsulated_response.status().message());

    FinishWithStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return false;
  }

  response_->mutable_auction_result_ciphertext()->assign(
      std::move(*encapsulated_response));
  return true;
}

void SelectAdReactor::PerformDebugReporting(
    const std::optional<AdScore>& high_score) {
  // Create new metric context.
  metric::MetricContextMap<SelectAdRequest>()->Get(request_);
  // Make metric_context a unique_ptr by releasing the ownership of the context
  // from ContextMap.
  absl::StatusOr<std::unique_ptr<metric::SfeContext>> metric_context =
      metric::MetricContextMap<SelectAdRequest>()->Remove(request_);
  CHECK_OK(metric_context);
  std::shared_ptr<metric::SfeContext> shared_context =
      *std::move(metric_context);

  bool enable_debug_reporting = false;
  std::visit(
      [&enable_debug_reporting](const auto& protected_auction_input) {
        enable_debug_reporting =
            protected_auction_input.enable_debug_reporting();
      },
      protected_auction_input_);
  if (!enable_debug_reporting) {
    return;
  }

  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      high_score, request_->auction_config().seller_currency());
  for (const auto& [ig_owner, get_bid_response] : shared_buyer_bids_map_) {
    for (int i = 0; i < get_bid_response->bids_size(); i++) {
      const AdWithBid& ad_with_bid = get_bid_response->bids().at(i);
      const auto& ig_name = ad_with_bid.interest_group_name();
      if (!ad_with_bid.has_debug_report_urls()) {
        continue;
      }
      absl::string_view debug_url;
      bool is_win_debug_url = false;
      if (post_auction_signals.winning_ig_owner == ig_owner &&
          ad_with_bid.interest_group_name() ==
              post_auction_signals.winning_ig_name) {
        debug_url = ad_with_bid.debug_report_urls().auction_debug_win_url();
        is_win_debug_url = true;
      } else {
        debug_url = ad_with_bid.debug_report_urls().auction_debug_loss_url();
      }
      if (debug_url.empty()) {
        continue;
      }

      absl::AnyInvocable<void(absl::StatusOr<absl::string_view>)> done_cb;
      if (server_common::log::PS_VLOG_IS_ON(5)) {
        done_cb =
            [ig_owner = ig_owner, ig_name](
                absl::StatusOr<absl::string_view> result) mutable {  // NOLINT
              if (result.ok()) {
                PS_VLOG(5) << "Performed debug reporting for:" << ig_owner
                           << ", interest_group: " << ig_name;
              } else {
                PS_VLOG(5) << "Error while performing debug reporting for:"
                           << ig_owner << ", interest_group: " << ig_name
                           << " ,status:" << result.status();
              }
            };
      } else {
        done_cb = [](absl::StatusOr<absl::string_view> result) {};  // NOLINT
      }
      HTTPRequest http_request = CreateDebugReportingHttpRequest(
          debug_url,
          GetPlaceholderDataForInterestGroup(ig_owner, ig_name,
                                             post_auction_signals),
          is_win_debug_url);
      clients_.reporting->DoReport(http_request, std::move(done_cb));
    }
  }
}

void SelectAdReactor::OnDone() { delete this; }

void SelectAdReactor::OnCancel() {
  // TODO(b/245982466): Handle early abort and errors.
}

void SelectAdReactor::ReportError(
    log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    const std::string& msg, ErrorCode error_code) {
  const auto& location = error_visibility_with_loc.location;
  ErrorVisibility error_visibility = error_visibility_with_loc.mandatory_param;
  error_accumulator_.ReportError(location, error_visibility, msg, error_code);
}

}  // namespace privacy_sandbox::bidding_auction_servers
