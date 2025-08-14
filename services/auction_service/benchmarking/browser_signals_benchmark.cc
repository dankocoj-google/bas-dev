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
//   services/auction_service/benchmarking:browser_signals_benchmark -- \
//   --benchmark_time_unit=us --benchmark_repetitions=10

#include "benchmark/benchmark.h"
#include "services/auction_service/utils/proto_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestPublisher[] = "publisher.com";
constexpr char kTestIGOwner[] = "interest_group_owner";
constexpr char kTestRenderUrl[] = "https://render_url";
constexpr char kTestTopLevelSeller[] = "top_level_seller_origin";
constexpr char kTestAdComponentUrl_1[] = "https://component_1";
constexpr char kTestAdComponentUrl_2[] = "https://component_2";
constexpr char kTestBidCurrency[] = "ABC";
constexpr uint32_t kTestSellerDataVersion = 1989;
constexpr char kTestBuyerReportingId[] = "testBuyerReportingId";
constexpr char kTestBuyerAndSellerReportingId[] =
    "testBuyerAndSellerReportingId";
constexpr char kTestSelectedReportingId[] =
    "testSelectedBuyerAndSellerReportingId";

static void BM_CreateBrowserSignals(benchmark::State& state) {
  // Setup.
  ReportingIdsParamForBidMetadata reporting_ids = {
      .buyer_reporting_id = kTestBuyerReportingId,
      .buyer_and_seller_reporting_id = kTestBuyerAndSellerReportingId,
      .selected_buyer_and_seller_reporting_id = kTestSelectedReportingId};
  ForDebuggingOnlyFlags fdo_flags;
  fdo_flags.set_in_cooldown_or_lockout(true);
  google::protobuf::RepeatedPtrField<std::string> component_urls;
  component_urls.Add(kTestAdComponentUrl_1);
  component_urls.Add(kTestAdComponentUrl_2);
  for (auto _ : state) {
    // This code gets timed.
    MakeBidMetadata(kTestPublisher, kTestIGOwner, kTestRenderUrl,
                    component_urls, kTestTopLevelSeller, kTestBidCurrency,
                    kTestSellerDataVersion, reporting_ids, fdo_flags);
  }
}

// Register the function as a benchmark
BENCHMARK(BM_CreateBrowserSignals);

}  // namespace
// Run the benchmark
BENCHMARK_MAIN();
}  // namespace privacy_sandbox::bidding_auction_servers
