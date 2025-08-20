//  Copyright 2024 Google LLC
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

#include "services/bidding_service/generate_bids_binary_reactor.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/error_categories.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using IGForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;

inline constexpr absl::Duration kDefaultGenerateBidExecutionTimeout =
    absl::Seconds(1);

roma_service::GenerateProtectedAudienceBidRequest
BuildCommonProtectedAudienceBidRequest(RawRequest& raw_request,
                                       bool logging_enabled,
                                       bool debug_reporting_enabled) {
  roma_service::GenerateProtectedAudienceBidRequest bid_request;

  // Populate auction and buyer signals.
  bid_request.set_auction_signals(
      std::move(*raw_request.mutable_auction_signals()));
  bid_request.set_per_buyer_signals(
      std::move(*raw_request.mutable_buyer_signals()));

  // Populate server metadata.
  roma_service::ServerMetadata* server_metadata =
      bid_request.mutable_server_metadata();
  server_metadata->set_logging_enabled(logging_enabled);
  server_metadata->set_debug_reporting_enabled(debug_reporting_enabled);
  return bid_request;
}

std::vector<AdWithBid> ParseProtectedAudienceBids(
    google::protobuf::RepeatedPtrField<roma_service::ProtectedAudienceBid>&
        bids,
    absl::string_view ig_name, bool collect_debug_reporting_urls) {
  // Reserve memory to prevent reallocation.
  std::vector<AdWithBid> ads_with_bids;
  ads_with_bids.reserve(bids.size());

  // Iterate through every roma_service::ProtectedAudienceBid and build
  // corresponding AdWithBid.
  for (roma_service::ProtectedAudienceBid& bid : bids) {
    AdWithBid ad_with_bid;
    *ad_with_bid.mutable_ad()->mutable_string_value() =
        std::move(*bid.mutable_ad());
    ad_with_bid.set_bid(bid.bid());
    *ad_with_bid.mutable_render() = std::move(*bid.mutable_render());
    ad_with_bid.mutable_ad_components()->Swap(bid.mutable_ad_components());
    ad_with_bid.set_allow_component_auction(bid.allow_component_auction());
    ad_with_bid.set_interest_group_name(
        ig_name);  // Cannot move since it can be shared by multiple bids
    ad_with_bid.set_ad_cost(bid.ad_cost());
    if (bid.has_debug_report_urls() && collect_debug_reporting_urls) {
      *(ad_with_bid.mutable_debug_report_urls()
            ->mutable_auction_debug_win_url()) =
          std::move(*bid.mutable_debug_report_urls()
                         ->mutable_auction_debug_win_url());
      *(ad_with_bid.mutable_debug_report_urls()
            ->mutable_auction_debug_loss_url()) =
          std::move(*bid.mutable_debug_report_urls()
                         ->mutable_auction_debug_loss_url());
    }
    ad_with_bid.set_modeling_signals(bid.modeling_signals());
    *ad_with_bid.mutable_bid_currency() =
        std::move(*bid.mutable_bid_currency());
    if (!bid.mutable_buyer_reporting_id()->empty()) {
      *ad_with_bid.mutable_buyer_reporting_id() =
          std::move(*bid.mutable_buyer_reporting_id());
    }
    if (!bid.mutable_buyer_and_seller_reporting_id()->empty()) {
      *ad_with_bid.mutable_buyer_and_seller_reporting_id() =
          std::move(*bid.mutable_buyer_and_seller_reporting_id());
    }
    if (!bid.mutable_selected_buyer_and_seller_reporting_id()->empty()) {
      *ad_with_bid.mutable_selected_buyer_and_seller_reporting_id() =
          std::move(*bid.mutable_selected_buyer_and_seller_reporting_id());
    }
    ads_with_bids.emplace_back(std::move(ad_with_bid));
  }
  return ads_with_bids;
}

