// Copyright 2023 Google LLC
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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/inference/inference_utils.h"

#include <gmock/gmock-matchers.h>
#include <gmock/gmock.h>

#include <grpcpp/client_context.h>

#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "services/bidding_service/inference/inference_flags.h"
#include "services/common/clients/code_dispatcher/request_context.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/metric/server_definition.h"
#include "src/roma/interface/roma.h"
#include "utils/error.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Forward declarations for internal functions used in tests.
absl::Status RegisterModelFromBucketInternal(
    absl::string_view bucket_name, const std::vector<std::string>& paths,
    const std::vector<BlobFetcher::Blob>& blobs,
    InferenceService::StubInterface& stub);
absl::Status RegisterModelFromLocalInternal(
    const std::vector<std::string>& paths,
    InferenceService::StubInterface& stub);
void RunInferenceInternal(google::scp::roma::FunctionBindingPayload<
                              RomaRequestSharedContext>& wrapper,
                          InferenceService::StubInterface& stub);
void GetModelPathsInternal(google::scp::roma::FunctionBindingPayload<
                               RomaRequestSharedContext>& wrapper,
                           InferenceService::StubInterface& stub);
bool InferenceUseProto();

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::StartsWith;

constexpr absl::string_view kSidecarBinary =
    "__main__/external/inference_common/inference_sidecar";
constexpr absl::string_view kInit = "non-empty";
constexpr absl::string_view kTestModelPath =
    "external/inference_common/testdata/models/tensorflow_1_mib_saved_model.pb";
constexpr absl::string_view kBucketName = "test_bucket";
constexpr absl::string_view kRuntimeConfig = R"json({
  "num_interop_threads": 4,
  "num_intraop_threads": 5,
  "module_name": "test",
  "cpuset": [0, 1]
})json";

// Mock for the gRPC stub's async interface.
class MockAsyncInferenceStub
    : public InferenceService::StubInterface::async_interface {
 public:
  // Callback-based mocks
  MOCK_METHOD(void, RegisterModel,
              (grpc::ClientContext*, const RegisterModelRequest*,
               RegisterModelResponse*, std::function<void(grpc::Status)>),
              (override));
  MOCK_METHOD(void, Predict,
              (grpc::ClientContext*, const PredictRequest*, PredictResponse*,
               std::function<void(grpc::Status)>),
              (override));
  MOCK_METHOD(void, GetModelPaths,
              (grpc::ClientContext*, const GetModelPathsRequest*,
               GetModelPathsResponse*, std::function<void(grpc::Status)>),
              (override));
  MOCK_METHOD(void, PredictBatch,
              (grpc::ClientContext*, const PredictBatchRequest*,
               PredictBatchResponse*, std::function<void(grpc::Status)>),
              (override));
  MOCK_METHOD(void, DeleteModel,
              (grpc::ClientContext*, const DeleteModelRequest*,
               DeleteModelResponse*, std::function<void(grpc::Status)>),
              (override));

  // Reactor-based mocks
  MOCK_METHOD(void, RegisterModel,
              (grpc::ClientContext*, const RegisterModelRequest*,
               RegisterModelResponse*, grpc::ClientUnaryReactor*),
              (override));
  MOCK_METHOD(void, Predict,
              (grpc::ClientContext*, const PredictRequest*, PredictResponse*,
               grpc::ClientUnaryReactor*),
              (override));
  MOCK_METHOD(void, GetModelPaths,
              (grpc::ClientContext*, const GetModelPathsRequest*,
               GetModelPathsResponse*, grpc::ClientUnaryReactor*),
              (override));
  MOCK_METHOD(void, PredictBatch,
              (grpc::ClientContext*, const PredictBatchRequest*,
               PredictBatchResponse*, grpc::ClientUnaryReactor*),
              (override));
  MOCK_METHOD(void, DeleteModel,
              (grpc::ClientContext*, const DeleteModelRequest*,
               DeleteModelResponse*, grpc::ClientUnaryReactor*),
              (override));
};

// Mock for the gRPC stub to control its behavior in tests.
class MockInferenceStub : public InferenceService::StubInterface {
 public:
  MockInferenceStub() {
    ON_CALL(*this, async).WillByDefault(Return(&async_stub_));
  }

  MOCK_METHOD(async_interface*, async, (), (override));

  MockAsyncInferenceStub async_stub_;

