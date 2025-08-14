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

variable "subscription_id" {
  description = "Azure Subscription ID"
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

variable "storage_account_name" {
  description = "Blob Storage Account Name. Can only use lowercase letters for name."
  type        = string
}

variable "storage_account_tier" {
  description = "Blob Storage Account Tier. Can be Standard or Premium."
  type        = string
}

variable "storage_account_replication_type" {
  description = "Blob Storage Account Replication Type. Can be GRS or LRS."
  type        = string
}

variable "cert_email" {
  description = "Email Associated with the Let's Encrypt Cluster Issuer"
  type        = string
}

variable "dns_zone_name" {
  description = "DNS Zone Name"
  type        = string
}

variable "dns_zone_resource_group" {
  description = "DNS Zone Resource Group"
  type        = string
}

variable "unique_tls_akv_manager_identifier" {
  description = "A Unique Identifier for the ACME-bot function app that creates the TLS Certificates via a web dashboard."
  type        = string
}

variable "create_tls_akv_manager" {
  type        = bool
  default     = false # Default to not creating an ACME-bot function app
  description = "If true, creates an Azure Function app to create and manage Let's Encrypt TLS Certificates, which are stored in the Azure Key Vault."
}
