# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "egress",
    srcs = [
        "egress.cc",
        "//third_party/cddl:cddl_h",
    ],
    hdrs = [
        "egress.h",
    ],
    includes = [
        "include",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@cddl_lib//:cddl",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "egress_test",
    size = "small",
    srcs = ["egress_test.cc"],
    data = [
        "//services/bidding_service/egress_cddl_spec:1.0.0",
    ],
    deps = [
        ":egress",
        "//services/common/util:file_util",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "validation",
    srcs = ["validation.cc"],
    hdrs = ["validation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/attestation:adtech_enrollment_cache",
        "//services/common/attestation:attestation_util",
        "//services/common/loggers:request_log_context",
        "//services/common/metric:server_definition",
        "//services/common/util:reporting_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "validation_test",
    size = "small",
    srcs = ["validation_test.cc"],
    deps = [
        ":validation",
        "//services/common/attestation:adtech_enrollment_cache",
        "//services/common/test:random",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "browser_signals_util",
    srcs = [
        "browser_signals_util.cc",
    ],
    hdrs = [
        "browser_signals_util.h",
    ],
    visibility = [
        "//services/bidding_service:__pkg__",
        "//services/bidding_service/benchmarking:__pkg__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/constants:common_constants",
        "//services/common/util:json_util",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@rapidjson",
    ],
)

cc_test(
    name = "browser_signals_util_test",
    size = "small",
    srcs = ["browser_signals_util_test.cc"],
    deps = [
        ":browser_signals_util",
        "//services/common/util:json_util",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
