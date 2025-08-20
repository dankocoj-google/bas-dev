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

# Data resource to obtain Parc server Service IP Address
data "kubernetes_service" "parc_service" {
  metadata {
    name      = "${var.service}-service"
    namespace = var.namespace
  }
  depends_on = [helm_release.parc_service]
}

# Data Resource For Parc Service Account to have necessary role permissions
data "kubernetes_service_account" "parc_aks_sa" {
  metadata {
    name      = "${var.service}-service-account"
    namespace = var.namespace
  }
  depends_on = [helm_release.parc_service]
}

# Data Resource For Parc ConfigMap in order to track when the Parc and B&A servers need to be restarted.
data "kubernetes_config_map" "parc-parameters" {
  metadata {
    name      = "${var.service}-parameters"
    namespace = var.namespace
  }
  depends_on = [helm_release.parc_service]
}

# Data Resource for Parc Blob Storage Account stored in onetime project configuration
data "azurerm_storage_account" "parc_storage_account" {
  name                = var.storage_account_name
  resource_group_name = var.storage_account_resource_group
}

# Helm Release to install Parc Service, ConfigMap, and Deployment.
resource "helm_release" "parc_service" {
  name             = "${var.operator}-${var.environment}-${var.region}-parc-service"
  chart            = "../../../helm/parc-chart"
  namespace        = var.namespace
  create_namespace = true
  timeout          = 240
  atomic           = true

  values = [
    "${file("../../../helm/parc-chart/values.yaml")}",
    yamlencode({
      replicaCount = 1
      serviceName  = var.service
      parc = {
        port                         = var.parc_port
        userAssignedIdentityClientId = var.parc_user_assigned_identity_client_id
        image = {
          repository = var.parc_image
          pullPolicy = "Always"
          tag        = "latest"
        }
        otel = {
          collectorEndpoint = "collector.azure.internal"
          grpcPort          = 4317
        }
        blob_http_endpoint = data.azurerm_storage_account.parc_storage_account.primary_blob_endpoint
        useWorkloadAuth    = true
        useByoc            = var.use_byoc
        feService          = var.fe_service
      }
      parameters = {
        operator    = var.operator
        environment = var.environment
        originalParametersJson = jsondecode(templatefile("${path.root}/${var.operator}_app_parameters.json", {
          environment = var.environment
        }))
      }
      service = {
        annotations = {
          "service.beta.kubernetes.io/azure-load-balancer-internal" = "true"
        }
      }
      serviceAccount = {
        name = "${var.service}-service-account"
      }
      fullnameOverride = var.service
    })
  ]
}
