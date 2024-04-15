//   Copyright 2022 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include "services/buyer_frontend_service/util/buyer_frontend_test_utils.h"

#include <gmock/gmock-matchers.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <include/gmock/gmock-actions.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/buyer_frontend_service/providers/bidding_signals_async_provider.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::testing::_;
using ::testing::An;
using ::testing::AnyNumber;

void SetupBiddingProviderMock(
    const MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>& provider,
    const std::optional<std::string>& bidding_signals_value,
    bool repeated_get_allowed,
    const std::optional<absl::Status>& server_error_to_return,
    bool match_any_params_any_times) {
  auto MockBiddingSignalsProvider =
      [bidding_signals_value, server_error_to_return](
          const BiddingSignalsRequest& bidding_signals_request,
          absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<BiddingSignals>>,
                   GetByteSize) &&>
              on_done,
          absl::Duration timeout) {
        GetByteSize get_byte_size;
        if (server_error_to_return.has_value()) {
          std::move(on_done)(*server_error_to_return, get_byte_size);
        } else {
          auto bidding_signals = std::make_unique<BiddingSignals>();
          if (bidding_signals_value.has_value()) {
            bidding_signals->trusted_signals =
                std::make_unique<std::string>(bidding_signals_value.value());
          }
          std::move(on_done)(std::move(bidding_signals), get_byte_size);
        }
      };
  if (match_any_params_any_times) {
    EXPECT_CALL(provider, Get(_, _, _))
        .Times(AnyNumber())
        .WillOnce(MockBiddingSignalsProvider);
  } else if (repeated_get_allowed) {
    EXPECT_CALL(provider,
                Get(An<const BiddingSignalsRequest&>(),
                    An<absl::AnyInvocable<
                        void(absl::StatusOr<std::unique_ptr<BiddingSignals>>,
                             GetByteSize) &&>>(),
                    An<absl::Duration>()))
        .WillRepeatedly(MockBiddingSignalsProvider);
  } else {
    EXPECT_CALL(provider,
                Get(An<const BiddingSignalsRequest&>(),
                    An<absl::AnyInvocable<
                        void(absl::StatusOr<std::unique_ptr<BiddingSignals>>,
                             GetByteSize) &&>>(),
                    An<absl::Duration>()))
        .WillOnce(MockBiddingSignalsProvider);
  }
}

std::unique_ptr<MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>
SetupBiddingProviderMock(
    const std::optional<std::string>& bidding_signals_value,
    bool repeated_get_allowed,
    const std::optional<absl::Status>& server_error_to_return) {
  auto provider = std::make_unique<
      MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>();
  // De-referencing the uPtr gives us the object. We can take a reference of it
  // since we know the object will outlive its reference and will not be moved
  // or modified while this method runs.
  SetupBiddingProviderMock(*provider, bidding_signals_value,
                           repeated_get_allowed, server_error_to_return);
  return provider;
}

}  // namespace privacy_sandbox::bidding_auction_servers
