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

#include "services/bidding_service/byob/generate_bid_byob_dispatch_client.h"

#include <functional>
#include <memory>
#include <vector>

#include "absl/synchronization/notification.h"
#include "services/common/loggers/request_log_context.h"
#include "src/roma/byob/config/config.h"
#include "src/util/duration.h"
#include "src/util/execution_token.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::privacy_sandbox::server_common::byob::Mode;
using ::privacy_sandbox::server_common::byob::ProcessRequestMetrics;
using ::privacy_sandbox::server_common::byob::UdfBlob;

namespace {
inline void AwaitOrCancelAllCallbacks(
    int* count, absl::Mutex* count_mu, const absl::Duration& execution_timeout,
    std::vector<google::scp::roma::ExecutionToken> execution_tokens,
    roma_service::ByobGenerateProtectedAudienceBidService<>& byob_service) {
  absl::Condition callbacks_all_called(
      +[](int* count) { return *count == 0; }, count);
  {
    // Wait for all callbacks to be called or the batch to timeout.
    absl::MutexLock lock(count_mu);
    if (count_mu->AwaitWithTimeout(callbacks_all_called, execution_timeout)) {
      return;
    }
  }
  for (auto& execution_token : execution_tokens) {
    byob_service.Cancel(std::move(execution_token));
  }
  absl::MutexLock lock(count_mu);
  count_mu->Await(callbacks_all_called);
}

absl::StatusOr<
    ByobDispatchResponse<roma_service::GenerateProtectedAudienceBidResponse>>
ParseGenerateBidResponse(
    absl::StatusOr<roma_service::GenerateProtectedAudienceBidResponse> response,
    const absl::StatusOr<std::string_view>& /*logs*/,
    ProcessRequestMetrics metrics) {
  if (response.ok()) {
    return absl::StatusOr<ByobDispatchResponse<
        roma_service::GenerateProtectedAudienceBidResponse>>(
        {.response = *std::move(response), .metrics = metrics});
  } else {
    return std::move(response).status();
  }
}

}  // namespace

absl::StatusOr<std::unique_ptr<GenerateBidByobDispatchClient>>
GenerateBidByobDispatchClient::Create(server_common::Executor* executor,
                                      int max_pending_batches,
                                      int num_workers) {
  PS_ASSIGN_OR_RETURN(
      auto byob_service,
      roma_service::ByobGenerateProtectedAudienceBidService<>::Create(
          {
              .syscall_filtering = ::privacy_sandbox::server_common::byob::
                  SyscallFiltering::kWorkerEngineSyscallFiltering,
              // TODO: b/398051960 - Enable this once bug is fixed.
              .disable_ipc_namespace = true,
          },
          /*mode=*/Mode::kModeNsJailSandbox));
  return std::make_unique<GenerateBidByobDispatchClient>(
      std::move(byob_service), executor, max_pending_batches, num_workers);
}

absl::Status GenerateBidByobDispatchClient::LoadSync(std::string version,
                                                     std::string code) {
  if (version.empty()) {
    return absl::InvalidArgumentError(
        "Code version to be loaded cannot be empty.");
  }
  if (code.empty()) {
    return absl::InvalidArgumentError("Code to be loaded cannot be empty.");
  }

  // Skip if code hash matches the hash of already loaded code.
  std::size_t new_code_hash = std::hash<std::string>{}(code);
  if (new_code_hash == code_hash_) {
    return absl::OkStatus();
  }

  // Get UDF blob for the given code and register it with the BYOB service.
  PS_ASSIGN_OR_RETURN(UdfBlob udf_blob, UdfBlob::Create(std::move(code)));
  PS_ASSIGN_OR_RETURN(std::string new_code_token,
                      byob_service_.Register(udf_blob(), num_workers_));

  // Acquire lock before updating info about the most recently loaded code
  // blob.
  if (code_mutex_.TryLock()) {
    code_token_ = std::move(new_code_token);
    code_version_ = std::move(version);
    code_hash_ = new_code_hash;
    code_mutex_.Unlock();
  }
  return absl::OkStatus();
}

absl::Status GenerateBidByobDispatchClient::Execute(
    const roma_service::GenerateProtectedAudienceBidRequest& request,
    absl::Duration timeout,
    absl::AnyInvocable<
        void(absl::StatusOr<ByobDispatchResponse<
                 roma_service::GenerateProtectedAudienceBidResponse>>) &&>
        callback) {
  PS_VLOG(kNoisyInfo) << "Dispatching GenerateBid Binary UDF";
  // Start the call.
  return byob_service_
      .GenerateProtectedAudienceBid(
          [callback = std::move(callback)](
              absl::StatusOr<roma_service::GenerateProtectedAudienceBidResponse>
                  response,
              const absl::StatusOr<std::string_view>& /*logs*/,
              ProcessRequestMetrics metrics) mutable {
            if (response.ok()) {
              std::move(callback)(
                  absl::StatusOr<ByobDispatchResponse<
                      roma_service::GenerateProtectedAudienceBidResponse>>(
                      {.response = *std::move(response), .metrics = metrics}));
            } else {
              std::move(callback)(response.status());
            }
          },
          request, /*metadata=*/{}, code_token_, timeout)
      .status();
}

