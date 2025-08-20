/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/common/attestation/adtech_enrollment_fetcher.h"

#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "api/attestation.pb.h"
#include "google/protobuf/text_format.h"
#include "include/gtest/gtest.h"
#include "services/common/attestation/adtech_enrollment_cache.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "services/seller_frontend_service/k_anon/k_anon_utils.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsGatedAPIProto;
using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsProto;
using PrivacySandboxAttestedAPIsProto =
    PrivacySandboxAttestationsProto::PrivacySandboxAttestedAPIsProto;
using ::google::scp::core::test::EqualsProto;

inline constexpr char kTestUrlEndpoint[] = "test_endpoint";
constexpr absl::Duration kTestFetchPeriod = absl::Minutes(2);
constexpr absl::Duration kTestTimeOut = absl::Minutes(1);
inline constexpr char kTestEnrolledSite1[] = "https://ad-site.net";
inline constexpr char kTestEnrolledSite2[] = "https://adtech-site.com";
inline constexpr char kTestEnrolledSite3[] = "https://site.com";
inline constexpr char kTestUnenrolledSite[] = "https://unenrolled.com";

class AdtechEnrollmentFetcherTest : public ::testing::Test {
 public:
  void SetUp() override {
    CommonTestInit();
    server_common::log::SetGlobalPSVLogLevel(20);
    mock_http_fetcher_ = std::make_unique<MockHttpFetcherAsync>();
    mock_executor_ = std::make_unique<MockExecutor>();
  }

 protected:
  std::unique_ptr<MockHttpFetcherAsync> mock_http_fetcher_;
  std::unique_ptr<MockExecutor> mock_executor_;
};

TEST_F(AdtechEnrollmentFetcherTest, SharedCacheCanBeAttested) {
  auto cache = std::make_unique<AdtechEnrollmentCache>();

  // Make a binary proto string from message.
  PrivacySandboxAttestationsProto attestation_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::StrFormat(
          R"pb(
            all_apis: [
              ATTRIBUTION_REPORTING,
              PRIVATE_AGGREGATION,
              PROTECTED_AUDIENCE,
              SHARED_STORAGE,
              TOPICS
            ]
            site_attestations: {
              key: "%s"
              value: {
                attested_apis: [ PRIVATE_AGGREGATION, PROTECTED_AUDIENCE ]
              }
            }
            site_attestations: {
              key: "%s"
              value: { attested_apis: [ PROTECTED_AUDIENCE ] }
            }
            site_attestations: {
              key: "%s"
              value: { attested_apis: [ SHARED_STORAGE, TOPICS ] }
            }
            sites_attested_for_all_apis: "%s"
          )pb",
          kTestEnrolledSite1, kTestEnrolledSite2, kTestUnenrolledSite,
          kTestEnrolledSite3),
      &attestation_proto));
  std::string binary_string;
  attestation_proto.SerializeToString(&binary_string);

  absl::Notification http_fetch_done;
  EXPECT_CALL(*mock_http_fetcher_, FetchUrls)
      .Times(1)
      .WillOnce([&http_fetch_done, &binary_string](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<std::string>>)&&>
                        done_callback) {
        EXPECT_EQ(timeout, kTestTimeOut);
        std::move(done_callback)({std::move(binary_string)});
        http_fetch_done.Notify();
      });

  EXPECT_CALL(*mock_executor_, RunAfter)
      .WillOnce(
          [](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kTestFetchPeriod);
            return server_common::TaskId{};
          });

  EXPECT_CALL(*mock_executor_, Cancel);

  std::unique_ptr<AdtechEnrollmentFetcher> adtech_enrollment_fetcher =
      std::make_unique<AdtechEnrollmentFetcher>(
          kTestUrlEndpoint, kTestFetchPeriod, mock_http_fetcher_.get(),
          mock_executor_.get(), kTestTimeOut, cache.get());

  auto status = adtech_enrollment_fetcher->Start();
  ASSERT_TRUE(status.ok()) << status;
  http_fetch_done.WaitForNotification();
  adtech_enrollment_fetcher->End();

  // Check that cache can be queried outside of the fetcher object as intended
  // for attestation purpose.
  EXPECT_TRUE(cache->Query(kTestEnrolledSite1));
  EXPECT_TRUE(cache->Query(kTestEnrolledSite2));
  EXPECT_TRUE(cache->Query(kTestEnrolledSite3));
  EXPECT_FALSE(cache->Query(kTestUnenrolledSite));
}

