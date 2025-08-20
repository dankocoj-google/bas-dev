//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_BYOB_BYOB_DISPATCH_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_BYOB_BYOB_DISPATCH_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "services/common/clients/code_dispatcher/udf_code_loader_interface.h"
#include "src/roma/byob/interface/metrics.h"

namespace privacy_sandbox::bidding_auction_servers {

template <typename ServiceResponse>
struct ByobDispatchResponse {
  // The response of the execution.
  ServiceResponse response;
  // Execution metrics.
  ::privacy_sandbox::server_common::byob::ProcessRequestMetrics metrics;
};

// Classes implementing this template and interface are able to execute
// asynchronous requests using ROMA BYOB.
template <typename BatchRequest, typename ServiceRequest,
          typename ServiceResponse>
class ByobDispatchClient : public UdfCodeLoaderInterface {
 public:
  // Polymorphic class => virtual destructor
  virtual ~ByobDispatchClient() = default;

  // Loads new execution code synchronously. The class implementing this method
  // must track the version currently loaded into ROMA BYOB. If the class
  // supports multiple versions, it needs to track and be able to provide a
  // means of executing different versions of the UDF code.
  //
  // version: the new version string of the code to load
  // code: the code string to load
  // return: a status indicating whether the code load was successful.
  virtual absl::Status LoadSync(std::string version, std::string code) = 0;

  // Executes a single request asynchronously.
  //
  // request: the request object.
  // timeout: the maximum time this will block for.
  // callback: called with the output of execution.
  // return: a status indicating if the execution request was properly
  //         processed by the implementing class. This should not be confused
  //         with the output of the execution itself, which is sent to callback.
  virtual absl::Status Execute(
      const ServiceRequest& request, absl::Duration timeout,
      absl::AnyInvocable<
          void(absl::StatusOr<ByobDispatchResponse<ServiceResponse>>) &&>
          callback) = 0;
  // Executes a batch of requests asynchronously.
  //
  // raw_request: the batch data object.
  // common_request: a common request object that can be updated for every
  // request in the batch start_timeout: the maximum time an execution will wait
  // for a worker execution_timeout: the maximum time this will wait for the
  // batch of executions to finish after which they will be cancelled callback:
  // called with a vector of execution outputs return: a status indicating if
  // the execution request was properly
  //         accepted by the implementing class. This should not be confused
  //         with the output of the execution itself, which is sent to callback.
  virtual absl::Status ExecuteManyWithSharedTimeouts(
      BatchRequest& raw_request, ServiceRequest common_request,
      absl::Duration start_timeout, absl::Duration execution_timeout,
      absl::AnyInvocable<void(std::vector<absl::StatusOr<
                                  ByobDispatchResponse<ServiceResponse>>>) &&>
          callback) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_BYOB_BYOB_DISPATCH_CLIENT_H_
