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

#ifndef CONCURRENT_THREADPOOL_EXECUTOR_H_
#define CONCURRENT_THREADPOOL_EXECUTOR_H_

#include <memory>

#include "src/concurrent/executor.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"

namespace privacy_sandbox::bidding_auction_servers {

class ThreadPoolExecutor final : public server_common::Executor {
 public:
  explicit ThreadPoolExecutor(int reserve_threads);
  ~ThreadPoolExecutor() override = default;

  void Run(absl::AnyInvocable<void()> closure) override;

  // ThreadPool does not support RunAfter.
  server_common::TaskId RunAfter(absl::Duration duration,
                                 absl::AnyInvocable<void()> closure) override;

  // ThreadPool does not support Cancel.
  bool Cancel(server_common::TaskId task_id) override;

 private:
  std::shared_ptr<grpc_event_engine::experimental::ThreadPool> thread_pool_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // CONCURRENT_THREADPOOL_EXECUTOR_H_
