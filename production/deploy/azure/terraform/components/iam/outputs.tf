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

output "cert_manager_user_assigned_identity_client_id" {
  description = "Cert-Manager User Assigned Identity Client ID, this identity is used for tls certificate creation which allows the cluster-issuer resource to access the DNS zone for validation of ownership of the domain."
  value       = azurerm_user_assigned_identity.cert_manager_identity.client_id
}

output "parc_user_assigned_identity_client_id" {
  description = "Parc User Assigned Identity Client ID, this identity allows the Parc deployment pods to have read, write, and delete access to the Azure Storage Blob account."
  value       = azurerm_user_assigned_identity.parc_user_assigned_managed_identity.client_id
}
