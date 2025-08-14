/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_CANCELLATION_UTILS_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_CANCELLATION_UTILS_H_

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/server_context.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// This is a set of classes that handles downstream cancellation
// in inference modules. It provides a common interface for observing
// cancellation signals and is intended to be used with adapter
// pattern for compatibility and extensibility to various fixture types.

// Base class for cancellation context. All others inherit from it.
class CancellableServerContext {
 public:
  virtual ~CancellableServerContext() = default;
  virtual bool IsCancelled() const = 0;
};

// A context that does not support cancellation, utilized as the default
// argument for the module interface. This is to avoid refactoring
// legacy unit tests.
class EmptyCancellableServerContext : public CancellableServerContext {
 public:
  // This context does not support cancellation.
  bool IsCancelled() const override { return false; }
};

// A mock context that enables us to set the cancelled behavior. We make
// use of this in unit testing.
class MockCancellableContext : public CancellableServerContext {
 public:
  // Constructor to set the desired cancellation state for the test.
  explicit MockCancellableContext(bool is_cancelled)
      : is_cancelled_(is_cancelled) {}

  // The overridden method returns the value you set.
  bool IsCancelled() const override { return is_cancelled_; }

 private:
  bool is_cancelled_;
};

// A context adapter that wraps a gRPC ServerContextBase and provides
// a CancellableServerContext interface. See grpc_sidecar.cc for usage.
// It is intended that cancellation_enabled_ is provided by a flag.
class GRPCContextAdapter : public CancellableServerContext {
 public:
  // Takes a non-owning pointer to the real gRPC context.
  explicit GRPCContextAdapter(const grpc::ServerContextBase& server_context,
                              bool cancellation_enabled = true)
      : context_(server_context), cancellation_enabled_(cancellation_enabled) {}

  // Delegates the call to the real gRPC context.
  bool IsCancelled() const override {
    return cancellation_enabled_ && context_.IsCancelled();
  }

 private:
  const grpc::ServerContextBase& context_;
  bool cancellation_enabled_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference
/**
 * An enumeration of code locations to identify where a cancellation
 * was detected. This provides more granular debugging information.
 */
enum class CancelLocation {
  // --- GRPC Service Layer ---
  kPredictEnter = 1,
  kRegModelEnter = 2,
  kDelModelEnter = 3,
  kGetPathsEnter = 4,

  // --- Module Predict  ---
  kPredictLoopStart = 10,
  kPredictTaskDispatch = 11,
  kPredictResults = 12,
  kPredictLogic = 13,
  kPredictAsync = 14,

  // --- Module Other  ---
  kRegModelLogic = 20,
  kDelModelLogic = 21,
  kModelPutPreLock = 22,
  kModelPutPostLock = 23,
};

/**
 * Checks if the gRPC context is cancelled. If it is, this macro
 * returns an `absl::CancelledError` with a message that includes
 * a specific location identifier.
 */
#define RETURN_IF_CANCELLED(context, location)                         \
  do {                                                                 \
    if (ABSL_PREDICT_FALSE((context).IsCancelled())) {                 \
      return absl::CancelledError(                                     \
          absl::StrCat("Request cancelled by client. Location code: ", \
                       static_cast<int>(location)));                   \
    }                                                                  \
  } while (0)

// Previous macro, but returns a gRPC status instead of an absl::Status.
#define RETURN_GRPC_IF_CANCELLED(context, location, reactor)              \
  do {                                                                    \
    if (ABSL_PREDICT_FALSE((context).IsCancelled())) {                    \
      (reactor)->Finish(grpc::Status(grpc::StatusCode::CANCELLED,         \
                                     "Request was cancelled by client")); \
      return reactor;                                                     \
    }                                                                     \
  } while (0)

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_CANCELLATION_UTILS_H_
