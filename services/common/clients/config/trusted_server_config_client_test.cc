//  Copyright 2025 Google LLC
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

#include "services/common/clients/config/trusted_server_config_client.h"

#include <future>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "src/public/cpio/interface/error_codes.h"
#include "src/public/cpio/interface/parameter_client/parameter_client_interface.h"
#include "src/public/cpio/mock/parameter_client/mock_parameter_client.h"

ABSL_FLAG(std::optional<std::string>, config_param_1, std::nullopt,
          "test flag 1");
ABSL_FLAG(std::optional<bool>, config_param_2, std::nullopt, "test flag 2");
ABSL_FLAG(std::optional<bool>, config_param_3, std::nullopt, "test flag 3");
ABSL_FLAG(std::optional<int32_t>, config_param_4, std::nullopt, "test flag 4");
ABSL_FLAG(std::optional<std::string>, config_param_5, "default value",
          "test flag with default val");
ABSL_FLAG(
    std::optional<privacy_sandbox::server_common::telemetry::TelemetryFlag>,
    config_param_6, std::nullopt, "metric flag");

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::cmrt::sdk::parameter_service::v1::GetParameterRequest;
using ::google::cmrt::sdk::parameter_service::v1::GetParameterResponse;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::cpio::Callback;
using ::google::scp::cpio::ParameterClientInterface;
using ::google::scp::cpio::ParameterClientOptions;
using ::testing::Return;

using ::google::scp::cpio::MockParameterClient;

constexpr absl::string_view kFlags[] = {"config_param_1", "config_param_2",
                                        "config_param_3", "config_param_4",
                                        "config_param_6"};

class MockParcParameterClient : public ParcParameterClient {
 public:
  // Default constructor that creates a dummy stub.
  MockParcParameterClient()
      : ParcParameterClient(
            std::make_unique<privacysandbox::apis::parc::v0::ParcService::Stub>(
                grpc::CreateChannel("dummy_target",
                                    grpc::InsecureChannelCredentials()))) {}

  MOCK_METHOD(absl::StatusOr<std::string>, GetParameterSync,
              (const std::string& parameter_name));
};

class TrustedServerConfigClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_parc_parameter_client_ = std::make_unique<MockParcParameterClient>();
    mock_cpio_parameter_client_ = std::make_unique<MockParameterClient>();
  }

  std::unique_ptr<MockParcParameterClient> mock_parc_parameter_client_;
  std::unique_ptr<MockParameterClient> mock_cpio_parameter_client_;
};

TEST_F(TrustedServerConfigClientTest, ParcClientCanInit) {
  std::vector<absl::string_view> flags = {"flag1"};
  TrustedServersConfigClient client(flags);

  EXPECT_CALL(*mock_parc_parameter_client_, GetParameterSync("prefix-flag1"))
      .WillOnce(Return("foo"));

  EXPECT_TRUE(
      client.Init("prefix-", std::move(mock_parc_parameter_client_)).ok());
  EXPECT_EQ(client.GetStringParameter("flag1"), "foo");
}

TEST_F(TrustedServerConfigClientTest, ParcClientWillNotErrorOnMissingFlag) {
  std::vector<absl::string_view> flags = {"flag1", "flag2"};
  TrustedServersConfigClient client(flags);

  EXPECT_CALL(*mock_parc_parameter_client_, GetParameterSync("flag1"))
      .WillOnce(Return("value1"));
  EXPECT_CALL(*mock_parc_parameter_client_, GetParameterSync("flag2"))
      .WillOnce(Return(absl::NotFoundError("Not found")));

  EXPECT_TRUE(client.Init("", std::move(mock_parc_parameter_client_)).ok());
}

TEST_F(TrustedServerConfigClientTest, CanReadFlagsPassedThroughConstructor) {
  absl::SetFlag(&FLAGS_config_param_1, "config_value_1");
  absl::SetFlag(&FLAGS_config_param_2, true);
  absl::SetFlag(&FLAGS_config_param_3, false);
  absl::SetFlag(&FLAGS_config_param_4, 100);

  server_common::telemetry::TelemetryFlag metric_flag;
  metric_flag.server_config.set_mode(
      server_common::telemetry::TelemetryConfig::PROD);
  absl::SetFlag(&FLAGS_config_param_6, metric_flag);

  std::vector<std::future<void>> f;

  EXPECT_CALL(*mock_cpio_parameter_client_, Init)
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, Run)
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, GetParameter)
      .WillRepeatedly([&f](const GetParameterRequest& get_param_req,
                           Callback<GetParameterResponse> callback) {
        // async reading parameter like the real case.
        f.push_back(
            std::async(std::launch::async, [cb = std::move(callback)]() {
              absl::SleepFor(absl::Milliseconds(100));  // simulate delay
              GetParameterResponse response;
              cb(FailureExecutionResult(
                     google::scp::core::errors::SC_CPIO_RESOURCE_NOT_FOUND),
                 response);
            }));
        return absl::OkStatus();
      });

  TrustedServersConfigClient config_client(kFlags);
  config_client.SetFlag(FLAGS_config_param_1, "config_param_1");
  config_client.SetFlag(FLAGS_config_param_2, "config_param_2");
  config_client.SetFlag(FLAGS_config_param_3, "config_param_3");
  config_client.SetFlag(FLAGS_config_param_4, "config_param_4");
  config_client.SetFlag(FLAGS_config_param_6, "config_param_6");

  ASSERT_TRUE(
      config_client.Init("", std::move(mock_cpio_parameter_client_)).ok());
  for (auto& each : f) {
    each.get();
  }

  EXPECT_EQ(config_client.GetStringParameter("config_param_1"),
            "config_value_1");
  EXPECT_EQ(config_client.GetBooleanParameter("config_param_2"), true);
  EXPECT_EQ(config_client.GetBooleanParameter("config_param_3"), false);
  EXPECT_EQ(config_client.GetIntParameter("config_param_4"), 100);
  EXPECT_EQ(config_client
                .GetCustomParameter<server_common::telemetry::TelemetryFlag>(
                    "config_param_6")
                .server_config.mode(),
            server_common::telemetry::TelemetryConfig::PROD);
}

