/**
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

variable "fe_service" {
  description = "Frontend Service Name"
  type        = string
}

variable "fe_replicas" {
  description = "Number of Frontend Pod Replicas"
  type        = number
}

variable "fe_ccepolicy" {
  description = "Frontend Confidential Compute Policy. Default is a wildcard policy."
  type        = string
  default     = "cGFja2FnZSBwb2xpY3kKCmFwaV9zdm4gOj0gIjAuMTAuMCIKCm1vdW50X2RldmljZSA6PSB7ImFsbG93ZWQiOiB0cnVlfQptb3VudF9vdmVybGF5IDo9IHsiYWxsb3dlZCI6IHRydWV9CmNyZWF0ZV9jb250YWluZXIgOj0geyJhbGxvd2VkIjogdHJ1ZSwgImVudl9saXN0IjogbnVsbCwgImFsbG93X3N0ZGlvX2FjY2VzcyI6IHRydWV9CnVubW91bnRfZGV2aWNlIDo9IHsiYWxsb3dlZCI6IHRydWV9IAp1bm1vdW50X292ZXJsYXkgOj0geyJhbGxvd2VkIjogdHJ1ZX0KZXhlY19pbl9jb250YWluZXIgOj0geyJhbGxvd2VkIjogdHJ1ZSwgImVudl9saXN0IjogbnVsbH0KZXhlY19leHRlcm5hbCA6PSB7ImFsbG93ZWQiOiB0cnVlLCAiZW52X2xpc3QiOiBudWxsLCAiYWxsb3dfc3RkaW9fYWNjZXNzIjogdHJ1ZX0Kc2h1dGRvd25fY29udGFpbmVyIDo9IHsiYWxsb3dlZCI6IHRydWV9CnNpZ25hbF9jb250YWluZXJfcHJvY2VzcyA6PSB7ImFsbG93ZWQiOiB0cnVlfQpwbGFuOV9tb3VudCA6PSB7ImFsbG93ZWQiOiB0cnVlfQpwbGFuOV91bm1vdW50IDo9IHsiYWxsb3dlZCI6IHRydWV9CmdldF9wcm9wZXJ0aWVzIDo9IHsiYWxsb3dlZCI6IHRydWV9CmR1bXBfc3RhY2tzIDo9IHsiYWxsb3dlZCI6IHRydWV9CnJ1bnRpbWVfbG9nZ2luZyA6PSB7ImFsbG93ZWQiOiB0cnVlfQpsb2FkX2ZyYWdtZW50IDo9IHsiYWxsb3dlZCI6IHRydWV9CnNjcmF0Y2hfbW91bnQgOj0geyJhbGxvd2VkIjogdHJ1ZX0Kc2NyYXRjaF91bm1vdW50IDo9IHsiYWxsb3dlZCI6IHRydWV9Cg=="
}

variable "ccepolicy" {
  description = "Confidential Compute Policy. Default is a wildcard policy."
  type        = string
  default     = "cGFja2FnZSBwb2xpY3kKCmFwaV9zdm4gOj0gIjAuMTAuMCIKCm1vdW50X2RldmljZSA6PSB7ImFsbG93ZWQiOiB0cnVlfQptb3VudF9vdmVybGF5IDo9IHsiYWxsb3dlZCI6IHRydWV9CmNyZWF0ZV9jb250YWluZXIgOj0geyJhbGxvd2VkIjogdHJ1ZSwgImVudl9saXN0IjogbnVsbCwgImFsbG93X3N0ZGlvX2FjY2VzcyI6IHRydWV9CnVubW91bnRfZGV2aWNlIDo9IHsiYWxsb3dlZCI6IHRydWV9IAp1bm1vdW50X292ZXJsYXkgOj0geyJhbGxvd2VkIjogdHJ1ZX0KZXhlY19pbl9jb250YWluZXIgOj0geyJhbGxvd2VkIjogdHJ1ZSwgImVudl9saXN0IjogbnVsbH0KZXhlY19leHRlcm5hbCA6PSB7ImFsbG93ZWQiOiB0cnVlLCAiZW52X2xpc3QiOiBudWxsLCAiYWxsb3dfc3RkaW9fYWNjZXNzIjogdHJ1ZX0Kc2h1dGRvd25fY29udGFpbmVyIDo9IHsiYWxsb3dlZCI6IHRydWV9CnNpZ25hbF9jb250YWluZXJfcHJvY2VzcyA6PSB7ImFsbG93ZWQiOiB0cnVlfQpwbGFuOV9tb3VudCA6PSB7ImFsbG93ZWQiOiB0cnVlfQpwbGFuOV91bm1vdW50IDo9IHsiYWxsb3dlZCI6IHRydWV9CmdldF9wcm9wZXJ0aWVzIDo9IHsiYWxsb3dlZCI6IHRydWV9CmR1bXBfc3RhY2tzIDo9IHsiYWxsb3dlZCI6IHRydWV9CnJ1bnRpbWVfbG9nZ2luZyA6PSB7ImFsbG93ZWQiOiB0cnVlfQpsb2FkX2ZyYWdtZW50IDo9IHsiYWxsb3dlZCI6IHRydWV9CnNjcmF0Y2hfbW91bnQgOj0geyJhbGxvd2VkIjogdHJ1ZX0Kc2NyYXRjaF91bm1vdW50IDo9IHsiYWxsb3dlZCI6IHRydWV9Cg=="
}

variable "fe_image" {
  description = "Frontend Image"
  type        = string
}

variable "image" {
  description = "Image"
  type        = string
}

variable "environment" {
  description = "Environment. Ex: dev, prod, etc."
  type        = string
}

variable "operator" {
  description = "Operator. Ex: buyer1, seller1, etc."
  type        = string
}

variable "region" {
  description = "Region. Ex: eastus, westus, etc."
  type        = string
}

variable "availability_zones" {
  description = "Availability Zones. At this time only the first zone will be used, until the upcoming multi-zone availability zones for Azure Virtual Nodes is released. This is the zone where ALL the pods will be deployed. Ex: 2, 3"
  type        = list(string)
  default     = null
}

variable "fe_grpc_port" {
  description = "Frontend gRPC Port"
  type        = number
}

variable "fe_http_port" {
  description = "Frontend Optional HTTP Port"
  type        = number
  default     = null
}

variable "service" {
  description = "Service Name"
  type        = string
}

variable "grpc_port" {
  description = "gRPC Port"
  type        = number
}

variable "namespace" {
  description = "Namespace"
  type        = string
}

variable "parc_config_map_hash" {
  description = "Parc ConfigMap Hash"
  type        = string
}

variable "replicas" {
  description = "Number of Backend Service Pod Replicas"
  type        = number
}

variable "frontend_cpu_limit" {
  description = "Frontend Pod CPU Limit"
  type        = string
  default     = null
}

variable "frontend_memory_limit" {
  description = "Frontend Pod Memory Limit"
  type        = string
  default     = null
}

variable "backend_cpu_limit" {
  description = "Backend Pod CPU Limit"
  type        = string
  default     = null
}

variable "backend_memory_limit" {
  description = "Backend Pod Memory Limit"
  type        = string
  default     = null
}

variable "hpa_fe_min_replicas" {
  description = "Frontend Horizontal Pod Autoscaling Min Replicas"
  type        = number
  default     = 1
}

variable "hpa_fe_max_replicas" {
  description = "Frontend Horizontal Pod Autoscaling Max Replicas"
  type        = number
  default     = 10
}

variable "hpa_fe_avg_cpu_utilization" {
  description = "Frontend Horizontal Pod Autoscaling Average CPU Utilization "
  type        = number
  default     = 75
}

variable "hpa_min_replicas" {
  description = "Horizontal Pod Autoscaling Min Replicas"
  type        = number
  default     = 1
}

variable "hpa_max_replicas" {
  description = "Horizontal Pod Autoscaling Max Replicas"
  type        = number
  default     = 10
}

variable "hpa_avg_cpu_utilization" {
  description = "Horizontal Pod Autoscaling Average CPU Utilization "
  type        = number
  default     = 75
}
