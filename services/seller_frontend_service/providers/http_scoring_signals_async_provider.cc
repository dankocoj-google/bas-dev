// Copyright 2022 Google LLC
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

#include "services/seller_frontend_service/providers/http_scoring_signals_async_provider.h"

#include <string>
#include <utility>

namespace privacy_sandbox::bidding_auction_servers {

HttpScoringSignalsAsyncProvider::HttpScoringSignalsAsyncProvider(
    std::unique_ptr<AsyncClient<GetSellerValuesInput, GetSellerValuesOutput>>
        http_seller_kv_async_client,
    bool enable_protected_app_signals)
    : http_seller_kv_async_client_(std::move(http_seller_kv_async_client)),
      enable_protected_app_signals_(enable_protected_app_signals) {}

void HttpScoringSignalsAsyncProvider::Get(
    const ScoringSignalsRequest& scoring_signals_request,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<ScoringSignals>>,
                            GetByteSize) &&>
        on_done,
    absl::Duration timeout) const {
  auto request = std::make_unique<GetSellerValuesInput>();
  for (const auto& [unused_buyer, get_bids_response] :
       scoring_signals_request.buyer_bids_map_) {
    for (const auto& ad : get_bids_response->bids()) {
      request->render_urls.emplace(ad.render());
      request->ad_component_render_urls.insert(ad.ad_components().begin(),
                                               ad.ad_components().end());
    }
    if (enable_protected_app_signals_) {
      for (const auto& ad : get_bids_response->protected_app_signals_bids()) {
        request->render_urls.emplace(ad.render());
      }
    }
  }
  request->client_type = scoring_signals_request.client_type_;
  request->seller_kv_experiment_group_id =
      scoring_signals_request.seller_kv_experiment_group_id_;
  auto status = http_seller_kv_async_client_->Execute(
      std::move(request), scoring_signals_request.filtering_metadata_,
      [on_done = std::move(on_done)](
          absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>
              kv_output) mutable {
        GetByteSize get_byte_size;
        absl::StatusOr<std::unique_ptr<ScoringSignals>> res;
        if (kv_output.ok()) {
          res = std::make_unique<ScoringSignals>();
          res.value()->scoring_signals =
              std::make_unique<std::string>(kv_output.value()->result);
          get_byte_size.request = kv_output.value()->request_size;
          get_byte_size.response = kv_output.value()->response_size;
        } else {
          res = kv_output.status();
        }
        std::move(on_done)(std::move(res), get_byte_size);
      },
      timeout);
  if (!status.ok()) {
    PS_LOG(ERROR) << "Unable to get seller KV signals: " << status;
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