TEST_F(TrustedServerConfigClientTest, FetchesConfigValueFromConfigClient) {
  // The values we expect the ADMC config client to return.
  absl::flat_hash_map<std::string, std::string> expected_param_values = {
      {"config_param_1", "config_value_1"}, {"config_param_2", "true"},
      {"config_param_3", "false"},          {"config_param_4", "4"},
      {"config_param_6", "mode: PROD"},
  };

  std::vector<std::future<void>> f;

  EXPECT_CALL(*mock_cpio_parameter_client_, Init)
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, Run)
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, GetParameter)
      .WillRepeatedly([&f, &expected_param_values](
                          GetParameterRequest get_param_req,
                          Callback<GetParameterResponse> callback) {
        // async reading parameter like the real case
        f.push_back(std::async(
            std::launch::async,
            [cb = std::move(callback), req = std::move(get_param_req),
             &expected_param_values]() {
              absl::SleepFor(absl::Milliseconds(100));  // simulate delay
              GetParameterResponse response;
              response.set_parameter_value(
                  expected_param_values.at(req.parameter_name()));
              cb(SuccessExecutionResult(), response);
            }));
        return absl::OkStatus();
      });

  TrustedServersConfigClient config_client(kFlags);
  ASSERT_TRUE(
      config_client.Init("", std::move(mock_cpio_parameter_client_)).ok());
  for (auto& each : f) {
    each.get();
  }
  EXPECT_EQ(config_client.GetStringParameter("config_param_1"),
            "config_value_1");
  EXPECT_EQ(config_client.GetBooleanParameter("config_param_2"), true);
  EXPECT_EQ(config_client.GetBooleanParameter("config_param_3"), false);
  EXPECT_EQ(config_client.GetIntParameter("config_param_4"), 4);
  EXPECT_EQ(config_client
                .GetCustomParameter<server_common::telemetry::TelemetryFlag>(
                    "config_param_6")
                .server_config.mode(),
            server_common::telemetry::TelemetryConfig::PROD);
}

TEST_F(TrustedServerConfigClientTest, OverwritesConfigValueFromCloud) {
  std::string key = "config_param_5";

  // The values we expect the ADMC config client to return.
  absl::flat_hash_map<std::string, std::string> expected_param_values = {
      {key, "config_value"},
  };
  std::vector<std::future<void>> f;

  EXPECT_CALL(*mock_cpio_parameter_client_, Init)
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, Run)
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, GetParameter)
      .WillRepeatedly([&f, &expected_param_values](
                          GetParameterRequest get_param_req,
                          Callback<GetParameterResponse> callback) {
        // async reading parameter like the real case
        f.push_back(std::async(
            std::launch::async,
            [cb = std::move(callback), req = std::move(get_param_req),
             &expected_param_values]() {
              absl::SleepFor(absl::Milliseconds(100));  // simulate delay
              GetParameterResponse response;
              response.set_parameter_value(
                  expected_param_values.at(req.parameter_name()));
              cb(SuccessExecutionResult(), response);
            }));
        return absl::OkStatus();
      });

  TrustedServersConfigClient config_client({key});
  config_client.SetFlag(FLAGS_config_param_5, "config_param_5");
  ASSERT_TRUE(
      config_client.Init("", std::move(mock_cpio_parameter_client_)).ok());
  for (auto& each : f) {
    each.get();
  }
  EXPECT_EQ(config_client.GetStringParameter(key), "config_value");
}

TEST_F(TrustedServerConfigClientTest, ThrowsUnavailableErrorOnClientInitFail) {
  absl::flat_hash_map<std::string, std::string> config_entries_map = {
      {"config_param_1", kEmptyValue},
  };

  EXPECT_CALL(*mock_cpio_parameter_client_, Init())
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, Run())
      .WillOnce(Return(absl::UnknownError("")));

  TrustedServersConfigClient config_client({"config_param_1"});
  absl::Status init_result =
      config_client.Init("", std::move(mock_cpio_parameter_client_));

  ASSERT_FALSE(init_result.ok());
}

TEST_F(TrustedServerConfigClientTest, PrependsFlagNamesWithTag) {
  EXPECT_CALL(*mock_cpio_parameter_client_, Init())
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, Run())
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cpio_parameter_client_, GetParameter)
      .WillOnce([](const GetParameterRequest& get_param_req,
                   const Callback<GetParameterResponse>& callback) {
        // Verify we query for fetched config values with the prefix.
        EXPECT_EQ(get_param_req.parameter_name(),
                  "MyConfigParamPrefix-config_param_1");

        GetParameterResponse response;
        response.set_parameter_value("config_value_1");
        callback(SuccessExecutionResult(), response);
        return absl::OkStatus();
      });
  TrustedServersConfigClient config_client({"config_param_1"});
  ASSERT_TRUE(
      config_client
          .Init("MyConfigParamPrefix-", std::move(mock_cpio_parameter_client_))
          .ok());

  EXPECT_EQ(config_client.GetStringParameter("config_param_1"),
            "config_value_1");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
