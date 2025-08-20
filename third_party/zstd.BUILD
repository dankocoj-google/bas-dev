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

load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "common",
    srcs = glob([
        "lib/common/*.c",
        "lib/common/*.h",
    ]),
)

filegroup(
    name = "compress",
    srcs = glob([
        "lib/compress/*.c",
        "lib/compress/*.h",
    ]),
)

filegroup(
    name = "decompress",
    srcs = glob([
        "lib/decompress/*.c",
        "lib/decompress/*.h",
    ]) + select({
        "//conditions:default": glob(["lib/decompress/*.S"]),
    }),
)

cc_library(
    name = "zstd",
    srcs = [
        ":common",
        ":compress",
        ":decompress",
    ],
    hdrs = [
        "lib/zdict.h",
        "lib/zstd.h",
        "lib/zstd_errors.h",
    ],
    includes = ["lib"],
    linkopts = ["-pthread"],
    linkstatic = True,
    local_defines = [
        "XXH_NAMESPACE=ZSTD_",
        "ZSTD_BUILD_SHARED=OFF",
        "ZSTD_BUILD_STATIC=ON",
    ],
)