void ExportLogsByType(
    absl::string_view ig_name,
    const google::protobuf::RepeatedPtrField<std::string>& logs,
    absl::string_view log_type, RequestLogContext& log_context) {
  if (!logs.empty()) {
    for (const std::string& log : logs) {
      std::string formatted_log =
          absl::StrCat("[", log_type, "] ", ig_name, ": ", log);
      PS_VLOG(kUdfLog, log_context) << formatted_log;
      log_context.SetEventMessageField(formatted_log);
    }
  }
}

size_t EstimateNumBids(
    const std::vector<std::vector<AdWithBid>>& ads_with_bids_by_ig) {
  size_t num_bids = 0;
  for (const auto& ads_with_bids : ads_with_bids_by_ig) {
    num_bids += ads_with_bids.size();
  }
  return num_bids;
}

void HandleLogMessages(absl::string_view ig_name,
                       const roma_service::LogMessages& log_messages,
                       RequestLogContext& log_context) {
  PS_VLOG(kUdfLog, log_context) << "UDF log messages for " << ig_name
                                << " exported in EventMessage if consented";
  ExportLogsByType(ig_name, log_messages.logs(), "log", log_context);
  ExportLogsByType(ig_name, log_messages.warnings(), "warning", log_context);
  ExportLogsByType(ig_name, log_messages.errors(), "error", log_context);
}

absl::Duration StringMsToAbslDuration(const std::string& string_ms) {
  absl::Duration duration;
  if (absl::ParseDuration(string_ms, &duration)) {
    return duration;
  }
  return kDefaultGenerateBidExecutionTimeout;
}
}  // namespace

GenerateBidsBinaryReactor::GenerateBidsBinaryReactor(
    grpc::CallbackServerContext* context,
    ByobDispatchClient<GenerateBidsRequest::GenerateBidsRawRequest,
                       roma_service::GenerateProtectedAudienceBidRequest,
                       roma_service::GenerateProtectedAudienceBidResponse>&
        byob_client,
    const GenerateBidsRequest* request, GenerateBidsResponse* response,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BiddingServiceRuntimeConfig& runtime_config,
    AdtechEnrollmentCacheInterface* adtech_attestation_cache)
    : BaseGenerateBidsReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse>(
          runtime_config, request, response, key_fetcher_manager,
          crypto_client),
      context_(context),
      byob_client_(&byob_client),
      async_task_tracker_(raw_request_.interest_group_for_bidding_size(),
                          log_context_,
                          [this](bool any_successful_bid) {
                            OnAllBidsDone(any_successful_bid);
                          }),
      roma_timeout_duration_(StringMsToAbslDuration(
          BaseGenerateBidsReactor::roma_timeout_duration_)),
      auction_scope_(
          raw_request_.top_level_seller().empty()
              ? AuctionScope::AUCTION_SCOPE_SINGLE_SELLER
              : AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER),
      batch_start_timeout_(runtime_config.enable_byob_batching
                               ? std::make_optional<absl::Duration>(
                                     runtime_config.byob_batch_start_timeout)
                               : std::nullopt),
      is_logging_enabled_(enable_adtech_code_logging_ ||
                          !server_common::log::IsProd()),
      is_debug_reporting_enabled_(enable_buyer_debug_url_generation_ &&
                                  raw_request_.enable_debug_reporting()),
      is_request_in_cooldown_or_lockout_(
          raw_request_.fdo_flags().enable_sampled_debug_reporting() &&
          raw_request_.fdo_flags().in_cooldown_or_lockout()) {
  PS_CHECK_OK(
      [this]() {
        PS_ASSIGN_OR_RETURN(metric_context_,
                            metric::BiddingContextMap()->Remove(request_));
        if (log_context_.is_consented()) {
          metric_context_->SetConsented(
              raw_request_.log_context().generation_id());
        } else if (log_context_.is_prod_debug()) {
          metric_context_->SetConsented(kProdDebug.data());
        }
        return absl::OkStatus();
      }(),
      log_context_)
      << "BiddingContextMap()->Get(request) should have been called";
  debug_urls_validation_config_ = {
      .max_allowed_size_debug_url_chars =
          runtime_config.max_allowed_size_debug_url_bytes,
      .max_allowed_size_all_debug_urls_chars =
          kBytesMultiplyer * runtime_config.max_allowed_size_all_debug_urls_kb,
      .enable_sampled_debug_reporting =
          raw_request_.fdo_flags().enable_sampled_debug_reporting(),
      .debug_reporting_sampling_upper_bound =
          runtime_config.debug_reporting_sampling_upper_bound,
      .attestation_cache = adtech_attestation_cache};
}