TEST_F(AdtechEnrollmentFetcherTest, UpdateCacheWithLatestList) {
  auto mock_cache = std::make_unique<AdtechEnrollmentCacheMock>();

  // Make a binary proto string from message.
  PrivacySandboxAttestationsProto initial_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::StrFormat(
          R"pb(
            all_apis: [
              ATTRIBUTION_REPORTING,
              PRIVATE_AGGREGATION,
              PROTECTED_AUDIENCE,
              SHARED_STORAGE,
              TOPICS
            ]
            sites_attested_for_all_apis: "%s"
            sites_attested_for_all_apis: "%s"
            sites_attested_for_all_apis: "%s"
          )pb",
          kTestEnrolledSite1, kTestEnrolledSite2, kTestUnenrolledSite),
      &initial_proto));
  std::string initial_list;
  initial_proto.SerializeToString(&initial_list);

  PrivacySandboxAttestationsProto updated_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::StrFormat(
          R"pb(
            all_apis: [
              ATTRIBUTION_REPORTING,
              PRIVATE_AGGREGATION,
              PROTECTED_AUDIENCE,
              SHARED_STORAGE,
              TOPICS
            ]
            site_attestations: {
              key: "%s"
              value: {
                attested_apis: [ PRIVATE_AGGREGATION, PROTECTED_AUDIENCE ]
              }
            }
            site_attestations: {
              key: "%s"
              value: { attested_apis: [ PROTECTED_AUDIENCE ] }
            }
            site_attestations: {
              key: "%s"
              value: { attested_apis: [ SHARED_STORAGE, TOPICS ] }
            }
          )pb",
          kTestEnrolledSite1, kTestEnrolledSite2, kTestUnenrolledSite),
      &updated_proto));
  std::string updated_list;
  updated_proto.SerializeToString(&updated_list);

  absl::BlockingCounter done_fetch_url(2);
  int fetch_url_count = 0;
  EXPECT_CALL(*mock_http_fetcher_, FetchUrls)
      .Times(2)
      .WillRepeatedly(
          [&initial_list, &updated_list, &done_fetch_url, &fetch_url_count](
              const std::vector<HTTPRequest>& requests, absl::Duration timeout,
              absl::AnyInvocable<void(
                  std::vector<absl::StatusOr<std::string>>)&&>
                  done_callback) {
            EXPECT_EQ(timeout, kTestTimeOut);
            if (fetch_url_count == 0) {
              std::move(done_callback)({std::move(initial_list)});
              fetch_url_count++;
            } else {
              std::move(done_callback)({std::move(updated_list)});
            }
            done_fetch_url.DecrementCount();
          });

  absl::BlockingCounter done_refresh(2);
  int refresh_count = 0;
  EXPECT_CALL(*mock_cache, Refresh)
      .Times(2)
      .WillRepeatedly(
          [&initial_proto, &updated_proto, &done_refresh, &refresh_count](
              std::unique_ptr<const PrivacySandboxAttestationsProto> message) {
            if (refresh_count == 0) {
              EXPECT_THAT(*message, EqualsProto(initial_proto));
              refresh_count++;
            } else {
              EXPECT_THAT(*message, EqualsProto(updated_proto));
            }
            done_refresh.DecrementCount();
          });

  // Store the closure to be called later.
  absl::AnyInvocable<void()> stored_closure;
  EXPECT_CALL(*mock_executor_, RunAfter)
      .Times(2)
      .WillRepeatedly([&stored_closure](absl::Duration duration,
                                        absl::AnyInvocable<void()> closure) {
        EXPECT_EQ(duration, kTestFetchPeriod);
        stored_closure = std::move(closure);
        return server_common::TaskId{};
      });

  EXPECT_CALL(*mock_executor_, Cancel);

  std::unique_ptr<AdtechEnrollmentFetcher> adtech_enrollment_fetcher =
      std::make_unique<AdtechEnrollmentFetcher>(
          kTestUrlEndpoint, kTestFetchPeriod, mock_http_fetcher_.get(),
          mock_executor_.get(), kTestTimeOut, mock_cache.get());

  auto status = adtech_enrollment_fetcher->Start();
  ASSERT_TRUE(status.ok()) << status;

  // Call the stored closure to trigger the fetch.
  ASSERT_TRUE(stored_closure != nullptr);
  stored_closure();

  done_fetch_url.Wait();
  done_refresh.Wait();
  adtech_enrollment_fetcher->End();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
