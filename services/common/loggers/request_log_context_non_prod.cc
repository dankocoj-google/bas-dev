/*
 * Copyright 2024 Google LLC
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

#include "services/common/loggers/request_log_context.h"

namespace privacy_sandbox::bidding_auction_servers {

void ModifyConsent(server_common::ConsentedDebugConfiguration& original) {
  PS_LOG(INFO, SystemLogContext())
      << "Modify request to consented, original config: "
      << original.ShortDebugString();
  original.set_is_consented(true);
  original.set_token(server_common::log::ServerToken());
  PS_LOG(INFO, SystemLogContext())
      << "After modification: " << original.ShortDebugString();
}

}  // namespace privacy_sandbox::bidding_auction_servers
