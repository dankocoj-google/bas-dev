# Copyright 2025 Google LLC
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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "attestation_util",
    hdrs = [
        "attestation_util.h",
    ],
    deps = [
        "//services/common/util:hash_util",
        "//third_party/libtld",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_test(
    name = "attestation_util_test",
    size = "small",
    srcs = [
        "attestation_util_test.cc",
    ],
    deps = [
        ":attestation_util",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_library(
    name = "adtech_enrollment_cache",
    srcs = [
        "adtech_enrollment_cache.cc",
    ],
    hdrs = [
        "adtech_enrollment_cache.h",
    ],
    deps = [
        "//api:attestation_cc_proto",
        "//third_party/libtld",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_test(
    name = "adtech_enrollment_cache_test",
    size = "small",
    srcs = [
        "adtech_enrollment_cache_test.cc",
    ],
    deps = [
        ":adtech_enrollment_cache",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_library(
    name = "adtech_enrollment_fetcher",
    srcs = ["adtech_enrollment_fetcher.cc"],
    hdrs = [
        "adtech_enrollment_fetcher.h",
    ],
    deps = [
        ":adtech_enrollment_cache",
        "//api:attestation_cc_proto",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/data_fetch:periodic_url_fetcher",
        "//services/seller_frontend_service/k_anon:k_anon_utils",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "adtech_enrollment_fetcher_test",
    size = "small",
    srcs = [
        "adtech_enrollment_fetcher_test.cc",
    ],
    deps = [
        ":adtech_enrollment_fetcher",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "//services/seller_frontend_service/k_anon:k_anon_utils",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/core/test/utils",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)
