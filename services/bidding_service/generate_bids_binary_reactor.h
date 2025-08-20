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

#ifndef SERVICES_BIDDING_SERVICE_GENERATE_BIDS_BINARY_REACTOR_H_
#define SERVICES_BIDDING_SERVICE_GENERATE_BIDS_BINARY_REACTOR_H_

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.pb.h"
#include "api/udf/generate_bid_udf_interface.pb.h"
#include "services/bidding_service/base_generate_bids_reactor.h"
#include "services/bidding_service/byob/proto_utils.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/bidding_service/utils/validation.h"
#include "services/common/attestation/adtech_enrollment_cache.h"
#include "services/common/clients/code_dispatcher/byob/byob_dispatch_client.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/async_task_tracker.h"

namespace privacy_sandbox::bidding_auction_servers {

//  This is a gRPC reactor that serves a single GenerateBidsRequest by running
//  generateBid binary for each interest group in the raw request. It stores
//  state relevant to the request and after the response is finished being
//  served, GenerateBidsBinaryReactor cleans up all necessary state and grpc
//  releases the reactor from memory.
//  Output metrics:
//  - kBiddingGenerateBidsFailedToDispatchCode for any dispatch errors
//     - including if no executions could be dispatched due within
//     batchStartTimeoutMs.
//  - kUdfExecutionErrorCount for every error status recieved after execution
//  - kBiddingGenerateBidsDispatchTimedOutError for any execution not started
//  within batchStartTimeoutMs.
//  - kBiddingGenerateBidsTimedOutError for executions that started but timed
//  out.
//  - kBiddingGenerateBidsDispatchResponseError for
//  executions that returned an error response.
//  Output status:
//  - OK if a bid was generated from one of the executions
//  - INTERNAL if all executions errored out (including timeouts)
//  - RESOURCE_EXHAUSTED if no executions could be dispatched due within
//  batchStartTimeoutMs
class GenerateBidsBinaryReactor
    : public BaseGenerateBidsReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse> {
 public:
  explicit GenerateBidsBinaryReactor(
      grpc::CallbackServerContext* context,
      ByobDispatchClient<GenerateBidsRequest::GenerateBidsRawRequest,
                         roma_service::GenerateProtectedAudienceBidRequest,
                         roma_service::GenerateProtectedAudienceBidResponse>&
          byob_client,
      const GenerateBidsRequest* request, GenerateBidsResponse* response,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const BiddingServiceRuntimeConfig& runtime_config,
      AdtechEnrollmentCacheInterface* adtech_attestation_cache = nullptr);

  // Initiates parallel synchronous executions for each interest group in the
  // GenerateBidsRequest using executor_.
  void Execute() override;

 private:
  // Dispatches requests sequentially to BYOB client and tracks/processes all
  // responses separately using async_task_tracker_.
  void DispatchExecutions();

  // Initiates the synchronous execution for the interest group at the given
  // index of the GenerateBidsRequest.
  void DispatchInterestGroup(
      roma_service::GenerateProtectedAudienceBidRequest& bid_request,
      int ig_index);

  // Dispatches requests to BYOB client as a single batch and processes the
  // response as a single batch of a vector of responses.
  void DispatchExecutionsInBatch();

  // Processes an individual UDF response status.
  TaskStatus ProcessResponse(
      absl::StatusOr<ByobDispatchResponse<
          roma_service::GenerateProtectedAudienceBidResponse>>
          bid_response_status,
      std::string_view ig_name, int ig_index);

  // Once all bids are fetched, this callback gets executed.
  void OnAllBidsDone(bool any_successful_bids);

  // Encrypts the response before the gRPC call is finished with the provided
  // status.
  void EncryptResponseAndFinish(grpc::Status status);

  // Encrypts the response before the gRPC call is finished with the provided
  // status.
  void FinishWithNoBids(grpc::Status status);

  // Cleans up and deletes the GenerateBidsBinaryReactor. Called by the grpc
  // library after the response has finished.
  void OnDone() override;

  // Store the stats for the entire batch of UDF executions in a request.
  struct UdfStats {
    // Total started executions.
    int total = 0;

    // Total executions that timed out while executing.
    int timeout_executed = 0;

    // Total executions that timed out while waiting for a worker.
    int timeout_waiting = 0;

    // Total executions that finished execution with success.
    int success_executed = 0;

    // Total executions that finished execution with error.
    int error_executed = 0;

    // Max of time(in ms) spent waiting for worker by each execution in  batch.
    int max_queue_time_ms;

    // Avg time taken to finish by each success execution in batch.
    int avg_execution_time_ms;
  } udf_stats_;

  grpc::CallbackServerContext* context_;

  // Dispatches execution requests to the ROMA BYOB library.
  ByobDispatchClient<GenerateBidsRequest::GenerateBidsRawRequest,
                     roma_service::GenerateProtectedAudienceBidRequest,
                     roma_service::GenerateProtectedAudienceBidResponse>*
      byob_client_;

  // Keeps track of the pending bid requests and executes the registered
  // callback once all the bids have been fetched.
  AsyncTaskTracker async_task_tracker_;

  // The maximum time within which UDF executions need to finish.
  absl::Duration roma_timeout_duration_;

  // Stores the list of bids for an interest group, for each interest group.
  std::vector<std::vector<AdWithBid>> ads_with_bids_by_ig_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::BiddingContext> metric_context_;

  // The timestamp at which the first ROMA execution dispatch was started. Used
  // to calculate udf_execution.duration_ms metric.
  absl::Time start_binary_execution_time_;

  // Specifies whether this is a single seller or component auction.
  // Impacts the parsing of generateBid output.
  AuctionScope auction_scope_;

  // Specifies the maximum time a batch should wait for a worker to be
  // available.
  std::optional<absl::Duration> batch_start_timeout_;

  // Specifies the max size limits and sampling config for debug reporting.
  DebugUrlsValidationConfig debug_urls_validation_config_;

  const bool is_logging_enabled_;

  const bool is_debug_reporting_enabled_;

  const bool is_request_in_cooldown_or_lockout_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_GENERATE_BIDS_BINARY_REACTOR_H_
