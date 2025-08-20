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

# Configure the Microsoft Azure Provider
provider "azurerm" {
  features {
    resource_group {
      prevent_deletion_if_contains_resources = false
    }
  }
  subscription_id = var.subscription_id
}

# Creates a resource group for a onetime project setup
resource "azurerm_resource_group" "rg" {
  name     = var.resource_group_name
  location = var.region
}

# Creates a shared Parc Blob Storage Account
resource "azurerm_storage_account" "parc_storage_account" {
  name                     = var.storage_account_name
  resource_group_name      = azurerm_resource_group.rg.name
  location                 = var.region
  account_tier             = var.storage_account_tier
  account_replication_type = var.storage_account_replication_type
}

# Creates an Azure Function app to create and manage Let's Encrypt TLS Certificates, which are stored in the Azure Key Vault.
# See production/deploy/azure/terraform/project_setup_utils/tls_akv_manager/main.tf for more details.
module "tls_akv_manager" {
  count                             = var.create_tls_akv_manager ? 1 : 0
  source                            = "./tls_akv_manager"
  subscription_id                   = var.subscription_id
  region                            = var.region
  cert_email                        = var.cert_email
  dns_zone_name                     = var.dns_zone_name
  dns_zone_resource_group           = var.dns_zone_resource_group
  resource_group_name               = var.resource_group_name
  unique_tls_akv_manager_identifier = var.unique_tls_akv_manager_identifier
}
