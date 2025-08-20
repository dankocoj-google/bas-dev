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

#include "services/common/clients/cancellable_grpc_context_manager.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/clients/code_dispatcher/request_context.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

// Test fixture for CancellableGrpcContextManager
class CancellableGrpcContextManagerTest : public ::testing::Test {
 protected:
  CancellableGrpcContextManager manager_;
};

TEST_F(CancellableGrpcContextManagerTest, InitialStateIsntCancelled) {
  EXPECT_FALSE(manager_.IsCancelled());
  EXPECT_EQ(manager_.GetContextCount(), 0);
}

TEST_F(CancellableGrpcContextManagerTest, CreateContextSuccessfully) {
  std::shared_ptr<grpc::ClientContext> ctx1 = manager_.CreateClientContext();
  ASSERT_NE(ctx1, nullptr);
  EXPECT_FALSE(manager_.IsCancelled());
  EXPECT_EQ(manager_.GetContextCount(), 1);

  std::shared_ptr<grpc::ClientContext> ctx2 = manager_.CreateClientContext();
  ASSERT_NE(ctx2, nullptr);
  EXPECT_NE(ctx1, ctx2) << "Each created context should have a unique address";
  EXPECT_EQ(manager_.GetContextCount(), 2);
}

TEST_F(CancellableGrpcContextManagerTest, TryCancelSetsFlag) {
  manager_.TryCancelAll();
  EXPECT_TRUE(manager_.IsCancelled());
}

TEST_F(CancellableGrpcContextManagerTest,
       CannotCreateContextAfterCancellation) {
  // Create one context before cancelling.
  std::shared_ptr<grpc::ClientContext> ctx_before =
      manager_.CreateClientContext();
  ASSERT_NE(ctx_before, nullptr);
  ASSERT_EQ(manager_.GetContextCount(), 1);

  // Cancel the manager.
  manager_.TryCancelAll();
  EXPECT_TRUE(manager_.IsCancelled());

  // Attempt to create another context.
  std::shared_ptr<grpc::ClientContext> ctx_after =
      manager_.CreateClientContext();
  EXPECT_EQ(ctx_after, nullptr);

  // The count should not have increased.
  EXPECT_EQ(manager_.GetContextCount(), 1);
}

TEST_F(CancellableGrpcContextManagerTest, TryCancelIsIdempotent) {
  ASSERT_FALSE(manager_.IsCancelled());
  manager_.TryCancelAll();
  EXPECT_TRUE(manager_.IsCancelled());
  manager_.TryCancelAll();
  EXPECT_TRUE(manager_.IsCancelled());
  EXPECT_EQ(manager_.CreateClientContext(), nullptr);
}

TEST_F(CancellableGrpcContextManagerTest,
       ThreadSafetyOnCreationAndCancellation) {
  constexpr int num_creator_threads = 8;
  constexpr int creations_per_thread = 100;
  constexpr long target_creation_time_ms = 20;

  // The cancellation will trigger about halfway through the total creation
  // time.
  constexpr long cancel_delay_ms = target_creation_time_ms / 2;
  // The interleaving delay is set to spread out one thread's creations
  // over the target runtime.
  constexpr long interleaving_delay_us =
      (target_creation_time_ms * 1000) / creations_per_thread;

  std::vector<std::thread> threads;
  std::atomic<int> successful_creations = 0;
  std::atomic<int> failed_creations = 0;

  // This thread will cancel the manager after a short, unpredictable delay.
  std::thread canceller([this, cancel_delay_ms]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(cancel_delay_ms));
    manager_.TryCancelAll();
  });

  // These threads will all race to create contexts.
  threads.reserve(num_creator_threads);
  for (int i = 0; i < num_creator_threads; ++i) {
    threads.emplace_back([this, &successful_creations, &failed_creations,
                          interleaving_delay_us]() {
      for (int j = 0; j < creations_per_thread; ++j) {
        if (manager_.CreateClientContext() != nullptr) {
          successful_creations++;
        } else {
          failed_creations++;
        }
        // Small sleep to increase the chance of thread interleaving.
        std::this_thread::sleep_for(
            std::chrono::microseconds(interleaving_delay_us));
      }
    });
  }

  // The manager must end in a cancelled state.
  canceller.join();
  for (auto& t : threads) {
    t.join();
  }

  // Due to the race, some creations must have succeeded and failed.
  EXPECT_TRUE(manager_.IsCancelled());
  EXPECT_GT(successful_creations, 0);
  EXPECT_GT(failed_creations, 0);

  // The total number of attempts must match the expected number.
  EXPECT_EQ(successful_creations + failed_creations,
            num_creator_threads * creations_per_thread);

  // The final count of contexts stored in the manager must match the number
  // of successful creations.
  EXPECT_EQ(manager_.GetContextCount(), successful_creations);

  // All subsequent creation attempts must fail.
  EXPECT_EQ(manager_.CreateClientContext(), nullptr);
}