  // Keep original sync and other async mocks to implement the full interface.
  MOCK_METHOD(grpc::Status, RegisterModel,
              (grpc::ClientContext * context,
               const RegisterModelRequest& request,
               RegisterModelResponse* response),
              (override));
  MOCK_METHOD(grpc::Status, Predict,
              (grpc::ClientContext * context, const PredictRequest& request,
               PredictResponse* response),
              (override));
  MOCK_METHOD(grpc::Status, GetModelPaths,
              (grpc::ClientContext * context,
               const GetModelPathsRequest& request,
               GetModelPathsResponse* response),
              (override));
  MOCK_METHOD(grpc::Status, PredictBatch,
              (grpc::ClientContext * context,
               const PredictBatchRequest& request,
               PredictBatchResponse* response),
              (override));
  MOCK_METHOD(grpc::Status, DeleteModel,
              (grpc::ClientContext * context, const DeleteModelRequest& request,
               DeleteModelResponse* response),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<PredictBatchResponse>*,
              AsyncPredictBatchRaw,
              (grpc::ClientContext * context,
               const PredictBatchRequest& request, grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<PredictBatchResponse>*,
              PrepareAsyncPredictBatchRaw,
              (grpc::ClientContext * context,
               const PredictBatchRequest& request, grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<PredictResponse>*,
              AsyncPredictRaw,
              (grpc::ClientContext * context, const PredictRequest& request,
               grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<PredictResponse>*,
              PrepareAsyncPredictRaw,
              (grpc::ClientContext * context, const PredictRequest& request,
               grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<RegisterModelResponse>*,
              AsyncRegisterModelRaw,
              (grpc::ClientContext * context,
               const RegisterModelRequest& request, grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<RegisterModelResponse>*,
              PrepareAsyncRegisterModelRaw,
              (grpc::ClientContext * context,
               const RegisterModelRequest& request, grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<DeleteModelResponse>*,
              AsyncDeleteModelRaw,
              (grpc::ClientContext * context, const DeleteModelRequest& request,
               grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<DeleteModelResponse>*,
              PrepareAsyncDeleteModelRaw,
              (grpc::ClientContext * context, const DeleteModelRequest& request,
               grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<GetModelPathsResponse>*,
              AsyncGetModelPathsRaw,
              (grpc::ClientContext * context,
               const GetModelPathsRequest& request, grpc::CompletionQueue* cq),
              (override));
  MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<GetModelPathsResponse>*,
              PrepareAsyncGetModelPathsRaw,
              (grpc::ClientContext * context,
               const GetModelPathsRequest& request, grpc::CompletionQueue* cq),
              (override));
};

// Helper function to assert that a given grpc::ClientContext has a deadline
// that is approximately equal to an expected duration from now within a
// specified tolerance.
void ExpectDeadlineApproximately(grpc::ClientContext& context,
                                 absl::Duration expected_duration_from_now,
                                 absl::Duration tolerance) {
  const absl::Time call_time = absl::Now();
  absl::Time expected_deadline = call_time + expected_duration_from_now;
  absl::Time actual_deadline = absl::FromChrono(context.deadline());
  absl::Duration time_difference =
      absl::AbsDuration(actual_deadline - expected_deadline);

  EXPECT_LE(time_difference, tolerance)
      << "Deadline mismatch. Expected: " << absl::FormatTime(expected_deadline)
      << ", Actual: " << absl::FormatTime(actual_deadline)
      << ", Difference: " << time_difference << ", Tolerance: " << tolerance;
}

class InferenceUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::SetFlag(&FLAGS_testonly_allow_policies_for_bazel, true);
    absl::SetFlag(&FLAGS_inference_sidecar_binary_path,
                  GetFilePath(kSidecarBinary));
    absl::SetFlag(&FLAGS_inference_sidecar_runtime_config, kRuntimeConfig);
    absl::SetFlag(&FLAGS_inference_model_execution_timeout_ms, absl::nullopt);
    absl::SetFlag(&FLAGS_inference_enable_proto_parsing, false);
  }

 private:
  absl::FlagSaver flag_saver_;
};

// TODO(b/322030670): Making static SandboxExecutor compatible with multiple
// tests.

constexpr char kSimpleModel[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    }
  ]
}]
    })json";

