// Copyright 2025 Google LLC
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

#ifndef SERVICES_BIDDING_SERVICE_UTILS_BROWSER_SIGNALS_H_
#define SERVICES_BIDDING_SERVICE_UTILS_BROWSER_SIGNALS_H_

#include <string>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
constexpr char kEmptyDeviceSignals[] = R"JSON({})JSON";
std::string MakeBrowserSignalsForScript(
    absl::string_view publisher_name, absl::string_view seller,
    absl::string_view top_level_seller,
    const BrowserSignalsForBidding& browser_signals, uint32_t data_version,
    int32_t multi_bid_limit, const ForDebuggingOnlyFlags& fdo_flags);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_UTILS_BROWSER_SIGNALS_H_