absl::Status GenerateBidByobDispatchClient::ExecuteManyWithSharedTimeouts(
    GenerateBidsRequest::GenerateBidsRawRequest& raw_request,
    roma_service::GenerateProtectedAudienceBidRequest common_request,
    absl::Duration start_timeout, absl::Duration execution_timeout,
    absl::AnyInvocable<
        void(std::vector<absl::StatusOr<ByobDispatchResponse<
                 roma_service::GenerateProtectedAudienceBidResponse>>>) &&>
        callback) {
  {
    absl::MutexLock l(&num_pending_batches_mu_);
    if (num_pending_batches_ >= max_pending_batches_) {
      return absl::ResourceExhaustedError("Max pending batches exceeded.");
    }
    ++num_pending_batches_;
  }
  privacy_sandbox::server_common::ExpiringFlag start_by_flag;
  start_by_flag.Set(start_timeout);
  executor_->Run([this, execution_timeout, &raw_request,
                  common_request = std::move(common_request),
                  callback = std::move(callback), start_by_flag = start_by_flag,
                  start_timeout]() mutable {
    std::vector<absl::StatusOr<ByobDispatchResponse<
        roma_service::GenerateProtectedAudienceBidResponse>>>
        responses;
    ExecuteManyWithSharedTimeoutsImpl(raw_request, common_request,
                                      start_by_flag, start_timeout,
                                      execution_timeout, responses);
    std::move(callback)(std::move(responses));
    absl::MutexLock l(&num_pending_batches_mu_);
    --num_pending_batches_;
  });
  return absl::OkStatus();
}

void GenerateBidByobDispatchClient::ExecuteManyWithSharedTimeoutsImpl(
    GenerateBidsRequest::GenerateBidsRawRequest& raw_request,
    roma_service::GenerateProtectedAudienceBidRequest common_request,
    privacy_sandbox::server_common::ExpiringFlag start_by_flag,
    absl::Duration start_timeout, absl::Duration execution_timeout,
    std::vector<absl::StatusOr<ByobDispatchResponse<
        roma_service::GenerateProtectedAudienceBidResponse>>>& responses) {
  PS_VLOG(kNoisyInfo) << "Dispatching GenerateBid Binary UDF";
  int batch_size = raw_request.interest_group_for_bidding_size();
  responses = std::vector<absl::StatusOr<ByobDispatchResponse<
      roma_service::GenerateProtectedAudienceBidResponse>>>(batch_size);
  int count = batch_size;
  absl::Mutex count_mu;
  std::vector<google::scp::roma::ExecutionToken> execution_tokens;
  execution_tokens.reserve(batch_size);
  for (int i = 0; i < batch_size; ++i) {
    UpdateProtectedAudienceBidRequest(
        common_request, raw_request,
        *raw_request.mutable_interest_group_for_bidding(i));
    auto execution_token = byob_service_.GenerateProtectedAudienceBid(
        [i, &responses, &count, &count_mu](
            absl::StatusOr<roma_service::GenerateProtectedAudienceBidResponse>
                response,
            const absl::StatusOr<std::string_view>& logs,
            ProcessRequestMetrics metrics) mutable {
          responses[i] =
              ParseGenerateBidResponse(std::move(response), logs, metrics);
          absl::MutexLock lock(&count_mu);
          --count;
        },
        common_request, /*metadata=*/{}, code_token_,
        start_by_flag.GetTimeRemaining());
    if (!execution_token.ok()) {
      if (start_timeout == absl::Milliseconds(0)) {
        // Case for 0 start_timeout
        responses[i] = std::move(execution_token).status();
        absl::MutexLock lock(&count_mu);
        --count;
      } else {
        // Set all other responses to same status.
        for (int j = i + 1; j < batch_size; ++j) {
          responses[j] = execution_token.status();
        }
        responses[i] = std::move(execution_token).status();
        absl::MutexLock lock(&count_mu);
        count -= batch_size - i;

        // Do not dispatch the rest of the batch.
        break;
      }
    } else {
      execution_tokens.push_back(*std::move(execution_token));
    }
  }
  // Execution tokens not usable after this.
  AwaitOrCancelAllCallbacks(&count, &count_mu, execution_timeout,
                            std::move(execution_tokens), byob_service_);
}
}  // namespace privacy_sandbox::bidding_auction_servers
