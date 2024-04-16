# Copyright 2022 Google LLC
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

load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//:config.bzl", "ENABLE_CORE_DUMPS_DEFINES")

cc_binary(
    name = "server",
    srcs = ["buyer_frontend_main.cc"],
    linkopts = [
        "-Wl,-rpath,\\$$ORIGIN/../lib",
    ],
    local_defines = ENABLE_CORE_DUMPS_DEFINES,
    visibility = ["//visibility:public"],
    deps = [
    ],
)
