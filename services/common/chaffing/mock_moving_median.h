// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing per21missions and
// limitations under the License.

#include <gmock/gmock.h>

#include "include/gtest/gtest.h"
#include "services/common/chaffing/moving_median.h"

namespace privacy_sandbox::bidding_auction_servers {

class MockMovingMedian : public MovingMedian {
 public:
  MockMovingMedian() : MovingMedian(1, 0.0f) {}

  MOCK_METHOD(void, AddNumber, (RandomNumberGenerator&, int), ());
  MOCK_METHOD(absl::StatusOr<int>, GetMedian, (), (const));
  MOCK_METHOD(bool, IsWindowFilled, (), (const));
};

}  // namespace privacy_sandbox::bidding_auction_servers