void GenerateBidsBinaryReactor::Execute() {
  if (enable_cancellation_ && context_->IsCancelled()) {
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    if (server_common::log::PS_VLOG_IS_ON(kEncrypted)) {
      PS_VLOG(kEncrypted, log_context_)
          << "Encrypted GenerateBidsRequest exported in EventMessage if "
             "consented";
      log_context_.SetEventMessageField(*request_);
    }
    PS_VLOG(kPlain, log_context_)
        << "GenerateBidsRawRequest exported in EventMessage if consented";
    log_context_.SetEventMessageField(raw_request_);
  }

  if (raw_request_.multi_bid_limit() <= 0) {
    // used when multi bid limit is not specified.
    raw_request_.set_multi_bid_limit(kDefaultMultiBidLimit);
  }

  if (batch_start_timeout_) {
    DispatchExecutionsInBatch();
  } else {
    DispatchExecutions();
  }
}

void GenerateBidsBinaryReactor::DispatchExecutions() {
  // Number of tasks to track equals the number of interest groups.
  int ig_count = raw_request_.interest_group_for_bidding_size();
  async_task_tracker_.SetNumTasksToTrack(ig_count);
  udf_stats_.total = ig_count;

  // Resize to number of interest groups in advance to prevent reallocation.
  ads_with_bids_by_ig_.resize(ig_count);

  roma_service::GenerateProtectedAudienceBidRequest common_bid_request =
      BuildCommonProtectedAudienceBidRequest(raw_request_, is_logging_enabled_,
                                             is_debug_reporting_enabled_);

  // Send execution requests for each interest group immediately.
  start_binary_execution_time_ = absl::Now();
  for (int ig_index = 0; ig_index < ig_count; ++ig_index) {
    // Populate bid request for given interest group.
    UpdateProtectedAudienceBidRequest(
        common_bid_request, raw_request_,
        *raw_request_.mutable_interest_group_for_bidding(ig_index));
    DispatchInterestGroup(common_bid_request, ig_index);
  }
}

void GenerateBidsBinaryReactor::DispatchInterestGroup(
    roma_service::GenerateProtectedAudienceBidRequest& bid_request,
    int ig_index) {
  const std::string ig_name = bid_request.interest_group().name();
  // Make asynchronous execute call using the BYOB client.
  PS_VLOG(kNoisyInfo) << "Starting UDF execution for IG: " << ig_name;
  absl::Status execute_status = byob_client_->Execute(
      bid_request, roma_timeout_duration_,
      [this, ig_index,
       ig_name](absl::StatusOr<ByobDispatchResponse<
                    roma_service::GenerateProtectedAudienceBidResponse>>
                    bid_response_status) {
        async_task_tracker_.TaskCompleted(
            ProcessResponse(std::move(bid_response_status), ig_name, ig_index));
      });

  // Handle the case where execute call fails to dispatch to UDF binary.
  if (!execute_status.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Execution of GenerateProtectedAudienceBid request failed "
           "for IG "
        << ig_name << " with status: "
        << execute_status.ToString(absl::StatusToStringMode::kWithEverything);
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                       1, metric::kBiddingGenerateBidsFailedToDispatchCode));
    async_task_tracker_.TaskCompleted(TaskStatus::ERROR);
  }
}

