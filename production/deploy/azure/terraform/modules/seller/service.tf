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

provider "kubernetes" {
  host                   = module.aks.kube_config_host
  client_certificate     = base64decode(module.aks.kube_config_client_certificate)
  client_key             = base64decode(module.aks.kube_config_client_key)
  cluster_ca_certificate = base64decode(module.aks.kube_config_cluster_ca_certificate)
}

provider "helm" {
  debug = true
  kubernetes {
    host                   = module.aks.kube_config_host
    client_certificate     = base64decode(module.aks.kube_config_client_certificate)
    client_key             = base64decode(module.aks.kube_config_client_key)
    cluster_ca_certificate = base64decode(module.aks.kube_config_cluster_ca_certificate)
  }
}

module "networking" {
  source                = "../../components/networking"
  region                = var.region
  operator              = var.operator
  environment           = var.environment
  resource_group_name   = var.resource_group_name
  vnet_cidr             = var.vnet_cidr
  default_subnet_cidr   = var.default_subnet_cidr
  aks_subnet_cidr       = var.aks_subnet_cidr
  cg_subnet_cidr        = var.cg_subnet_cidr
  agfc_subnet_cidr      = var.agfc_subnet_cidr
  private_dns_zone_name = var.private_dns_zone_name
}

module "aks" {
  source               = "../../components/aks"
  region               = var.region
  operator             = var.operator
  environment          = var.environment
  resource_group_name  = var.resource_group_name
  aks_subnet_id        = module.networking.aks-subnet_id
  resource_group_id    = var.resource_group_id
  vnet_id              = module.networking.vnet_id
  monitor_workspace_id = var.monitor_workspace_id
  aks_service_cidr     = var.aks_service_cidr
  aks_dns_service_ip   = var.aks_dns_service_cidr
  namespace            = var.namespace
  availability_zones   = var.availability_zones
}

module "iam" {
  source                                  = "../../components/iam"
  region                                  = var.region
  operator                                = var.operator
  environment                             = var.environment
  namespace                               = var.namespace
  resource_group_name                     = var.resource_group_name
  resource_group_id                       = var.resource_group_id
  node_resource_group_id                  = module.aks.node_resource_group_id
  vnet_id                                 = module.networking.vnet_id
  agfc_subnet_id                          = module.networking.agfc-subnet_id
  principal_id                            = module.aks.principal_id
  kubelet_principal_id                    = module.aks.kubelet_principal_id
  oidc_issuer_url                         = module.aks.oidc_issuer_url
  acr_id                                  = var.acr_id
  dns_zone_name                           = var.dns_zone_name
  dns_zone_resource_group                 = var.dns_zone_resource_group
  parc_service_account_name               = module.parc.parc_service_account_name
  parc_storage_account_resource_id        = module.parc.parc_storage_account_resource_id
  tls_certificate_akv_name                = var.tls_certificate_akv_name
  tls_certificate_akv_resource_group_name = var.tls_certificate_akv_resource_group_name
  use_byoc                                = var.use_byoc
  aks_key_vault_addon_identity_object_id  = module.aks.aks_key_vault_addon_identity_object_id
}

module "virtual_node" {
  source                                = "../../components/virtual_node"
  aks_cluster_name                      = module.aks.name
  resource_group_name                   = var.resource_group_name
  region                                = var.region
  operator                              = var.operator
  environment                           = var.environment
  vn_replica_count                      = var.vn_replica_count
  vn_admission_controller_replica_count = var.vn_admission_controller_replica_count
  availability_zones                    = var.availability_zones
}

module "regional_load_balancing" {
  source                         = "../../components/regional_load_balancing"
  region                         = var.region
  operator                       = var.operator
  environment                    = var.environment
  resource_group_name            = var.resource_group_name
  subnet_id                      = module.networking.agfc-subnet_id
  traffic_manager_profile_id     = var.global_load_balancer_id
  geo_tie_break_routing_priority = var.geo_tie_break_routing_priority
}

module "bidding-auction" {
  source                = "../../components/bidding-auction"
  fe_service            = "sfe"
  fe_replicas           = var.hpa_fe_min_replicas
  fe_image              = var.fe_image
  image                 = var.image
  environment           = var.environment
  operator              = var.operator
  region                = var.region
  availability_zones    = var.availability_zones
  fe_grpc_port          = var.fe_grpc_port
  fe_http_port          = var.fe_http_port
  grpc_port             = var.grpc_port
  service               = "auction"
  namespace             = var.namespace
  parc_config_map_hash  = module.parc.parc_config_map_hash
  replicas              = var.hpa_min_replicas
  frontend_cpu_limit    = var.frontend_cpu_limit
  frontend_memory_limit = var.frontend_memory_limit
  backend_cpu_limit     = var.backend_cpu_limit
  backend_memory_limit  = var.backend_memory_limit
}

module "otel_collector" {
  source                                        = "../../components/otel_collector"
  namespace                                     = var.namespace
  environment                                   = var.environment
  region                                        = var.region
  operator                                      = var.operator
  resource_group_name                           = var.resource_group_name
  otel_image                                    = var.otel_image
  otel_grpc_port                                = var.otel_grpc_port
  application_insights_otel_connection_string   = var.application_insights_otel_connection_string
  application_insights_otel_instrumentation_key = var.application_insights_otel_instrumentation_key
}

module "prometheus" {
  count                         = var.use_managed_monitoring ? 1 : 0
  source                        = "../../components/prometheus"
  environment                   = var.environment
  region                        = var.region
  operator                      = var.operator
  resource_group_name           = var.resource_group_name
  aks_cluster_name              = module.aks.name
  aks_cluster_id                = module.aks.aks_id
  grafana_identity_principal_id = var.grafana_identity_principal_id
}

module "parc" {
  source                                = "../../components/parc"
  operator                              = var.operator
  environment                           = var.environment
  region                                = var.region
  resource_group_name                   = var.resource_group_name
  fe_service                            = "sfe"
  service                               = "parc"
  namespace                             = var.namespace
  parc_image                            = var.parc_image
  parc_port                             = var.parc_port
  parc_user_assigned_identity_client_id = module.iam.parc_user_assigned_identity_client_id
  use_byoc                              = var.use_byoc
  storage_account_name                  = var.storage_account_name
  storage_account_resource_group        = var.storage_account_resource_group
}

module "tls" {
  source                                 = "../../components/tls"
  use_byoc                               = var.use_byoc
  region                                 = var.region
  resource_group_name                    = var.resource_group_name
  operator                               = var.operator
  environment                            = var.environment
  tenant_id                              = var.tenant_id
  namespace                              = var.namespace
  subscription_id                        = var.subscription_id
  cert_email                             = var.cert_email
  user_assigned_client_id                = module.iam.cert_manager_user_assigned_identity_client_id
  agfc_resource_id                       = module.regional_load_balancing.agfc_id
  agfc_fe_name                           = module.regional_load_balancing.agfc_fe_name
  fe_grpc_port                           = var.fe_http_port
  fe_service                             = "sfe"
  dns_zone_name                          = var.dns_zone_name
  aks_key_vault_addon_identity_client_id = module.aks.aks_key_vault_addon_identity_client_id
  tls_certificate_akv_name               = var.tls_certificate_akv_name
  certificate_name                       = var.certificate_name
}
