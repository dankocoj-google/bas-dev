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

#include "grpc_sidecar.h"

#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "gtest/gtest.h"
#include "modules/module_interface.h"
#include "modules/test_module.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"
#include "utils/cancellation_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

class CancelledRPCTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup gRPC server and stub for testing cancellation.
    InferenceSidecarRuntimeConfig config;
    service_ = std::make_unique<InferenceServiceImpl>(config);

    grpc::ServerBuilder builder;
    server_address_ = "unix:/tmp/inference_sidecar_test_" +
                      std::to_string(absl::GetCurrentTimeNanos());
    builder.AddListeningPort(server_address_,
                             grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();
    ASSERT_NE(server_, nullptr);

    stub_ = InferenceService::NewStub(grpc::CreateChannel(
        server_address_, grpc::InsecureChannelCredentials()));
    ASSERT_NE(stub_, nullptr);
  }

  void TearDown() override { server_->Shutdown(); }

  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<InferenceService::StubInterface> stub_;
  std::unique_ptr<InferenceService::Service> service_;
};

namespace {

constexpr absl::string_view kInferenceSidecarBinary = "inference_sidecar";
constexpr absl::string_view kTestModelPath = "test_model";
constexpr char kJsonString[] = R"json({
  "request" : [{
    "model_path" : "test_model",
    "tensors" : [
    {
      "tensor_name": "double1",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
}]
    })json";

TEST_F(CancelledRPCTest, GetModelPathsIsCancelled) {
  GetModelPathsRequest request;
  GetModelPathsResponse response;
  grpc::ClientContext context;

  context.TryCancel();

  grpc::Status status = stub_->GetModelPaths(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
}

TEST_F(CancelledRPCTest, RegisterModelIsCancelled) {
  RegisterModelRequest request;
  RegisterModelResponse response;
  grpc::ClientContext context;

  context.TryCancel();

  grpc::Status status = stub_->RegisterModel(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
}

TEST_F(CancelledRPCTest, PredictIsCancelled) {
  PredictRequest request;
  PredictResponse response;
  grpc::ClientContext context;

  context.TryCancel();

  grpc::Status status = stub_->Predict(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
}

TEST_F(CancelledRPCTest, DeleteModelIsCancelled) {
  DeleteModelRequest request;
  DeleteModelResponse response;
  grpc::ClientContext context;

  context.TryCancel();

  grpc::Status status = stub_->DeleteModel(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
