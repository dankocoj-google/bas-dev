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

variable "use_default_namespace" {
  type        = bool
  default     = false # Default to using the environment variable for k8s namespace
  description = "If true, use the 'default' namespace instead of the environment variable."
}

variable "use_byoc" {
  type        = bool
  default     = false # Default to using Let's Encrypt Provided Certificate.
  description = "If true, use the 'bring your own certificate'approach with a custom certificate uploaded in the Azure Key Vault."
}

variable "seller_operators" {
  description = "List of sellers"
  type        = list(string)
  default     = ["seller1"]
}

variable "seller_regions" {
  description = "List of Seller regions"
  type        = list(string)
  default     = ["westus"]
}

variable "seller_cert_email" {
  description = "Seller TLS Certificate email using Let's Encrypt"
  type        = string
}

variable "seller_private_dns_zone_name" {
  description = "Seller Private DNS Zone Name"
  type        = string
}

variable "subscription_id" {
  description = "Azure Subscription ID"
  type        = string
}

variable "seller_vn_replica_count" {
  description = "Seller Virtual Node replica count"
  type        = number
  default     = 1
}

variable "seller_vn_admission_controller_replica_count" {
  description = "Seller Virtual Node admission controller replica count"
  type        = number
  default     = 1
}

variable "seller_hpa_fe_min_replicas" {
  description = "Seller Frontend Service Horizontal Pod Autoscaler minimum replicas"
  type        = number
  default     = 1
}

variable "seller_hpa_fe_max_replicas" {
  description = "Seller Frontend Service Horizontal Pod Autoscaler maximum replicas"
  type        = number
  default     = 10
}

variable "seller_hpa_fe_avg_cpu_utilization" {
  description = "Seller Frontend Service Horizontal Pod Autoscaler target CPU utilization"
  type        = number
  default     = 75
}

variable "seller_hpa_min_replicas" {
  description = "Seller Backend Service Horizontal Pod Autoscaler minimum replicas"
  type        = number
  default     = 1
}

variable "seller_hpa_max_replicas" {
  description = "Seller Backend Service Horizontal Pod Autoscaler maximum replicas"
  type        = number
  default     = 10
}

variable "seller_hpa_avg_cpu_utilization" {
  description = "Seller Backend Service Horizontal Pod Autoscaler Average CPU utilization"
  type        = number
  default     = 75
}

variable "seller_dns_zone_name" {
  description = "Seller DNS zone name"
  type        = string
}

variable "seller_dns_zone_resource_group" {
  description = "Seller DNS zone resource group name"
  type        = string
}

variable "seller_fe_grpc_port" {
  description = "Seller Frontend gRPC port"
  type        = number
  default     = 50053
}

variable "seller_fe_http_port" {
  description = "Seller Frontend HTTP port"
  type        = number
  default     = 51052
}

variable "seller_grpc_port" {
  description = "Seller general gRPC port"
  type        = number
  default     = 50061
}

variable "seller_otel_image" {
  description = "OTel Image with Version"
  type        = string
  default     = "otel/opentelemetry-collector-contrib:latest"
}

variable "seller_otel_grpc_port" {
  description = "Seller OTel gRPC port"
  type        = number
  default     = 4317
}

variable "seller_parc_image" {
  description = "Seller Parc Image"
  type        = string
}

variable "seller_parc_port" {
  description = "Seller Parc gRPC port"
  type        = number
  default     = 2000
}

variable "seller_acr_id" {
  description = "Resource ID of the Seller Azure Container Registry"
  type        = string
}

variable "seller_fe_image" {
  description = "Seller Frontend Server Image"
  type        = string
}

variable "seller_image" {
  description = "Seller Backend Server Image"
  type        = string
}

variable "seller_fe_ccepolicy" {
  description = "Seller Frontend Confidential Compute Policy. Default is a wildcard policy."
  type        = string
  default     = "cGFja2FnZSBwb2xpY3kKCmFwaV9zdm4gOj0gIjAuMTAuMCIKCm1vdW50X2RldmljZSA6PSB7ImFsbG93ZWQiOiB0cnVlfQptb3VudF9vdmVybGF5IDo9IHsiYWxsb3dlZCI6IHRydWV9CmNyZWF0ZV9jb250YWluZXIgOj0geyJhbGxvd2VkIjogdHJ1ZSwgImVudl9saXN0IjogbnVsbCwgImFsbG93X3N0ZGlvX2FjY2VzcyI6IHRydWV9CnVubW91bnRfZGV2aWNlIDo9IHsiYWxsb3dlZCI6IHRydWV9IAp1bm1vdW50X292ZXJsYXkgOj0geyJhbGxvd2VkIjogdHJ1ZX0KZXhlY19pbl9jb250YWluZXIgOj0geyJhbGxvd2VkIjogdHJ1ZSwgImVudl9saXN0IjogbnVsbH0KZXhlY19leHRlcm5hbCA6PSB7ImFsbG93ZWQiOiB0cnVlLCAiZW52X2xpc3QiOiBudWxsLCAiYWxsb3dfc3RkaW9fYWNjZXNzIjogdHJ1ZX0Kc2h1dGRvd25fY29udGFpbmVyIDo9IHsiYWxsb3dlZCI6IHRydWV9CnNpZ25hbF9jb250YWluZXJfcHJvY2VzcyA6PSB7ImFsbG93ZWQiOiB0cnVlfQpwbGFuOV9tb3VudCA6PSB7ImFsbG93ZWQiOiB0cnVlfQpwbGFuOV91bm1vdW50IDo9IHsiYWxsb3dlZCI6IHRydWV9CmdldF9wcm9wZXJ0aWVzIDo9IHsiYWxsb3dlZCI6IHRydWV9CmR1bXBfc3RhY2tzIDo9IHsiYWxsb3dlZCI6IHRydWV9CnJ1bnRpbWVfbG9nZ2luZyA6PSB7ImFsbG93ZWQiOiB0cnVlfQpsb2FkX2ZyYWdtZW50IDo9IHsiYWxsb3dlZCI6IHRydWV9CnNjcmF0Y2hfbW91bnQgOj0geyJhbGxvd2VkIjogdHJ1ZX0Kc2NyYXRjaF91bm1vdW50IDo9IHsiYWxsb3dlZCI6IHRydWV9Cg=="
}

