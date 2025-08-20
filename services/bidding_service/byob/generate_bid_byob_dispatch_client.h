// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SERVICES_BIDDING_SERVICE_BYOB_GENERATE_BID_BYOB_DISPATCH_CLIENT_H_
#define SERVICES_BIDDING_SERVICE_BYOB_GENERATE_BID_BYOB_DISPATCH_CLIENT_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "api/bidding_auction_servers.pb.h"
#include "api/udf/generate_bid_roma_byob_app_service.h"
#include "api/udf/generate_bid_udf_interface.pb.h"
#include "services/bidding_service/byob/proto_utils.h"
#include "services/common/clients/code_dispatcher/byob/byob_dispatch_client.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/roma/byob/config/config.h"
#include "src/roma/byob/utility/udf_blob.h"
#include "src/util/duration.h"

namespace privacy_sandbox::bidding_auction_servers {

class GenerateBidByobDispatchClient
    : public ByobDispatchClient<
          GenerateBidsRequest::GenerateBidsRawRequest,
          roma_service::GenerateProtectedAudienceBidRequest,
          roma_service::GenerateProtectedAudienceBidResponse> {
 public:
  // Factory method that creates a GenerateBidByobDispatchClient instance.
  //
  // num_workers: the number of workers to spin up in the execution environment
  // return: created instance if successful, a status indicating reason for
  // failure otherwise
  static absl::StatusOr<std::unique_ptr<GenerateBidByobDispatchClient>> Create(
      server_common::Executor* executor, int max_pending_batches,
      int num_workers);

  explicit GenerateBidByobDispatchClient(
      roma_service::ByobGenerateProtectedAudienceBidService<> byob_service,
      server_common::Executor* executor, int max_pending_batches,
      int num_workers)
      : byob_service_(std::move(byob_service)),
        executor_(executor),
        max_pending_batches_(max_pending_batches),
        num_workers_(num_workers) {}

  // Non-copyable and non-movable.
  GenerateBidByobDispatchClient(const GenerateBidByobDispatchClient&) = delete;
  GenerateBidByobDispatchClient(GenerateBidByobDispatchClient&&) = delete;
  GenerateBidByobDispatchClient& operator=(
      const GenerateBidByobDispatchClient&) = delete;
  GenerateBidByobDispatchClient& operator=(GenerateBidByobDispatchClient&&) =
      delete;

  ~GenerateBidByobDispatchClient() override {
    absl::MutexLock l(&num_pending_batches_mu_);
    num_pending_batches_mu_.Await(absl::Condition(
        +[](int* i) { return *i == 0; }, &num_pending_batches_));
  }

  // Loads new execution code into ROMA BYOB synchronously. Tracks the code
  // token, version, and hash of the loaded code, if successful.
  //
  // version: the new version string of the code to load
  // code: the code string to load
  // return: a status indicating whether the code load was successful.
  absl::Status LoadSync(std::string version, std::string code) override;

  // Executes a single request asynchronously.
  //
  // request: the request object.
  // timeout: the maximum time this will block for.
  // callback: called with the output of execution.
  // return: a status indicating if the execution request was properly
  //         processed by the implementing class. This should not be confused
  //         with the output of the execution itself, which is sent to callback.
  absl::Status Execute(
      const roma_service::GenerateProtectedAudienceBidRequest& request,
      absl::Duration timeout,
      absl::AnyInvocable<
          void(absl::StatusOr<ByobDispatchResponse<
                   roma_service::GenerateProtectedAudienceBidResponse>>) &&>
          callback) override;

  // Executes a batch of requests asynchronously.
  //
  // raw_request: the batch data object.
  // common_request: a common request object that can be updated for every
  // request in the batch start_timeout: the maximum time an execution will wait
  // for a worker execution_timeout: the maximum time this will wait for the
  // batch of executions to finish after which they will be cancelled callback:
  // called with a vector of execution outputs return: a status indicating if
  // the execution request was properly
  //         accepted by the implementing class. This should not be confused
  //         with the output of the execution itself, which is sent to callback.
  absl::Status ExecuteManyWithSharedTimeouts(
      GenerateBidsRequest::GenerateBidsRawRequest& raw_request,
      roma_service::GenerateProtectedAudienceBidRequest common_request,
      absl::Duration start_timeout, absl::Duration execution_timeout,
      absl::AnyInvocable<
          void(std::vector<absl::StatusOr<ByobDispatchResponse<
                   roma_service::GenerateProtectedAudienceBidResponse>>>) &&>
          callback) override;

 private:
  void ExecuteManyWithSharedTimeoutsImpl(
      GenerateBidsRequest::GenerateBidsRawRequest& raw_request,
      roma_service::GenerateProtectedAudienceBidRequest common_request,
      privacy_sandbox::server_common::ExpiringFlag start_by_flag,
      absl::Duration start_timeout, absl::Duration execution_timeout,
      std::vector<absl::StatusOr<ByobDispatchResponse<
          roma_service::GenerateProtectedAudienceBidResponse>>>& responses);

  // ROMA BYOB service that encapsulates the AdTech UDF interface.
  roma_service::ByobGenerateProtectedAudienceBidService<> byob_service_;
  server_common::Executor* executor_;

  int max_pending_batches_;
  int num_pending_batches_ ABSL_GUARDED_BY(num_pending_batches_mu_) = 0;
  absl::Mutex num_pending_batches_mu_;

  // Unique ID used to identify the most recently loaded code blob.
  std::string code_token_;

  // AdTech code version corresponding to the most recently loaded code blob.
  std::string code_version_ ABSL_GUARDED_BY(code_mutex_);

  // Hash of the most recently loaded code blob. Used to prevent loading the
  // same code multiple times.
  std::size_t code_hash_;

  // Mutex to protect information about the most recently loaded code blob.
  absl::Mutex code_mutex_;

  // Number of UDF workers.
  int num_workers_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BYOB_GENERATE_BID_BYOB_DISPATCH_CLIENT_H_
