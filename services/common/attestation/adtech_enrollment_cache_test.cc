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

#include "services/common/attestation/adtech_enrollment_cache.h"

#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "api/attestation.pb.h"
#include "google/protobuf/text_format.h"
#include "include/gtest/gtest.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsGatedAPIProto;
using ::wireless::android::adservices::mdd::adtech_enrollment::chrome::
    PrivacySandboxAttestationsProto;
using PrivacySandboxAttestedAPIsProto =
    PrivacySandboxAttestationsProto::PrivacySandboxAttestedAPIsProto;

inline constexpr char kTestEnrolledSite1[] = "https://ad-site.net";
inline constexpr char kTestEnrolledSite2[] = "https://adtech-site.com";
inline constexpr char kTestUnenrolledSite1[] = "https://unenrolled.com";
inline constexpr char kTestUnenrolledSite2[] = "https://unenrolled-site.com";

class AdtechEnrollmentCacheTest : public ::testing::Test {
 public:
  void SetUp() {
    CommonTestInit();
    server_common::log::SetGlobalPSVLogLevel(20);
    cache_ = std::make_unique<AdtechEnrollmentCache>();
  }

 protected:
  std::unique_ptr<AdtechEnrollmentCache> cache_;
};

TEST_F(AdtechEnrollmentCacheTest, InsertsCorrectEnrolledSites) {
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
              value: { attested_apis: [ SHARED_STORAGE, TOPICS ] }
            }
            sites_attested_for_all_apis: "%s"
          )pb",
          kTestEnrolledSite1, kTestUnenrolledSite1, kTestEnrolledSite2),
      &attestation_proto));
  auto proto_ptr = std::make_unique<const PrivacySandboxAttestationsProto>(
      attestation_proto);

  cache_->Refresh(std::move(proto_ptr));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite1));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite2));
  EXPECT_FALSE(cache_->Query(kTestUnenrolledSite1));
}

TEST_F(AdtechEnrollmentCacheTest, RemovesUnenrolledSites) {
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
            sites_attested_for_all_apis: "%s"
            sites_attested_for_all_apis: "%s"
            sites_attested_for_all_apis: "%s"
            sites_attested_for_all_apis: "%s"
          )pb",
          kTestEnrolledSite1, kTestEnrolledSite2, kTestUnenrolledSite1,
          kTestUnenrolledSite2),
      &attestation_proto));
  auto initial_ptr = std::make_unique<const PrivacySandboxAttestationsProto>(
      attestation_proto);

  cache_->Refresh(std::move(initial_ptr));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite1));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite2));
  EXPECT_TRUE(cache_->Query(kTestUnenrolledSite1));
  EXPECT_TRUE(cache_->Query(kTestUnenrolledSite2));

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
              value: { attested_apis: [ SHARED_STORAGE, TOPICS ] }
            }
            sites_attested_for_all_apis: "%s"
          )pb",
          kTestEnrolledSite1, kTestUnenrolledSite1, kTestEnrolledSite2),
      &attestation_proto));
  auto updated_ptr = std::make_unique<const PrivacySandboxAttestationsProto>(
      attestation_proto);

  cache_->Refresh(std::move(updated_ptr));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite1));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite2));
  EXPECT_FALSE(cache_->Query(kTestUnenrolledSite1));
  EXPECT_FALSE(cache_->Query(kTestUnenrolledSite2));
}

TEST_F(AdtechEnrollmentCacheTest, InsertsNewlyEnrolledSites) {
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
              value: { attested_apis: [ SHARED_STORAGE, TOPICS ] }
            }
            sites_attested_for_all_apis: "%s"
          )pb",
          kTestEnrolledSite1, kTestUnenrolledSite1, kTestEnrolledSite2),
      &attestation_proto));

  auto initial_ptr = std::make_unique<const PrivacySandboxAttestationsProto>(
      attestation_proto);

  cache_->Refresh(std::move(initial_ptr));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite1));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite2));
  EXPECT_FALSE(cache_->Query(kTestUnenrolledSite1));
  EXPECT_FALSE(cache_->Query(kTestUnenrolledSite2));

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
            sites_attested_for_all_apis: "%s"
          )pb",
          kTestEnrolledSite1, kTestEnrolledSite2, kTestUnenrolledSite1,
          kTestUnenrolledSite2),
      &attestation_proto));

  auto updated_ptr = std::make_unique<const PrivacySandboxAttestationsProto>(
      attestation_proto);

  cache_->Refresh(std::move(updated_ptr));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite1));
  EXPECT_TRUE(cache_->Query(kTestEnrolledSite2));
  EXPECT_TRUE(cache_->Query(kTestUnenrolledSite1));
  EXPECT_TRUE(cache_->Query(kTestUnenrolledSite2));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
