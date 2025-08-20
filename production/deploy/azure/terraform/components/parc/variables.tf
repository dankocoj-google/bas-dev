# Copyright 2025 Google LLC
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

variable "operator" {
  description = "Operator. Ex: buyer1, seller1, etc."
  type        = string
}

variable "environment" {
  description = "Environment. Ex: dev, prod, etc."
  type        = string
}

variable "region" {
  description = "Region. Ex: eastus, westus, etc."
  type        = string
}

variable "resource_group_name" {
  description = "Resource Group Name"
  type        = string
}

variable "fe_service" {
  description = "Frontend Service Name"
  type        = string
}

variable "service" {
  description = "Service Name"
  type        = string
}

variable "namespace" {
  description = "Kubernetes Namespace"
  type        = string
}

variable "parc_image" {
  description = "Parc Image and Version"
  type        = string
}

variable "parc_port" {
  description = "Parc gRPC Port"
  type        = number
}

variable "otel_collector_endpoint" {
  description = "OTel Collector Endpoint"
  type        = string
  default     = "collector.test.internal"
}

variable "otel_grpc_port" {
  description = "OTel Collector gRPC Port"
  type        = number
  default     = 4317
}

variable "parc_user_assigned_identity_client_id" {
  description = "Parc User Assigned Managed Identity (for Blob Storage Account Access) Client ID"
  type        = string
}

variable "use_byoc" {
  description = "Bring Your Own Certificate Approach. True if enabled"
  type        = bool
  default     = false
}

variable "storage_account_name" {
  description = "Storage Account Name"
  type        = string
}

variable "storage_account_resource_group" {
  description = "Storage Account Resource Group"
  type        = string
}
