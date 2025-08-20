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

variable "region" {
  description = "Region. Ex: eastus, westus, etc."
  type        = string
}

variable "environment" {
  description = "Environment. Ex: dev, prod, etc."
  type        = string
  validation {
    condition     = length(var.environment) <= 10
    error_message = "Due to current naming scheme limitations, environment must not be longer than 10."
  }
}

variable "operator" {
  description = "Operator. Ex: buyer1, seller1, etc"
  type        = string
}

variable "subscription_id" {
  description = "Azure Subscription ID"
  type        = string
}

variable "tenant_id" {
  description = "Azure Tenant ID"
  type        = string
}

variable "cert_email" {
  description = "Email used for Let's Encrypt Certificate"
  type        = string
}

variable "resource_group_name" {
  description = "Resource Group Name"
  type        = string
}

variable "resource_group_id" {
  description = "Resource Group ID"
  type        = string
}

variable "global_load_balancer_id" {
  description = "Global Load Balancer (Traffic Manager) Resource ID"
  type        = string
}

variable "geo_tie_break_routing_priority" {
  description = "Secondary Priority when Geographic Performance Routing is the same."
  type        = string
}

variable "private_dns_zone_name" {
  description = "Domain Name"
  type        = string
}

variable "availability_zones" {
  description = "List of Availability Zones"
  type        = list(string)
}

variable "namespace" {
  description = "Namespace"
  type        = string
}

variable "vnet_cidr" {
  description = "Virtual Network CIDR"
  type        = string
}

variable "default_subnet_cidr" {
  description = "Default Subnet CIDR"
  type        = string
}

variable "aks_subnet_cidr" {
  description = "Azure Kubernetes Service Subnet CIDR"
  type        = string
}

variable "cg_subnet_cidr" {
  description = "Container Group Subnet CIDR, used for Virtual Nodes"
  type        = string
}

variable "agfc_subnet_cidr" {
  description = "App Gateway for Containers Subnet CIDR"
  type        = string
}

variable "aks_service_cidr" {
  description = "Azure Kubernetes Service CIDR"
  type        = string
}

variable "aks_dns_service_cidr" {
  description = "Azure Kubernetes Service DNS Service CIDR"
  type        = string
}

variable "vn_replica_count" {
  description = "Virtual Node Replica Count"
  type        = number
}

variable "vn_admission_controller_replica_count" {
  description = "Virtual Node Admission Controller Replica Count"
  type        = number
}

variable "hpa_fe_min_replicas" {
  description = "Frontend Service Horizontal Pod Autoscaling Minimum Replicas"
  type        = number
}

variable "hpa_fe_max_replicas" {
  description = "Frontend Service Horizontal Pod Autoscaling Maximum Replicas"
  type        = number
}

variable "hpa_fe_avg_cpu_utilization" {
  description = "Frontend Service Horizontal Pod Autoscaling Average CPU Utilization"
  type        = number
}

variable "hpa_min_replicas" {
  description = "Horizontal Pod Autoscaling Minimum Replicas"
  type        = number
}

variable "hpa_max_replicas" {
  description = "Horizontal Pod Autoscaling Maximum Replicas"
  type        = number
}

variable "hpa_avg_cpu_utilization" {
  description = "Horizontal Pod Autoscaling Average CPU Utilization"
  type        = number
}

variable "dns_zone_name" {
  description = "DNS Zone Name"
  type        = string
}

variable "dns_zone_resource_group" {
  description = "DNS Resource Group"
  type        = string
}

variable "fe_grpc_port" {
  description = "Frontend gRPC port"
  type        = number
}

variable "grpc_port" {
  description = "gRPC port"
  type        = number
}

variable "otel_image" {
  description = "OTel Image with Version"
  type        = string
  default     = "otel/opentelemetry-collector-contrib:latest"
}

variable "otel_grpc_port" {
  description = "OTel gRPC Port"
  type        = number
}

variable "application_insights_otel_connection_string" {
  description = "Application Insights OTel Connection String"
  type        = string
}

variable "application_insights_otel_instrumentation_key" {
  description = "Application Insights OTel Instrumentation Key"
  type        = string
}

variable "monitor_workspace_id" {
  description = "Azure OTel Monitor Workspace ID"
  type        = string
}

variable "parc_image" {
  description = "Parc Server Image"
  type        = string
}

variable "parc_port" {
  description = "Parc gRPC Port"
  type        = number
  default     = 2000
}

variable "acr_id" {
  description = "Resource ID of the Azure Container Registry"
  type        = string
}

variable "fe_image" {
  description = "Frontend Server Image"
  type        = string
}

variable "image" {
  description = "Backend Server Image"
  type        = string
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

variable "use_byoc" {
  description = "Bring Your Own Certificate Approach. Enabled if true"
  type        = bool
  default     = false
}

variable "tls_certificate_akv_name" {
  description = "Name of the TLS Certificate Azure Key Vault"
  type        = string
}

variable "tls_certificate_akv_resource_group_name" {
  description = "Name of the TLS Certificate Azure Key Vault"
  type        = string
}

variable "certificate_name" {
  description = "Name of Bring Your Own Certificate"
  type        = string
}

variable "storage_account_name" {
  description = "Storage Account Name"
  type        = string
}

variable "storage_account_resource_group" {
  description = "Storage Account Resource Group"
  type        = string
}

variable "grafana_identity_principal_id" {
  description = "Grafana Identity ID"
  type        = string
  default     = null
}

variable "use_managed_monitoring" {
  description = "Enables managed prometheus and managed grafana if true."
  type        = bool
  default     = false
}
