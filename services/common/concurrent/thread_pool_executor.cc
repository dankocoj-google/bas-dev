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

#include "services/common/concurrent/thread_pool_executor.h"

#include <utility>

#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

ThreadPoolExecutor::ThreadPoolExecutor(int reserve_threads)
    : thread_pool_(std::make_shared<
                   grpc_event_engine::experimental::WorkStealingThreadPool>(
          reserve_threads)) {}

void ThreadPoolExecutor::Run(absl::AnyInvocable<void()> closure) {
  thread_pool_->Run(std::move(closure));
}

server_common::TaskId ThreadPoolExecutor::RunAfter(
    absl::Duration duration, absl::AnyInvocable<void()> closure) {
  PS_CHECK(false) << "ThreadPool does not support RunAfter.";
  return server_common::TaskId{};
}

bool ThreadPoolExecutor::Cancel(server_common::TaskId task_id) { return false; }

}  // namespace privacy_sandbox::bidding_auction_servers
