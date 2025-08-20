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

#include "services/common/metric/roma_metric_utils.h"

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/roma/interface/metrics.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(RomaMetricsTest, CheckRomaMetricsNames) {
  EXPECT_EQ(google::scp::roma::kExecutionMetricDurationMs,
            metric::kRawRomaExecutionDuration);
  EXPECT_EQ(google::scp::roma::kExecutionMetricQueueFullnessRatio,
            metric::kRawRomaExecutionQueueFullnessRatio);
  EXPECT_EQ(google::scp::roma::kExecutionMetricActiveWorkerRatio,
            metric::kRawRomaExecutionActiveWorkerRatio);
  EXPECT_EQ(google::scp::roma::kExecutionMetricWaitTimeMs,
            metric::kRawRomaExecutionWaitTime);
  EXPECT_EQ(google::scp::roma::kExecutionMetricJsEngineCallDurationMs,
            metric::kRawRomaExecutionCodeRunDuration);
  EXPECT_EQ(google::scp::roma::kInputParsingMetricJsEngineDurationMs,
            metric::kRawRomaExecutionJsonInputParsingDuration);
  EXPECT_EQ(google::scp::roma::kHandlerCallMetricJsEngineDurationMs,
            metric::kRawRomaExecutionJsEngineHandlerCallDuration);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
