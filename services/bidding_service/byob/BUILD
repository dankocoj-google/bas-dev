# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_proto_library", "cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = ["//services/bidding_service:__subpackages__"])

proto_library(
    name = "byob_batching_config_proto",
    srcs = ["byob_batching_config.proto"],
    deps = [
        "@com_google_googleapis//google/api:annotations_proto",
    ],
)

cc_proto_library(
    name = "byob_batching_config_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":byob_batching_config_proto"],
)

cc_library(
    name = "proto_utils",
    srcs = ["proto_utils.h"],
    hdrs = ["proto_utils.h"],
    visibility = [
        "//services/bidding_service:__subpackages__",
        "//services/common/test:__subpackages__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//api/udf:generate_bid_byob_sdk_cc_proto",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "generate_bid_byob_dispatch_client",
    srcs = ["generate_bid_byob_dispatch_client.cc"],
    hdrs = ["generate_bid_byob_dispatch_client.h"],
    deps = [
        ":proto_utils",
        "//api/udf:generate_bid_byob_sdk_cc_proto",
        "//api/udf:generate_bid_byob_sdk_roma_cc_lib",
        "//services/common/clients/code_dispatcher/byob:byob_dispatch_client",
        "//services/common/loggers:request_log_context",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/roma/byob/config",
        "@google_privacysandbox_servers_common//src/roma/byob/utility:udf_blob",
        "@google_privacysandbox_servers_common//src/util:duration",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_library(
    name = "buyer_code_fetch_manager_byob",
    srcs = ["buyer_code_fetch_manager_byob.cc"],
    hdrs = ["buyer_code_fetch_manager_byob.h"],
    deps = [
        "//services/bidding_service:buyer_code_fetch_manager",
        "//services/common/util:file_util",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "buyer_code_fetch_manager_byob_test",
    size = "small",
    srcs = ["buyer_code_fetch_manager_byob_test.cc"],
    deps = [
        ":buyer_code_fetch_manager_byob",
        "//services/common/blob_storage_client:blob_storage_client_cpio",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/blob_storage_client:blob_storage_client_mock",
    ],
)
