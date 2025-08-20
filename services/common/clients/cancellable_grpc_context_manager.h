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

#ifndef SERVICES_COMMON_CLIENTS_CANCELLABLE_GRPC_CONTEXT_MANAGER_H_
#define SERVICES_COMMON_CLIENTS_CANCELLABLE_GRPC_CONTEXT_MANAGER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <grpcpp/client_context.h>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace privacy_sandbox::bidding_auction_servers {

/**
 * Manages the lifecycle of multiple grpc::ClientContext objects for a
 * single logical request.
 *
 * This class is designed to be used in scenarios where a primary request (e.g.,
 * a GenerateBids RPC) may spawn multiple outbound gRPC calls. If the primary
 * request is cancelled, this class provides a mechanism to forward the
 * cancellation to all outbound calls.
 *
 * The lifetime of this class should be tied to the lifetime of the
 * primary request, i.e. a gRPC reactor, not the lifetime of a gRPC
 * call, gRPC client, or gRPC service.
 *
 * This class is thread-safe.
 */
class CancellableGrpcContextManager {
 public:
  CancellableGrpcContextManager() = default;

  // This class is non-copyable and non-movable because it contains a mutex
  // and an atomic.
  CancellableGrpcContextManager(const CancellableGrpcContextManager&) = delete;
  CancellableGrpcContextManager& operator=(
      const CancellableGrpcContextManager&) = delete;
  CancellableGrpcContextManager(CancellableGrpcContextManager&&) = delete;
  CancellableGrpcContextManager& operator=(CancellableGrpcContextManager&&) =
      delete;

  /**
   * Creates a new grpc::ClientContext, stores it for future
   * cancellation, and returns a shared pointer to it.
   *
   * If the context manager has already been cancelled via TryCancelAll(), this
   * method will return nullptr immediately without creating a new context.
   *
   * returns: A shared pointer to the newly created grpc::ClientContext, or
   * nullptr if the manager has been cancelled. While semantically the context
   * belongs to the manager, we use shared_ptr to mitigate potential
   * use-after-frees in gRPC core clean-up tasks.
   */
  std::shared_ptr<grpc::ClientContext> CreateClientContext() {
    absl::MutexLock lock(&client_contexts_mu_);
    if (is_cancelled_.load(std::memory_order_relaxed)) {
      return std::shared_ptr<grpc::ClientContext>(nullptr);
    }

    auto new_context = std::make_shared<grpc::ClientContext>();
    client_contexts_.push_back(new_context);
    return new_context;
  }

  /**
   * Attempts to cancel all client contexts created by this manager.
   *
   * This method sets an internal flag to prevent the creation of new contexts
   * and then iterates through all previously created contexts, calling
   * TryCancel() on each. This action is irreversible for this instance of the
   * manager. It is safe to call this method multiple times.
   */
  void TryCancelAll() {
    // Set the cancellation flag first. We use release-acquire semantics to
    // ensure that writes in this thread are visible to other threads that
    // read the flag. Note that this function is thread-safe, can be called
    // multiple times without failure. However, in the expected workflow,
    // it should only be called once during its lifetime by the thread that
    // owns the primary request.
    is_cancelled_.store(true, std::memory_order_release);

    absl::MutexLock lock(&client_contexts_mu_);
    for (const auto& context_ptr : client_contexts_) {
      if (context_ptr) {
        context_ptr->TryCancel();
      }
    }
  }

  /**
   * Checks if the context manager has been cancelled, returning
   * true if TryCancelAll() has been called, false otherwise.
   * This is primarily useful for testing and debugging.
   */
  bool IsCancelled() const {
    return is_cancelled_.load(std::memory_order_acquire);
  }

  /**
   * Returns the number of client contexts currently being managed.
   */
  size_t GetContextCount() {
    absl::MutexLock lock(&client_contexts_mu_);
    return client_contexts_.size();
  }

  // Debugging utility to report the completion of the gRPC
  // associated with a context.
  void ReportRPCFinish() {
    absl::MutexLock lock(&client_contexts_mu_);
    spent_context_count_++;
  }

  // Returns the number of client contexts that are still pending
  // (i.e., not yet reported as finished).
  int64_t GetPendingContextCount() {
    absl::MutexLock lock(&client_contexts_mu_);
    return client_contexts_.size() - spent_context_count_;
  }

 private:
  std::atomic<bool> is_cancelled_{false};
  absl::Mutex client_contexts_mu_;
  std::vector<std::shared_ptr<grpc::ClientContext>> client_contexts_
      ABSL_GUARDED_BY(client_contexts_mu_);
  int64_t spent_context_count_ ABSL_GUARDED_BY(client_contexts_mu_) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CANCELLABLE_GRPC_CONTEXT_MANAGER_H_