TEST_F(InferenceUtilsTest, TestAPIOutputs) {
  SandboxExecutor& inference_executor = Executor();
  CHECK_EQ(inference_executor.StartSandboxee().code(), absl::StatusCode::kOk);

  ASSERT_TRUE(RegisterModelsFromLocal({std::string(kTestModelPath)}).ok());
  google::scp::roma::proto::FunctionBindingIoProto input_output_proto;
  google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext> wrapper{
      input_output_proto, {}};
  wrapper.io_proto.set_input_string(absl::StrCat(kSimpleModel));
  wrapper.io_proto.set_output_string(kInit);
  RunInference(wrapper);
  // TODO(b/317124477): Update the output string after Tensorflow execution
  // logic. Currently, this test uses a test inference module that doesn't
  // populate the output string.
  ASSERT_EQ(wrapper.io_proto.output_string(), "0.57721");

  // make sure GetModelPaths returns the registered model
  google::scp::roma::proto::FunctionBindingIoProto input_output_proto_1;
  google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext> wrapper_1{
      input_output_proto_1, {}};
  GetModelPaths(wrapper_1);
  ASSERT_EQ(wrapper_1.io_proto.output_string(),
            "[\"" + std::string(kTestModelPath) + "\"]");

  absl::StatusOr<sandbox2::Result> result = inference_executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
  ASSERT_EQ(result->reason_code(), 0);

  // Propagates JS error to client even when inference sidecar is not
  // reachable.
  RunInference(wrapper);
  EXPECT_THAT(
      wrapper.io_proto.output_string(),
      StartsWith(
          R"({"response":[{"error":{"error_type":"GRPC","description")"));
}

TEST_F(InferenceUtilsTest, RegisterModelsFromLocal_NoPath_Error) {
  EXPECT_EQ(RegisterModelsFromLocal({}).code(), absl::StatusCode::kNotFound);
}

TEST_F(InferenceUtilsTest, RegisterModelsFromLocal_EmptyPath_Error) {
  EXPECT_EQ(RegisterModelsFromLocal({""}).code(), absl::StatusCode::kNotFound);
}

TEST_F(InferenceUtilsTest, RegisterModelsFromBucket_Error) {
  EXPECT_EQ(
      RegisterModelsFromBucket("", {std::string(kTestModelPath)}, {}).code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      RegisterModelsFromBucket(kBucketName, {std::string(kTestModelPath)}, {})
          .code(),
      absl::StatusCode::kNotFound);
  EXPECT_EQ(RegisterModelsFromBucket(kBucketName, {}, {}).code(),
            absl::StatusCode::kNotFound);
  EXPECT_EQ(RegisterModelsFromBucket(kBucketName, {""}, {{"", ""}}).code(),
            absl::StatusCode::kNotFound);
}

TEST_F(InferenceUtilsTest, RegisterModelsFromBucket_RegistersAllModels) {
  auto mock_stub = std::make_unique<MockInferenceStub>();
  EXPECT_CALL(mock_stub->async_stub_,
              RegisterModel(_, _, _, An<std::function<void(grpc::Status)>>()))
      .Times(2)
      .WillRepeatedly([](grpc::ClientContext* context,
                         const RegisterModelRequest* request,
                         RegisterModelResponse* response,
                         const std::function<void(grpc::Status)>& callback) {
        callback(grpc::Status::OK);
      });

  std::vector<std::string> model_paths = {"path/to/model1", "path/to/model2"};
  std::vector<BlobFetcher::Blob> blobs = {
      BlobFetcher::Blob("path/to/model1/file1", "bytes1"),
      BlobFetcher::Blob("path/to/model2/file2", "bytes2")};

  absl::Status result = RegisterModelFromBucketInternal(
      kBucketName, model_paths, blobs, *mock_stub);
  EXPECT_TRUE(result.ok());
}

TEST_F(InferenceUtilsTest, RunInference_HandlesGrpcError) {
  auto mock_stub = std::make_unique<MockInferenceStub>();
  ON_CALL(mock_stub->async_stub_,
          Predict(_, _, _, An<std::function<void(grpc::Status)>>()))
      .WillByDefault([](grpc::ClientContext* context,
                        const PredictRequest* request,
                        PredictResponse* response,
                        const std::function<void(grpc::Status)>& callback) {
        callback(grpc::Status(grpc::StatusCode::UNAVAILABLE, "test error"));
      });

  google::scp::roma::proto::FunctionBindingIoProto io_proto;
  io_proto.set_input_string("{}");
  google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext> wrapper{
      io_proto, {}};

  RunInferenceInternal(wrapper, *mock_stub);

  EXPECT_THAT(wrapper.io_proto.output_string(),
              ::testing::AllOf(StartsWith("{\"response\":[{\"error\":"),
                               HasSubstr("\"error_type\":\"GRPC\""),
                               HasSubstr("\"description\":\"Code: 14,"
                                         " Message: test error\"")));
}

