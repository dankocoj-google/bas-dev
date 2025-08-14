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

#ifndef SERVICES_COMMON_METRIC_ROMA_METRIC_UTILS_H_
#define SERVICES_COMMON_METRIC_ROMA_METRIC_UTILS_H_

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
#include "services/common/metric/server_definition.h"

namespace privacy_sandbox::bidding_auction_servers {

// Retrieves metric values with metric name as key. Updates with maximum value
// among a batch execution.
template <typename T>
void UpdateMaxMetric(const absl::flat_hash_map<std::string, double>& metrics,
                     const std::string& key, T& value) {
  auto it = metrics.find(key);
  if (it != metrics.end()) {
    value = std::max(value, it->second);
  }
}

// Extracts Roma metrics from the dispatch response and logs the corresponding
// metrics in B&A.
template <typename ContextT>
void LogRomaMetrics(const std::vector<absl::StatusOr<DispatchResponse>>& result,
                    ContextT* metric_context) {
  double execution_duration_ms = 0;
  double max_queueing_duration_ms = 0;
  double code_run_duration_ms = 0;
  double json_input_parsing_duration_ms = 0;
  double js_engine_handler_call_duration_ms = 0;
  double queue_fullness_ratio = 0;
  double active_worker_ratio = 0;

  for (const auto& res : result) {
    if (res.ok()) {
      const auto& metrics = res.value().metrics;
      UpdateMaxMetric(metrics, metric::kRawRomaExecutionDuration,
                      execution_duration_ms);
      UpdateMaxMetric(metrics, metric::kRawRomaExecutionQueueFullnessRatio,
                      queue_fullness_ratio);
      UpdateMaxMetric(metrics, metric::kRawRomaExecutionActiveWorkerRatio,
                      active_worker_ratio);

      UpdateMaxMetric(metrics, metric::kRawRomaExecutionWaitTime,
                      max_queueing_duration_ms);
      UpdateMaxMetric(metrics, metric::kRawRomaExecutionCodeRunDuration,
                      code_run_duration_ms);
      UpdateMaxMetric(metrics,
                      metric::kRawRomaExecutionJsonInputParsingDuration,
                      json_input_parsing_duration_ms);
      UpdateMaxMetric(metrics,
                      metric::kRawRomaExecutionJsEngineHandlerCallDuration,
                      js_engine_handler_call_duration_ms);
    }
  }
  LogIfError(
      metric_context->template LogHistogram<metric::kRomaExecutionDuration>(
          static_cast<int>(execution_duration_ms)));
  LogIfError(
      metric_context
          ->template LogHistogram<metric::kRomaExecutionQueueFullnessRatio>(
              queue_fullness_ratio));
  LogIfError(
      metric_context
          ->template LogHistogram<metric::kRomaExecutionActiveWorkerRatio>(
              active_worker_ratio));
  LogIfError(metric_context
                 ->template LogHistogram<metric::kUdfExecutionQueueingDuration>(
                     static_cast<int>(max_queueing_duration_ms)));
  LogIfError(metric_context
                 ->template LogHistogram<metric::kRomaExecutionCodeRunDuration>(
                     static_cast<int>(code_run_duration_ms)));
  LogIfError(metric_context->template LogHistogram<
             metric::kRomaExecutionJsonInputParsingDuration>(
      static_cast<int>(json_input_parsing_duration_ms)));
  LogIfError(metric_context->template LogHistogram<
             metric::kRomaExecutionJsEngineHandlerCallDuration>(
      static_cast<int>(js_engine_handler_call_duration_ms)));
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_METRIC_ROMA_METRIC_UTILS_H_
