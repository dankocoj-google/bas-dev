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

// Run the benchmark as follows:
// builders/tools/bazel-debian run --dynamic_mode=off -c opt --copt=-gmlt \
//   --copt=-fno-omit-frame-pointer --fission=yes --strip=never \
//   services/bidding_service/benchmarking:browser_signals_benchmark -- \
//   --benchmark_time_unit=us --benchmark_repetitions=10

#include "benchmark/benchmark.h"
#include "services/bidding_service/utils/browser_signals_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestPublisher[] = "publisher.com";
constexpr char kTestTopLevelSeller[] = "top_level_seller_origin";
constexpr uint32_t kTestDataVersion = 1989;
constexpr uint32_t kTestInt = 3;
constexpr char kTestPrevWins[] = "[[1, \"1234\"], [1, \"1234\"]]";

static void BM_CreateBrowserSignals(benchmark::State& state) {
  // Setup.
  ForDebuggingOnlyFlags fdo_flags;
  fdo_flags.set_in_cooldown_or_lockout(true);

  BrowserSignalsForBidding browser_signals;
  browser_signals.set_join_count(kTestInt);
  browser_signals.set_bid_count(kTestInt);
  browser_signals.set_recency(kTestInt);
  browser_signals.set_prev_wins(kTestPrevWins);
  browser_signals.set_prev_wins_ms(kTestPrevWins);
  browser_signals.set_recency_ms(kTestInt);

  // Tuple of time-ad pairs for a previous win for this interest group
  // that occurred in the last 30 days. The time is specified in milliseconds
  // before the containing auctionBlob was requested.
  for (auto _ : state) {
    // This code gets timed.
    MakeBrowserSignalsForScript(kTestPublisher, kTestTopLevelSeller,
                                kTestTopLevelSeller, browser_signals,
                                kTestDataVersion, kTestInt, fdo_flags);
  }
}

// Register the function as a benchmark
BENCHMARK(BM_CreateBrowserSignals);

}  // namespace
// Run the benchmark
BENCHMARK_MAIN();
}  // namespace privacy_sandbox::bidding_auction_servers
