# Copyright 2023 Google LLC
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

"""Top-level config-related variables."""

ENABLE_CORE_DUMPS_DEFINES = select({
    "//:non_prod_build": ["PS_ENABLE_CORE_DUMPS=true"],
    "//conditions:default": ["PS_ENABLE_CORE_DUMPS=false"],
})

IS_PROD_BUILD_DEFINES = select({
    "//:non_prod_build": ["PS_IS_PROD_BUILD=false"],
    "//conditions:default": ["PS_IS_PROD_BUILD=true"],
})

IS_PARC_BUILD_DEFINES = select({
    "//:parc_enabled": ["PS_IS_PARC_BUILD=true"],
    "//conditions:default": ["PS_IS_PARC_BUILD=false"],
})