void GenerateBidsBinaryReactor::DispatchExecutionsInBatch() {
  // Resize to number of interest groups in advance to prevent reallocation.
  int ig_count = raw_request_.interest_group_for_bidding_size();
  ads_with_bids_by_ig_.resize(ig_count);
  udf_stats_.total = ig_count;

  std::vector<std::string> ig_names;
  ig_names.reserve(ig_count);

  for (int ig_index = 0; ig_index < ig_count; ++ig_index) {
    ig_names.push_back(
        raw_request_.mutable_interest_group_for_bidding(ig_index)->name());
  }
  roma_service::GenerateProtectedAudienceBidRequest common_bid_request =
      BuildCommonProtectedAudienceBidRequest(raw_request_, is_logging_enabled_,
                                             is_debug_reporting_enabled_);
  start_binary_execution_time_ = absl::Now();
  const absl::Status execute_status =
      byob_client_->ExecuteManyWithSharedTimeouts(
          raw_request_, std::move(common_bid_request),
          /*start_timeout=*/
          batch_start_timeout_ ? *batch_start_timeout_ : absl::ZeroDuration(),
          /*execution_timeout=*/roma_timeout_duration_,
          [this, ig_names = std::move(ig_names)](
              std::vector<absl::StatusOr<ByobDispatchResponse<
                  roma_service::GenerateProtectedAudienceBidResponse>>>
                  responses) {
            for (int ig_index = 0; ig_index < responses.size(); ++ig_index) {
              ProcessResponse(std::move(responses[ig_index]),
                              ig_names[ig_index], ig_index);
            }
            OnAllBidsDone(udf_stats_.success_executed > 0);
          });
  if (!execute_status.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Execution of GenerateProtectedAudienceBid failed with status: "
        << execute_status.ToString(absl::StatusToStringMode::kWithEverything);
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                ig_count, metric::kBiddingGenerateBidsFailedToDispatchCode));
    if (execute_status.code() == absl::StatusCode::kResourceExhausted) {
      FinishWithNoBids(grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                                    "Queue size exceeded. Try again."));
    } else {
      FinishWithNoBids(
          grpc::Status(grpc::StatusCode::INTERNAL, kInternalError));
    }
    return;
  }
}