variable "seller_ccepolicy" {
  description = "Seller Confidential Compute Policy. Default is a wildcard policy."
  type        = string
  default     = "cGFja2FnZSBwb2xpY3kKCmFwaV9zdm4gOj0gIjAuMTAuMCIKCm1vdW50X2RldmljZSA6PSB7ImFsbG93ZWQiOiB0cnVlfQptb3VudF9vdmVybGF5IDo9IHsiYWxsb3dlZCI6IHRydWV9CmNyZWF0ZV9jb250YWluZXIgOj0geyJhbGxvd2VkIjogdHJ1ZSwgImVudl9saXN0IjogbnVsbCwgImFsbG93X3N0ZGlvX2FjY2VzcyI6IHRydWV9CnVubW91bnRfZGV2aWNlIDo9IHsiYWxsb3dlZCI6IHRydWV9IAp1bm1vdW50X292ZXJsYXkgOj0geyJhbGxvd2VkIjogdHJ1ZX0KZXhlY19pbl9jb250YWluZXIgOj0geyJhbGxvd2VkIjogdHJ1ZSwgImVudl9saXN0IjogbnVsbH0KZXhlY19leHRlcm5hbCA6PSB7ImFsbG93ZWQiOiB0cnVlLCAiZW52X2xpc3QiOiBudWxsLCAiYWxsb3dfc3RkaW9fYWNjZXNzIjogdHJ1ZX0Kc2h1dGRvd25fY29udGFpbmVyIDo9IHsiYWxsb3dlZCI6IHRydWV9CnNpZ25hbF9jb250YWluZXJfcHJvY2VzcyA6PSB7ImFsbG93ZWQiOiB0cnVlfQpwbGFuOV9tb3VudCA6PSB7ImFsbG93ZWQiOiB0cnVlfQpwbGFuOV91bm1vdW50IDo9IHsiYWxsb3dlZCI6IHRydWV9CmdldF9wcm9wZXJ0aWVzIDo9IHsiYWxsb3dlZCI6IHRydWV9CmR1bXBfc3RhY2tzIDo9IHsiYWxsb3dlZCI6IHRydWV9CnJ1bnRpbWVfbG9nZ2luZyA6PSB7ImFsbG93ZWQiOiB0cnVlfQpsb2FkX2ZyYWdtZW50IDo9IHsiYWxsb3dlZCI6IHRydWV9CnNjcmF0Y2hfbW91bnQgOj0geyJhbGxvd2VkIjogdHJ1ZX0Kc2NyYXRjaF91bm1vdW50IDo9IHsiYWxsb3dlZCI6IHRydWV9Cg=="
}

variable "seller_frontend_cpu_limit" {
  description = "Seller Frontend Pod CPU Limit"
  type        = string
  default     = null
}

variable "seller_frontend_memory_limit" {
  description = "Seller Frontend Pod Memory Limit"
  type        = string
  default     = null
}

variable "seller_backend_cpu_limit" {
  description = "Seller Backend Pod CPU Limit"
  type        = string
  default     = null
}

variable "seller_backend_memory_limit" {
  description = "Seller Backend Pod Memory Limit"
  type        = string
  default     = null
}

variable "seller_certificate_name" {
  description = "Name of Bring Your Own Certificate"
  type        = string
}

variable "seller_tls_certificate_akv_name" {
  description = "Name of the TLS Certificate Azure Key Vault"
  type        = string
}

variable "seller_tls_certificate_akv_resource_group_name" {
  description = "Name of the TLS Certificate Azure Key Vault"
  type        = string
}

variable "seller_storage_account_name" {
  description = "Storage Account Name"
  type        = string
}

variable "seller_storage_account_resource_group" {
  description = "Storage Account Resource Group"
  type        = string
}

variable "seller_regional_config" {
  description = "Configuration map for each seller region, the geo_tie_break_routing_priority is the second routing method used if the geographic latency is the same, the availability zones provide reliability within a region, and the vnet prefixes (which need to be different by region due to the lack of Kube-Proxy) provide the highest bit of the vnet cidr."
  type = map(object({
    geo_tie_break_routing_priority = number
    availability_zones             = optional(list(string))
    vnet_prefix_high_bit           = number
  }))

  default = {
    "eastus" = {
      geo_tie_break_routing_priority = 1
      availability_zones             = ["eastus-1", "eastus-2", "eastus-3"]
      vnet_prefix_high_bit           = 10
    },
    "westus" = {
      geo_tie_break_routing_priority = 2
      availability_zones             = ["westus-1", "westus-2", "westus-3"]
      vnet_prefix_high_bit           = 11
    },
    "westeurope" = {
      geo_tie_break_routing_priority = 3
      availability_zones             = ["westeurope-1", "westeurope-2", "westeurope-3"]
      vnet_prefix_high_bit           = 12
    }
  }
}

variable "use_managed_monitoring" {
  description = "Enables managed prometheus and managed grafana if true."
  type        = bool
  default     = false
}
