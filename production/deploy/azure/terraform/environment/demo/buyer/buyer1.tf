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

locals {
  environment         = terraform.workspace
  namespace           = var.use_default_namespace ? "default" : local.environment
  bfe_ips             = [module.buyer_westus.fe_service_ip]
  bidding_ips         = [module.buyer_westus.service_ip]
  buyer_collector_ips = [module.buyer_westus.collector_ip]
  buyer_parc_ips      = [module.buyer_westus.parc_ip]
  buyer_aks_vnet_ids  = { "${var.buyer_regions[1]}" = module.buyer_westus.aks_vnet_id }
}

terraform {
  required_version = ">= 1.2.3"

  required_providers {
    azurerm = {
      source  = "hashicorp/azurerm"
      version = ">= 4.29.0"
    }
    kubernetes = {
      source  = "hashicorp/kubernetes"
      version = ">= 2.36.0"
    }
    helm = {
      source  = "hashicorp/helm"
      version = "~> 2.0"
    }
  }

  # Comment out whole backend block if you want to store the tf state locally: not recommended because if you lost your local state, it will be painful to delete the whole stack manually
  # Follow this link to setup remote state: https://learn.microsoft.com/en-us/azure/developer/terraform/store-state-in-azure-storage?tabs=azure-cli
  backend "azurerm" {
    resource_group_name  = ""
    storage_account_name = ""
    container_name       = ""

    key = "terraform.tfstate"
  }
}

# Configure the Microsoft Azure Provider
provider "azurerm" {
  features {
    resource_group {
      prevent_deletion_if_contains_resources = false
    }
  }
  subscription_id = var.subscription_id
}

# Creates a resource group for each Buyer operator
module "buyer_resource_group" {
  for_each    = toset(var.buyer_operators)
  source      = "../../components/resource_group"
  region      = var.buyer_regions[0]
  operator    = each.value
  environment = local.environment
}

# Creates a Global Load Balancer (Traffic Manager) for each Buyer operator
module "buyer_global_load_balancing" {
  for_each                = module.buyer_resource_group
  source                  = "../../components/global_load_balancing"
  resource_group_name     = each.value.name
  operator                = each.value.operator
  environment             = local.environment
  dns_zone_name           = var.buyer_dns_zone_name
  dns_zone_resource_group = var.buyer_dns_zone_resource_group
}

# Creates and Populates a Private DNS Zone for each Buyer operator
module "buyer_dns" {
  for_each              = module.buyer_resource_group
  source                = "../../components/dns"
  region                = each.value.region
  operator              = each.value.operator
  environment           = local.environment
  resource_group_name   = each.value.name
  private_dns_zone_name = var.buyer_private_dns_zone_name
  fe_service_ips        = local.bfe_ips
  service_ips           = local.bidding_ips
  collector_ips         = local.buyer_collector_ips
  parc_ips              = local.buyer_parc_ips
  fe_service            = "bfe"
  service               = "bidding"
  aks_vnet_ids          = local.buyer_aks_vnet_ids
}

# Creates a OTel metrics dashboards for each Buyer operator
module "buyer_dashboards" {
  for_each            = module.buyer_resource_group
  source              = "../../components/dashboards"
  resource_group_name = each.value.name
  operator            = each.value.operator
  environment         = local.environment
  region              = each.value.region
  tftpl_name          = "buyer"
}

data "azurerm_client_config" "current" {}

resource "azurerm_user_assigned_identity" "managed_grafana_identity" {
  count               = var.use_managed_monitoring ? 1 : 0
  resource_group_name = module.buyer_resource_group[var.buyer_operators[1]].name
  location            = "westus"
  name                = "${local.environment}-grafana-mi"
}

resource "azurerm_dashboard_grafana" "managed_grafana" {
  count                 = var.use_managed_monitoring ? 1 : 0
  name                  = "${local.environment}-grafana1"
  resource_group_name   = module.buyer_resource_group[var.buyer_operators[1]].name
  location              = "westus"
  grafana_major_version = 11
  azure_monitor_workspace_integrations {
    resource_id = module.buyer_westus.prometheus_monitor_workspace_id
  }

  identity {
    type         = "UserAssigned"
    identity_ids = [one(azurerm_user_assigned_identity.managed_grafana_identity).id]
  }
}

resource "azurerm_role_assignment" "grafana_monitoring_reader" {
  count                = var.use_managed_monitoring ? 1 : 0
  scope                = "/subscriptions/${data.azurerm_client_config.current.subscription_id}"
  role_definition_name = "Monitoring Reader"
  principal_id         = one(azurerm_user_assigned_identity.managed_grafana_identity).principal_id
  description          = "Allow ${one(azurerm_dashboard_grafana.managed_grafana).name} to read Azure Monitor data for the subscription."
}

resource "azurerm_role_assignment" "grafana_admin_role" {
  count                = var.use_managed_monitoring ? 1 : 0
  scope                = one(azurerm_dashboard_grafana.managed_grafana).id
  role_definition_name = "Grafana Admin"
  principal_id         = data.azurerm_client_config.current.object_id
}

