// Copyright 2025 Google LLC
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

#include "cancellation_util.h"

#include <grpcpp/server_context.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "utils/cancellation_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

TEST(CancellationContextTest, EmptyContextIsNotCancelled) {
  EmptyCancellableServerContext empty_context;
  EXPECT_FALSE(empty_context.IsCancelled());
}

TEST(CancellationContextTest, MockContextIsCancelled) {
  MockCancellableContext mock_context(true);
  EXPECT_TRUE(mock_context.IsCancelled());
}

TEST(CancellationContextTest, MockContextIsNotCancelled) {
  MockCancellableContext mock_context(false);
  EXPECT_FALSE(mock_context.IsCancelled());
}

TEST(CancellationContextTest, GRPCContextAdapterIsDisabled) {
  grpc::ServerContext grpc_context;
  GRPCContextAdapter adapter(grpc_context, false);
  EXPECT_FALSE(adapter.IsCancelled());
}

TEST(CancellationContextTest, GRPCContextAdapterIsEnabled) {
  grpc::ServerContext grpc_context;
  GRPCContextAdapter adapter(grpc_context, true);
  EXPECT_FALSE(adapter.IsCancelled());
}

TEST(CancellationMacroTest, CancelledReturnsAbslStatus) {
  MockCancellableContext mock_context(true);
  auto check_macro = [&]() -> absl::Status {
    RETURN_IF_CANCELLED(mock_context, CancelLocation::kPredictEnter);
    return absl::OkStatus();
  };
  EXPECT_FALSE(check_macro().ok())
      << "Expected cancellation to return an error";
}

TEST(CancellationMacroTest, UncancelledContextIsOK) {
  grpc::CallbackServerContext context;
  grpc::ServerUnaryReactor* reactor = context.DefaultReactor();
  MockCancellableContext active_context(/*is_cancelled=*/false);

  auto check_macro = [&]() -> absl::StatusOr<grpc::ServerUnaryReactor*> {
    RETURN_GRPC_IF_CANCELLED(active_context, CancelLocation::kPredictEnter,
                             reactor);
    return absl::Status(absl::StatusCode::kFailedPrecondition,
                        "Context is not cancelled");
  };

  EXPECT_TRUE(!check_macro().ok()) << "gRPC returned the reactor prematurely";
}

TEST(CancellationMacroTest, UncancelledContextIsOKWithABSL) {
  grpc::CallbackServerContext context;
  GRPCContextAdapter adapter_on(context, true);
  auto check_macro_with_adapter_on = [&]() -> absl::Status {
    RETURN_IF_CANCELLED(adapter_on, CancelLocation::kPredictEnter);
    return absl::OkStatus();
  };

  absl::Status status_with_adapter_on = check_macro_with_adapter_on();
  EXPECT_TRUE(status_with_adapter_on.ok()) << status_with_adapter_on;

  GRPCContextAdapter adapter_off(context, false);
  auto check_macro_with_adapter_off = [&]() -> absl::Status {
    RETURN_IF_CANCELLED(adapter_off, CancelLocation::kPredictEnter);
    return absl::OkStatus();
  };

  absl::Status status_with_adapter_off = check_macro_with_adapter_off();
  EXPECT_TRUE(status_with_adapter_off.ok()) << status_with_adapter_off;
}
}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers::inference
