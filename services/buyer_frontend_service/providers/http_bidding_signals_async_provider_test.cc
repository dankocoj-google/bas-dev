//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/buyer_frontend_service/providers/http_bidding_signals_async_provider.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/common/clients/http_kv_server/buyer/buyer_key_value_async_http_client.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::An;

// Experiment Group ID.
constexpr int kEgId = 1689;

GetBidsRequest::GetBidsRawRequest GetRequest(bool set_buyer_kv_egid = false) {
  GetBidsRequest::GetBidsRawRequest request;
  if (set_buyer_kv_egid) {
    request.set_buyer_kv_experiment_group_id(kEgId);
  }
  for (int i = 0; i < MakeARandomInt(1, 10); i++) {
    request.mutable_buyer_input()->mutable_interest_groups()->AddAllocated(
        MakeARandomInterestGroupFromBrowser().release());
  }
  return request;
}

TEST(HttpBiddingSignalsAsyncProviderTest, MapsMissingClientTypeToUnknown) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetBuyerValuesInput, GetBuyerValuesOutput>>();
  // The IGs created by this function have both names and bidding_signals_keys.
  auto request = GetRequest(/*set_buyer_kv_egid=*/true);
  request.clear_client_type();
  absl::Notification notification;
  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetBuyerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetBuyerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce(
          [&request, &notification](
              std::unique_ptr<GetBuyerValuesInput> input,
              const RequestMetadata& metadata,
              absl::AnyInvocable<void(
                  absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>)&&>
                  callback,
              absl::Duration timeout) {
            EXPECT_EQ(input->client_type, ClientType::CLIENT_TYPE_UNKNOWN);

            EXPECT_TRUE(request.has_buyer_kv_experiment_group_id());
            EXPECT_EQ(input->buyer_kv_experiment_group_id, absl::StrCat(kEgId));
            EXPECT_EQ(input->buyer_kv_experiment_group_id,
                      absl::StrCat(request.buyer_kv_experiment_group_id()));
            notification.Notify();
            return absl::OkStatus();
          });

  HttpBiddingSignalsAsyncProvider class_under_test(std::move(mock_client));

  BiddingSignalsRequest bidding_signals_request(request, {});
  class_under_test.Get(
      bidding_signals_request,
      [](absl::StatusOr<std::unique_ptr<BiddingSignals>> signals,
         GetByteSize get_byte_size) {},
      absl::Milliseconds(100));
  notification.WaitForNotification();
}

TEST(HttpBiddingSignalsAsyncProviderTest, MapsGetBidKeysToBuyerValuesInput) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetBuyerValuesInput, GetBuyerValuesOutput>>();
  auto request = GetRequest();
  request.set_client_type(ClientType::CLIENT_TYPE_BROWSER);
  absl::Notification notification;

  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetBuyerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetBuyerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce(
          [&request, &notification](
              std::unique_ptr<GetBuyerValuesInput> input,
              const RequestMetadata& metadata,
              absl::AnyInvocable<void(
                  absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>)&&>
                  callback,
              absl::Duration timeout) {
            // Check that keys received in input are from buyer_input
            EXPECT_EQ(input->hostname, request.publisher_name());
            EXPECT_EQ(input->client_type, ClientType::CLIENT_TYPE_BROWSER);

            EXPECT_FALSE(request.has_buyer_kv_experiment_group_id());
            EXPECT_EQ(request.buyer_kv_experiment_group_id(), 0);
            EXPECT_EQ(input->buyer_kv_experiment_group_id, "");

            // Collect all expected keys
            UrlKeysSet expected_keys;
            UrlKeysSet expected_ig_names;
            for (const auto& interest_group :
                 request.buyer_input().interest_groups()) {
              expected_ig_names.emplace(interest_group.name());
              expected_keys.insert(
                  interest_group.bidding_signals_keys().begin(),
                  interest_group.bidding_signals_keys().end());
            }

            // All expected keys in received keys and vice versa
            std::vector<absl::string_view> keys_diff;
            absl::c_set_difference(input->keys, expected_keys,
                                   std::back_inserter(keys_diff));
            EXPECT_TRUE(keys_diff.empty());
            // All expected IG names in received names and vice versa
            std::vector<absl::string_view> ig_names_diff;
            absl::c_set_difference(input->interest_group_names,
                                   expected_ig_names,
                                   std::back_inserter(ig_names_diff));
            EXPECT_TRUE(ig_names_diff.empty());
            notification.Notify();
            return absl::OkStatus();
          });

  HttpBiddingSignalsAsyncProvider class_under_test(std::move(mock_client));

  BiddingSignalsRequest bidding_signals_request(request, {});
  class_under_test.Get(
      bidding_signals_request,
      [](absl::StatusOr<std::unique_ptr<BiddingSignals>> signals,
         GetByteSize get_byte_size) {},
      absl::Milliseconds(100));
  notification.WaitForNotification();
}

TEST(HttpBiddingSignalsAsyncProviderTest, MapsBuyerValuesAsyncClientError) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetBuyerValuesInput, GetBuyerValuesOutput>>();
  auto request = GetRequest();
  absl::Notification notification;

  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetBuyerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetBuyerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce([](std::unique_ptr<GetBuyerValuesInput> input,
                   const RequestMetadata& metadata,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>)&&>
                       callback,
                   absl::Duration timeout) {
        (std::move(callback))(absl::InternalError(""));
        return absl::OkStatus();
      });

  HttpBiddingSignalsAsyncProvider class_under_test(std::move(mock_client));

  BiddingSignalsRequest bidding_signals_request(request, {});
  class_under_test.Get(
      bidding_signals_request,
      [&notification](absl::StatusOr<std::unique_ptr<BiddingSignals>> signals,
                      GetByteSize get_byte_size) {
        EXPECT_EQ(signals.status().code(), absl::StatusCode::kInternal);
        notification.Notify();
      },
      absl::Milliseconds(100));
  notification.WaitForNotification();
}

TEST(HttpBiddingSignalsAsyncProviderTest,
     MapsBuyerValuesAsyncClientResponseToBiddingSignals) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetBuyerValuesInput, GetBuyerValuesOutput>>();
  auto request = GetRequest();
  request.mutable_buyer_input()->mutable_interest_groups()->AddAllocated(
      MakeARandomInterestGroupFromBrowser().release());
  absl::Notification notification;
  std::string expected_output = MakeARandomString();

  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetBuyerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetBuyerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce(
          [&expected_output](
              std::unique_ptr<GetBuyerValuesInput> input,
              const RequestMetadata& metadata,
              absl::AnyInvocable<void(
                  absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>)&&>
                  callback,
              absl::Duration timeout) {
            auto output = std::make_unique<GetBuyerValuesOutput>();
            output->result = expected_output;
            (std::move(callback))(std::move(output));
            return absl::OkStatus();
          });

  HttpBiddingSignalsAsyncProvider class_under_test(std::move(mock_client));

  BiddingSignalsRequest bidding_signals_request(request, {});
  class_under_test.Get(
      bidding_signals_request,
      [&expected_output, &notification](
          absl::StatusOr<std::unique_ptr<BiddingSignals>> signals,
          GetByteSize get_byte_size) {
        EXPECT_EQ(*(signals.value()->trusted_signals), expected_output);
        notification.Notify();
      },
      absl::Milliseconds(100));
  notification.WaitForNotification();
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
