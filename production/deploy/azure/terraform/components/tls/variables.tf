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

variable "operator" {
  description = "Operator. Ex: buyer1, seller1, etc."
  type        = string
}

variable "environment" {
  description = "Environment. Ex: dev, prod, etc."
  type        = string
}

variable "namespace" {
  description = "Namespace"
  type        = string
}

variable "tenant_id" {
  description = "Azure Tenant ID"
  type        = string
}

variable "resource_group_name" {
  description = "Resource Group Name"
  type        = string
}

variable "subscription_id" {
  description = "Azure Subscription ID"
  type        = string
}

variable "cert_email" {
  description = "Email Associated with the Let's Encrypt Cluster Issuer"
  type        = string
}

variable "user_assigned_client_id" {
  description = "User Assigned Client ID"
  type        = string
}

variable "agfc_resource_id" {
  description = "App Gateway for Containers (Resource) ID"
  type        = string
}

variable "agfc_fe_name" {
  description = "App Gateway for Containers (Frontend) Name"
  type        = string
}

variable "fe_grpc_port" {
  description = "Frontend GRPC Port"
  type        = string
}

variable "fe_service" {
  description = "Frontend Service Name"
  type        = string
}

variable "dns_zone_name" {
  description = "DNS Zone Name"
  type        = string
}

variable "use_byoc" {
  description = "Bring Your Own Certificate Approach. True if enabled"
  type        = bool
  default     = false
}

variable "tls_certificate_akv_name" {
  description = "Name of the TLS Certificate Azure Key Vault"
  type        = string
}

variable "certificate_name" {
  description = "Name of the BYO Certificate that was uploaded to Azure Key Vault"
  type        = string
}

variable "certificate_secret_name" {
  description = "Name given to K8s Secret storing BYOC Certificate and Private Key"
  type        = string
  default     = "certificate-secret"
}

variable "aks_key_vault_addon_identity_client_id" {
  description = "The Client ID of the Azure Key Vault Secrets Provider add-on identity for the AKS cluster."
  type        = string
  default     = null
}
