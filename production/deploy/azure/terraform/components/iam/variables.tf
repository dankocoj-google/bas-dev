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

variable "region" {
  description = "Region. Ex: eastus, westus, etc."
  type        = string
}

variable "operator" {
  description = "Azure operator"
  type        = string
}

variable "environment" {
  description = "Environment. Ex: dev, prod, etc."
  type        = string
}

variable "namespace" {
  description = "Kubernetes Namespace"
  type        = string
}

variable "resource_group_name" {
  description = "Resource group name"
  type        = string
}

variable "resource_group_id" {
  description = "Resource group ID"
  type        = string
}

variable "node_resource_group_id" {
  description = "Node Resource Group ID"
  type        = string
}

variable "vnet_id" {
  description = "Virtual Network ID"
  type        = string
}

variable "agfc_subnet_id" {
  description = "App Gateway for Containers Subnet ID"
  type        = string
}

variable "principal_id" {
  description = "Principal ID"
  type        = string
}

variable "kubelet_principal_id" {
  description = "Kubelet Principal ID"
  type        = string
}

variable "oidc_issuer_url" {
  description = "OpenID Connect Protocol Issuer URL"
  type        = string
}

variable "acr_id" {
  description = "Azure Container Registry Resource ID"
  type        = string
}

variable "dns_zone_name" {
  description = "DNS Zone Name"
  type        = string
}

variable "dns_zone_resource_group" {
  description = "DNS Resource Group"
  type        = string
}

variable "parc_service_account_name" {
  description = "Parc Service Account Name"
  type        = string
}

variable "parc_storage_account_resource_id" {
  description = "Parc Blob Storage Account Resource ID"
  type        = string
}

variable "use_byoc" {
  description = "Bring Your Own Certificate Approach. True if enabled"
  type        = bool
  default     = false
}

variable "aks_key_vault_addon_identity_object_id" {
  description = "The Object/Principal ID of the Azure Key Vault Secrets Provider add-on identity for the AKS cluster."
  type        = string
  default     = null
}

variable "tls_certificate_akv_name" {
  description = "Name of the TLS Certificate Azure Key Vault"
  type        = string
}

variable "tls_certificate_akv_resource_group_name" {
  description = "Name of the TLS Certificate Azure Key Vault"
  type        = string
}