TaskStatus GenerateBidsBinaryReactor::ProcessResponse(
    absl::StatusOr<ByobDispatchResponse<
        roma_service::GenerateProtectedAudienceBidResponse>>
        bid_response_status,
    absl::string_view ig_name, int ig_index) {
  if (absl::IsUnavailable(bid_response_status.status())) {
    // Assume UDF non-response caused by cancellation.
    bid_response_status = absl::CancelledError("Execution timed out.");
  }
  // Error response.
  if (!bid_response_status.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Execution of GenerateProtectedAudienceBid request failed "
           "for IG "
        << ig_name << " with status: "
        << bid_response_status.status().ToString(
               absl::StatusToStringMode::kWithEverything);
    LogIfError(
        metric_context_->AccumulateMetric<metric::kUdfExecutionErrorCount>(1));
    if (bid_response_status.status().code() ==
        absl::StatusCode::kDeadlineExceeded) {
      // Execution dispatch timed out.
      udf_stats_.timeout_waiting += 1;
      udf_stats_.max_queue_time_ms =
          (batch_start_timeout_
               ? std::max<int>(udf_stats_.max_queue_time_ms,
                               (*batch_start_timeout_) / absl::Milliseconds(1))
               : udf_stats_.max_queue_time_ms);
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                         1, metric::kBiddingGenerateBidsDispatchTimedOutError));
      return TaskStatus::SKIPPED;
    } else if (bid_response_status.status().code() ==
               absl::StatusCode::kCancelled) {
      // Execution started but timed out.
      udf_stats_.timeout_executed += 1;
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                         1, metric::kBiddingGenerateBidsTimedOutError));
      return TaskStatus::CANCELLED;
    } else {
      // Execution failed.
      udf_stats_.error_executed += 1;
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                         1, metric::kBiddingGenerateBidsDispatchResponseError));
      return TaskStatus::ERROR;
    }
  }
  udf_stats_.success_executed += 1;
  int binary_execution_time_ms = (bid_response_status->metrics.send_time +
                                  bid_response_status->metrics.response_time) /
                                 absl::Milliseconds(1);
  // New Avg = Old Avg + (Val - Old Avg)/N
  udf_stats_.avg_execution_time_ms +=
      (binary_execution_time_ms - udf_stats_.avg_execution_time_ms) /
      udf_stats_.success_executed;
  LogIfError(metric_context_->AccumulateMetric<metric::kUdfExecutionDuration>(
      binary_execution_time_ms));
  udf_stats_.max_queue_time_ms = std::max<int>(
      udf_stats_.max_queue_time_ms,
      (bid_response_status->metrics.wait_time / absl::Milliseconds(1)));

  // Empty response.
  roma_service::GenerateProtectedAudienceBidResponse& bid_response =
      bid_response_status->response;
  if (!bid_response.IsInitialized()) {
    PS_LOG(INFO, log_context_)
        << "Execution of GenerateProtectedAudienceBid request for IG "
        << ig_name << "returned an empty response";
    return TaskStatus::EMPTY_RESPONSE;
  }

  // Initalized response.
  // Print log messages if present if logging enabled.
  if (is_logging_enabled_ && bid_response.has_log_messages()) {
    HandleLogMessages(ig_name, bid_response.log_messages(), log_context_);
  }

  if (server_common::log::PS_VLOG_IS_ON(kDispatch)) {
    PS_VLOG(kDispatch, log_context_)
        << "Generate Bids BYOB Response: " << bid_response;
  }

  // Empty response.
  if (bid_response.bids_size() == 0) {
    return TaskStatus::EMPTY_RESPONSE;
  }

  // Populate list of AdsWithBids for this interest group from the bid
  // response returned by UDF.
  if (bid_response.bids_size() > raw_request_.multi_bid_limit()) {
    PS_LOG(ERROR, log_context_)
        << "Execution of GenerateProtectedAudienceBid request for IG "
        << ig_name << " returned more bids than the allowed multi_bid_limit";
    // TODO(b/416751872): publish metric for error and parse the first
    // multi_bid_limit_ bids?
    return TaskStatus::ERROR;
  }

  if (bid_response.bids_size() > 0) {
    // Populate list of AdsWithBids for this interest group from the bid
    // response returned by UDF.
    ads_with_bids_by_ig_[ig_index] = ParseProtectedAudienceBids(
        *bid_response.mutable_bids(), ig_name,
        (is_debug_reporting_enabled_ && !is_request_in_cooldown_or_lockout_));
  }
  return TaskStatus::SUCCESS;
}

void GenerateBidsBinaryReactor::FinishWithNoBids(grpc::Status status) {
  LogIfError(
      metric_context_->LogHistogram<metric::kBiddingFailedToBidPercent>(1.0));
  LogIfError(metric_context_->LogHistogram<metric::kBiddingTotalBidsCount>(0));
  PS_LOG(WARNING, log_context_)
      << "No successful bids returned by the adtech UDF";
  EncryptResponseAndFinish(std::move(status));
}