TEST_F(InferenceUtilsTest, RunInference_SuccessPathNonProto) {
  absl::SetFlag(&FLAGS_inference_enable_proto_parsing, false);
  auto mock_stub = std::make_unique<MockInferenceStub>();
  const std::string expected_output = "non-proto output";

  EXPECT_CALL(mock_stub->async_stub_,
              Predict(_, _, _, An<std::function<void(grpc::Status)>>()))
      .WillOnce([&expected_output](
                    grpc::ClientContext* context, const PredictRequest* request,
                    PredictResponse* response,
                    const std::function<void(grpc::Status)>& callback) {
        response->set_output(expected_output);
        callback(grpc::Status::OK);
      });

  google::scp::roma::proto::FunctionBindingIoProto io_proto;
  io_proto.set_input_string("some input");
  google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext> wrapper{
      io_proto, {}};

  RunInferenceInternal(wrapper, *mock_stub);
  EXPECT_EQ(wrapper.io_proto.output_string(), expected_output);
}

TEST_F(InferenceUtilsTest, GetModelResponseToJsonOuput) {
  GetModelPathsResponse get_model_paths_response;
  EXPECT_EQ("[]", GetModelResponseToJson(get_model_paths_response));
  ModelSpec* spec;

  spec = get_model_paths_response.add_model_specs();
  spec->set_model_path("a");

  EXPECT_EQ("[\"a\"]", GetModelResponseToJson(get_model_paths_response));

  spec = get_model_paths_response.add_model_specs();
  spec->set_model_path("b");

  EXPECT_EQ("[\"a\",\"b\"]", GetModelResponseToJson(get_model_paths_response));
}

TEST_F(InferenceUtilsTest, SetClientDeadline_NoTimeout) {
  grpc::ClientContext context;
  std::chrono::system_clock::time_point default_unset_deadline =
      context.deadline();
  SetClientDeadline(std::nullopt, context);
  EXPECT_EQ(context.deadline(), default_unset_deadline)
      << "Deadline should not be set when timeout is nullopt.";
}

TEST_F(InferenceUtilsTest, SetClientDeadline_PositiveTimeout) {
  grpc::ClientContext context;
  absl::Duration tolerance = absl::Milliseconds(10);

  absl::SetFlag(&FLAGS_inference_model_execution_timeout_ms, 1000);
  SetClientDeadline(absl::GetFlag(FLAGS_inference_model_execution_timeout_ms),
                    context);
  ExpectDeadlineApproximately(context, absl::Milliseconds(1000), tolerance);

  absl::SetFlag(&FLAGS_inference_model_execution_timeout_ms, 2000);
  SetClientDeadline(absl::GetFlag(FLAGS_inference_model_execution_timeout_ms),
                    context);
  ExpectDeadlineApproximately(context, absl::Milliseconds(2000), tolerance);
}

TEST_F(InferenceUtilsTest, GetModelPathsInternal_Success) {
  auto mock_stub = std::make_unique<MockInferenceStub>();
  google::scp::roma::proto::FunctionBindingIoProto io_proto;
  google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext> wrapper{
      io_proto, {}};

  EXPECT_CALL(mock_stub->async_stub_,
              GetModelPaths(_, _, _, An<std::function<void(grpc::Status)>>()))
      .WillOnce([](grpc::ClientContext* context,
                   const GetModelPathsRequest* request,
                   GetModelPathsResponse* response,
                   const std::function<void(grpc::Status)>& callback) {
        response->add_model_specs()->set_model_path("model_a");
        response->add_model_specs()->set_model_path("model_b");
        callback(grpc::Status::OK);
      });

  GetModelPathsInternal(wrapper, *mock_stub);
  EXPECT_EQ(wrapper.io_proto.output_string(), "[\"model_a\",\"model_b\"]");
}

// Next two tests must be separate as flag read is static.
TEST_F(InferenceUtilsTest, InferenceUseProto_ReadsFlagFalse) {
  absl::SetFlag(&FLAGS_inference_enable_proto_parsing, false);
  EXPECT_FALSE(InferenceUseProto());
}

TEST_F(InferenceUtilsTest, InferenceUseProto_ReadsFlagTrue) {
  absl::SetFlag(&FLAGS_inference_enable_proto_parsing, true);
  EXPECT_FALSE(InferenceUseProto());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