// These are tests to verify the behavior of RomaRequestSharedContext
// when it interacts with CancellableGrpcContextManager.
class RomaRequestSharedContextGrpcManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::btree_map<std::string, std::string> context_map;
    privacy_sandbox::server_common::ConsentedDebugConfiguration debug_config;
    absl::AnyInvocable<privacy_sandbox::server_common::DebugInfo*()>
        debug_info = []() -> privacy_sandbox::server_common::DebugInfo* {
      return nullptr;
    };

    factory_ = std::make_unique<RomaRequestContextFactory>(
        context_map, debug_config, std::move(debug_info));
  }

  void TearDown() override { factory_.reset(); }

  std::unique_ptr<RomaRequestContextFactory> factory_;
};

TEST_F(RomaRequestSharedContextGrpcManagerTest,
       GetCancellableClientContext_NoManagerProvided) {
  RomaRequestSharedContext shared_context = factory_->Create();

  absl::StatusOr<std::shared_ptr<grpc::ClientContext>> client_context_status =
      shared_context.GetCancellableClientContext();

  EXPECT_FALSE(client_context_status.ok());
  EXPECT_EQ(client_context_status.status().code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(client_context_status.status().message(),
            "CancellableGrpcContextManager not available in this context.");
}

TEST_F(RomaRequestSharedContextGrpcManagerTest,
       GetCancellableClientContext_ManagerPresent_Success) {
  auto grpc_manager = std::make_shared<CancellableGrpcContextManager>();

  RomaRequestSharedContext shared_context = factory_->Create(grpc_manager);

  absl::StatusOr<std::shared_ptr<grpc::ClientContext>> client_context_status =
      shared_context.GetCancellableClientContext();

  EXPECT_TRUE(client_context_status.ok());
  EXPECT_NE(*client_context_status, nullptr);
}

TEST_F(RomaRequestSharedContextGrpcManagerTest,
       GetCancellableClientContext_ManagerPresent_Cancelled) {
  auto grpc_manager = std::make_shared<CancellableGrpcContextManager>();

  grpc_manager->TryCancelAll();

  RomaRequestSharedContext shared_context = factory_->Create(grpc_manager);

  absl::StatusOr<std::shared_ptr<grpc::ClientContext>> client_context_status =
      shared_context.GetCancellableClientContext();

  EXPECT_FALSE(client_context_status.ok());
  EXPECT_EQ(client_context_status.status().code(),
            absl::StatusCode::kCancelled);
  EXPECT_EQ(client_context_status.status().message(),
            "Request has been cancelled.");
}

TEST_F(RomaRequestSharedContextGrpcManagerTest,
       GetCancellableClientContext_WithTimeout) {
  auto grpc_manager = std::make_shared<CancellableGrpcContextManager>();

  RomaRequestSharedContext shared_context = factory_->Create(grpc_manager);

  std::optional<std::int64_t> test_timeout_ms = 100;

  auto start_time = absl::Now();

  absl::StatusOr<std::shared_ptr<grpc::ClientContext>> client_context_status =
      shared_context.GetCancellableClientContext(test_timeout_ms);

  EXPECT_TRUE(client_context_status.ok());
  std::shared_ptr<grpc::ClientContext> client_context = *client_context_status;
  ASSERT_NE(client_context, nullptr);

  auto expected_deadline_approx =
      start_time + absl::Milliseconds(*test_timeout_ms);
  auto actual_deadline_chrono = client_context->deadline();
  auto actual_deadline_absl = absl::FromChrono(actual_deadline_chrono);

  EXPECT_GE(actual_deadline_absl,
            expected_deadline_approx - absl::Milliseconds(5));
  EXPECT_LE(actual_deadline_absl,
            expected_deadline_approx + absl::Milliseconds(5));
}

TEST_F(RomaRequestSharedContextGrpcManagerTest,
       GetCancellableClientContext_NoTimeout) {
  auto grpc_manager = std::make_shared<CancellableGrpcContextManager>();

  RomaRequestSharedContext shared_context = factory_->Create(grpc_manager);

  absl::StatusOr<std::shared_ptr<grpc::ClientContext>> client_context_status =
      shared_context.GetCancellableClientContext(absl::nullopt);

  EXPECT_TRUE(client_context_status.ok());
  std::shared_ptr<grpc::ClientContext> client_context = *client_context_status;
  ASSERT_NE(client_context, nullptr);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