void GenerateBidsBinaryReactor::OnAllBidsDone(bool any_successful_bids) {
  // Log total end-to-end execution time.
  int binary_execution_time_ms =
      (absl::Now() - start_binary_execution_time_) / absl::Milliseconds(1);
  LogIfError(metric_context_->LogHistogram<metric::kUdfBatchExecutionDuration>(
      binary_execution_time_ms));

  // Log all other metrics
  PS_VLOG(kStats, log_context_)
      << "Avg batch execution time: " << udf_stats_.avg_execution_time_ms;
  LogIfError(
      metric_context_->LogHistogram<metric::kUdfExecutionQueueingDuration>(
          udf_stats_.max_queue_time_ms));
  // Handle the case where none of the requests could be scheduled.
  if (udf_stats_.timeout_waiting == udf_stats_.total && !any_successful_bids) {
    FinishWithNoBids(
        grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                     "All execution dispatch requests timed out. Try again."));
    return;
  }

  // Handle the case where none of the bid responses are successful.
  if (!any_successful_bids) {
    FinishWithNoBids(grpc::Status(
        grpc::INTERNAL, "No successful bids returned by the adtech UDF"));
    return;
  }

  // Handle cancellation.
  if (enable_cancellation_ && context_->IsCancelled()) {
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  // Populate GenerateBidsRawResponse from list of valid AdsWithBids for all
  // interest groups.
  int failed_to_bid_count =
      udf_stats_.timeout_executed + udf_stats_.timeout_waiting;
  int received_bid_count = 0;
  int zero_bid_count = 0;
  int total_debug_urls_count = 0;
  long total_debug_urls_chars = 0;
  int rejected_component_bid_count = 0;
  raw_response_.mutable_bids()->Reserve(EstimateNumBids(ads_with_bids_by_ig_));
  for (std::vector<AdWithBid>& ads_with_bids : ads_with_bids_by_ig_) {
    if (ads_with_bids.empty()) {
      continue;
    }
    for (AdWithBid& ad_with_bid : ads_with_bids) {
      received_bid_count += 1;
      if (absl::Status validation_status =
              IsValidProtectedAudienceBid(ad_with_bid, auction_scope_);
          !validation_status.ok()) {
        PS_VLOG(kNoisyInfo, log_context_) << validation_status.message();
        if (validation_status.code() == absl::StatusCode::kInvalidArgument) {
          zero_bid_count += 1;
        } else if (validation_status.code() ==
                   absl::StatusCode::kPermissionDenied) {
          // Received allowComponentAuction=false.
          ++rejected_component_bid_count;
        }
      } else {
        total_debug_urls_count += ValidateBuyerDebugUrls(
            ad_with_bid, total_debug_urls_chars, debug_urls_validation_config_,
            log_context_, metric_context_.get());
        *raw_response_.add_bids() = std::move(ad_with_bid);
      }
    }
  }
  LogIfError(metric_context_->LogHistogram<metric::kBiddingFailedToBidPercent>(
      (static_cast<double>(failed_to_bid_count)) /
      ads_with_bids_by_ig_.size()));
  LogIfError(metric_context_->LogHistogram<metric::kBiddingTotalBidsCount>(
      received_bid_count));
  if (received_bid_count > 0) {
    LogIfError(metric_context_->LogUpDownCounter<metric::kBiddingZeroBidCount>(
        zero_bid_count));
    LogIfError(metric_context_->LogHistogram<metric::kBiddingZeroBidPercent>(
        (static_cast<double>(zero_bid_count)) / received_bid_count));
  }
  if (rejected_component_bid_count > 0) {
    LogIfError(
        metric_context_->LogUpDownCounter<metric::kBiddingBidRejectedCount>(
            rejected_component_bid_count));
  }
  LogIfError(metric_context_->AccumulateMetric<metric::kBiddingDebugUrlCount>(
      total_debug_urls_count, kBuyerDebugUrlSentToSeller));
  LogIfError(metric_context_->LogHistogram<metric::kBiddingDebugUrlsSizeBytes>(
      static_cast<double>(total_debug_urls_chars)));
  EncryptResponseAndFinish(grpc::Status::OK);
}

void GenerateBidsBinaryReactor::EncryptResponseAndFinish(grpc::Status status) {
  raw_response_.set_bidding_export_debug(log_context_.ShouldExportEvent());
  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    PS_VLOG(kPlain, log_context_)
        << "GenerateBidsRawResponse exported in EventMessage if consented";
    log_context_.SetEventMessageField(raw_response_);
  }
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(
      /*if_export_consented=*/true, log_context_.ShouldExportEvent());

  if (!EncryptResponse()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to encrypt the generate bids response";
    status = grpc::Status(grpc::INTERNAL, kInternalServerError);
  }
  if (status.error_code() != grpc::StatusCode::OK) {
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  }
  PS_VLOG(kEncrypted, log_context_) << "Encrypted GenerateBidsResponse\n"
                                    << response_->ShortDebugString();
  Finish(status);
}

void GenerateBidsBinaryReactor::OnDone() { delete this; }

}  // namespace privacy_sandbox::bidding_auction_servers