# In order to have multi-regional support using terraform, it is important to make another module similar to this module, and to add the region to the regions list within the buyer_regional_config variable.
# This will create a new AKS cluster and the necessary resources in order to have multiple regional clusters in the SAME resource group.
module "buyer_westus" {
  source                                        = "../../modules/buyer"
  resource_group_name                           = module.buyer_resource_group[var.buyer_operators[0]].name
  resource_group_id                             = module.buyer_resource_group[var.buyer_operators[0]].id
  region                                        = "westus"
  operator                                      = module.buyer_resource_group[var.buyer_operators[0]].operator
  environment                                   = local.environment
  subscription_id                               = data.azurerm_client_config.current.subscription_id
  tenant_id                                     = data.azurerm_client_config.current.tenant_id
  cert_email                                    = var.buyer_cert_email
  global_load_balancer_id                       = module.buyer_global_load_balancing[var.buyer_operators[0]].global_load_balancer_id
  geo_tie_break_routing_priority                = var.buyer_regional_config["westus"].geo_tie_break_routing_priority
  private_dns_zone_name                         = var.buyer_private_dns_zone_name
  availability_zones                            = var.buyer_regional_config["westus"].availability_zones
  namespace                                     = local.namespace
  vnet_cidr                                     = "${var.buyer_regional_config["westus"].vnet_prefix_high_bit}.0.0.0/12"
  default_subnet_cidr                           = "${var.buyer_regional_config["westus"].vnet_prefix_high_bit}.0.0.0/24"
  aks_subnet_cidr                               = "${var.buyer_regional_config["westus"].vnet_prefix_high_bit}.1.0.0/16"
  cg_subnet_cidr                                = "${var.buyer_regional_config["westus"].vnet_prefix_high_bit}.2.0.0/16"
  agfc_subnet_cidr                              = "${var.buyer_regional_config["westus"].vnet_prefix_high_bit}.3.0.0/16"
  aks_service_cidr                              = "${var.buyer_regional_config["westus"].vnet_prefix_high_bit}.4.0.0/16"
  aks_dns_service_cidr                          = "${var.buyer_regional_config["westus"].vnet_prefix_high_bit}.4.0.10"
  vn_replica_count                              = var.buyer_vn_replica_count
  vn_admission_controller_replica_count         = var.buyer_vn_admission_controller_replica_count
  hpa_fe_min_replicas                           = var.buyer_hpa_fe_min_replicas
  hpa_fe_max_replicas                           = var.buyer_hpa_fe_max_replicas
  hpa_fe_avg_cpu_utilization                    = var.buyer_hpa_fe_avg_cpu_utilization
  hpa_min_replicas                              = var.buyer_hpa_min_replicas
  hpa_max_replicas                              = var.buyer_hpa_max_replicas
  hpa_avg_cpu_utilization                       = var.buyer_hpa_avg_cpu_utilization
  dns_zone_name                                 = var.buyer_dns_zone_name
  dns_zone_resource_group                       = var.buyer_dns_zone_resource_group
  fe_grpc_port                                  = var.buyer_fe_grpc_port
  grpc_port                                     = var.buyer_grpc_port
  otel_grpc_port                                = var.buyer_otel_grpc_port
  otel_image                                    = var.buyer_otel_image
  application_insights_otel_connection_string   = module.buyer_dashboards[var.buyer_operators[0]].application_insights_otel_connection_string
  application_insights_otel_instrumentation_key = module.buyer_dashboards[var.buyer_operators[0]].application_insights_otel_instrumentation_key
  monitor_workspace_id                          = module.buyer_dashboards[var.buyer_operators[0]].monitor_workspace_id
  parc_image                                    = var.buyer_parc_image
  parc_port                                     = var.buyer_parc_port
  acr_id                                        = var.buyer_acr_id
  fe_image                                      = var.buyer_fe_image
  image                                         = var.buyer_image
  fe_ccepolicy                                  = var.buyer_fe_ccepolicy
  ccepolicy                                     = var.buyer_ccepolicy
  frontend_cpu_limit                            = var.buyer_frontend_cpu_limit
  frontend_memory_limit                         = var.buyer_frontend_memory_limit
  backend_cpu_limit                             = var.buyer_backend_cpu_limit
  backend_memory_limit                          = var.buyer_backend_memory_limit
  use_byoc                                      = var.use_byoc
  certificate_name                              = var.buyer_certificate_name
  tls_certificate_akv_name                      = var.buyer_tls_certificate_akv_name
  tls_certificate_akv_resource_group_name       = var.buyer_tls_certificate_akv_resource_group_name
  storage_account_name                          = var.buyer_storage_account_name
  storage_account_resource_group                = var.buyer_storage_account_resource_group
  grafana_identity_principal_id                 = var.use_managed_monitoring ? one(azurerm_user_assigned_identity.managed_grafana_identity).principal_id : null
  use_managed_monitoring                        = var.use_managed_monitoring
}
