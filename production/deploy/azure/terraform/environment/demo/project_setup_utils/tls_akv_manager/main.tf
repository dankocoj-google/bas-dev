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

# These are all the resources needed to use the ACME-bot to create Let's Encrypt TLS Certs and store them in the Azure Key Vault.
# Then, the TLS Certs can be used by the B&A stack by specifying the cert name following the BYOC approach: production/deploy/azure/helm/byoc-tls-chart,
# making sure to modify <operator>_certificate_name, <operator>_tls_certificate_akv_name, and <operator>_tls_certificate_akv_resource_group_name.
# Operators should then navigate to https://func-tls-cert-${var.unique_tls_akv_manager_identifier}.azurewebsites.net/dashboard, login with their Microsoft Portal Login, and manually create their TLS certificates in the DNS Zone they have created.
# This implementation was largely derived from:  https://github.com/shibayan/keyvault-acmebot/wiki/Getting-Started
data "azurerm_client_config" "current" {}

# Resource to retrieve information about the function app in order to assign adequate roles
data "azurerm_windows_function_app" "acme_function_app" {
  name                = "func-tls-cert-${var.unique_tls_akv_manager_identifier}"
  resource_group_name = var.resource_group_name
  depends_on          = [module.keyvault_acmebot]
}

# Provides DNS Zone Contributor to entire DNS Zone Resource Group (allowing for subdomains)
data "azurerm_resource_group" "dns_zone_rg" {
  name = var.dns_zone_resource_group
}

# Grant Terraform permissions to manage certificates and secrets in Key Vault
resource "azurerm_role_assignment" "akv_certificates_administrator_role" {
  scope                = azurerm_key_vault.tls_cert_key_vault.id
  role_definition_name = "Key Vault Administrator"
  principal_id         = data.azurerm_windows_function_app.acme_function_app.identity[0].principal_id
}

# Grant Terraform permissions to manage certificates and secrets in Key Vault
resource "azurerm_role_assignment" "current_client_akv_certificates_administrator_role" {
  scope                = azurerm_key_vault.tls_cert_key_vault.id
  role_definition_name = "Key Vault Administrator"
  principal_id         = data.azurerm_client_config.current.object_id
}

# DNS Zone Contributor Role Assignment for the Managed Identity
resource "azurerm_role_assignment" "dns_zone_contributor_role_assignment" {
  scope                = data.azurerm_resource_group.dns_zone_rg.id
  role_definition_name = "DNS Zone Contributor"
  principal_id         = data.azurerm_windows_function_app.acme_function_app.identity[0].principal_id
}

resource "random_uuid" "user_impersonation" {}

resource "random_uuid" "app_role_issue" {}

resource "random_uuid" "app_role_revoke" {}

resource "time_rotating" "default" {
  rotation_days = 180
}

resource "azuread_application" "default" {
  display_name    = "tls-cert-acmebot-app-registration-${var.unique_tls_akv_manager_identifier}"
  identifier_uris = ["api://keyvault-acmebot-${var.unique_tls_akv_manager_identifier}"]
  owners          = [data.azurerm_client_config.current.object_id]

  api {
    requested_access_token_version = 2

    oauth2_permission_scope {
      admin_consent_description  = "Allow the application to access Acmebot on behalf of the signed-in user."
      admin_consent_display_name = "Access Acmebot"
      enabled                    = true
      id                         = random_uuid.user_impersonation.result
      type                       = "User"
      user_consent_description   = "Allow the application to access Acmebot on your behalf."
      user_consent_display_name  = "Access Acmebot"
      value                      = "user_impersonation"
    }
  }

  app_role {
    allowed_member_types = ["User", "Application"]
    description          = "Allow new and renew certificate"
    display_name         = "Acmebot.IssueCertificate"
    enabled              = true
    value                = "Acmebot.IssueCertificate"
    id                   = random_uuid.app_role_issue.result
  }

  app_role {
    allowed_member_types = ["User", "Application"]
    description          = "Allow revoke certificate"
    display_name         = "Acmebot.RevokeCertificate"
    enabled              = true
    value                = "Acmebot.RevokeCertificate"
    id                   = random_uuid.app_role_revoke.result
  }

  web {
    redirect_uris = ["https://func-tls-cert-${var.unique_tls_akv_manager_identifier}.azurewebsites.net/.auth/login/aad/callback"]

    implicit_grant {
      access_token_issuance_enabled = false
      id_token_issuance_enabled     = true
    }
  }
}

resource "azuread_service_principal" "default" {
  client_id = azuread_application.default.client_id
  owners    = [data.azurerm_client_config.current.object_id]

  app_role_assignment_required = false
}

resource "azuread_application_password" "default" {
  application_id = azuread_application.default.id
  end_date       = timeadd(timestamp(), "8760h")

  rotate_when_changed = {
    rotation = time_rotating.default.id
  }

  lifecycle {
    create_before_destroy = true
  }
}

# Create a Key Vault to store TLS certificates from ACME-bot
resource "azurerm_key_vault" "tls_cert_key_vault" {
  name                      = "tls-cert-key-vault"
  location                  = var.region
  resource_group_name       = var.resource_group_name
  tenant_id                 = data.azurerm_client_config.current.tenant_id
  purge_protection_enabled  = false
  sku_name                  = "standard"
  enable_rbac_authorization = true
}

# Deploys several resources in order to create a valid signed TLS-Cert by Let's Encrypt that is managed and renewed by the ACME-bot.
module "keyvault_acmebot" {
  source  = "shibayan/keyvault-acmebot/azurerm"
  version = "~> 3.0"

  app_base_name       = "tls-cert-${var.unique_tls_akv_manager_identifier}"
  resource_group_name = var.resource_group_name
  location            = var.region
  mail_address        = var.cert_email
  vault_uri           = azurerm_key_vault.tls_cert_key_vault.vault_uri

  azure_dns = {
    subscription_id = var.subscription_id
  }
  auth_settings = {
    enabled = true
    active_directory = {
      client_id            = azuread_application.default.client_id
      client_secret        = azuread_application_password.default.value
      tenant_auth_endpoint = "https://login.microsoftonline.com/${data.azurerm_client_config.current.tenant_id}/v2.0"
    }
  }

}
