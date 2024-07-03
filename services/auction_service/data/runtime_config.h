/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_AUCTION_SERVICE_DATA_RUNTIME_CONFIG_H_
#define SERVICES_AUCTION_SERVICE_DATA_RUNTIME_CONFIG_H_

#include <string>

#include "services/auction_service/auction_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

struct AuctionServiceRuntimeConfig {
  bool enable_seller_debug_url_generation = false;
  // Sets the timeout used by Roma for dispatch requests
  std::string roma_timeout_ms = "10000";

  // Enables Seller Code Wrapper for complete code generation.
  bool enable_seller_code_wrapper = false;
  // Enables exporting console.logs from Roma to Auction Service
  bool enable_adtech_code_logging = false;
  // Enables execution of reportResult() function from AdTech provided script to
  // generate the event level reporting urls.
  bool enable_report_result_url_generation = false;
  // Enables execution of reportWin() function from AdTech provided script to
  // generate the event level reporting urls for Buyer.
  bool enable_report_win_url_generation = false;
  // Seller's domain required as input for reporting url generation.
  std::string seller_origin = "";

  // Enables protected app signals support.
  bool enable_protected_app_signals = false;

  // Enables noising of modeling_signals, recency and join_count inputs to
  // reportWin function.
  bool enable_report_win_input_noising = false;
  // The max allowed size of a debug win or loss URL. Default value is 64 KB.
  int max_allowed_size_debug_url_bytes = 65536;
  // The max allowed size of all debug win or loss URLs for an auction.
  // Default value is 3000 kilobytes.
  int max_allowed_size_all_debug_urls_kb = 3000;

  // Default code version to pass to Roma.
  std::string default_code_version = kScoreAdBlobVersion;
  // Temporary flag to enable seller and buyer udf isolation.
  bool enable_seller_and_buyer_udf_isolation = false;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_DATA_AUCTIONSERVICERUNTIMECONFIG_H_
