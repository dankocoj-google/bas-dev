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

resource "helm_release" "bidding_auction_servers" {
  name             = "${var.operator}-${var.environment}-${var.region}-bidding-auction-servers"
  chart            = "../../../helm/bidding-auction-servers-chart"
  create_namespace = true
  namespace        = var.namespace
  timeout          = 300
  atomic           = true
  values = [
    "${file("../../../helm/bidding-auction-servers-chart/values.yaml")}",
    yamlencode({
      global = {
        namespace         = var.namespace
        environment       = var.environment
        operator          = var.operator
        region            = var.region
        availabilityZones = coalesce(var.availability_zones, [])
        parcConfigMapHash = var.parc_config_map_hash
      }
      frontend = {
        serviceName    = var.fe_service
        deploymentName = "${var.fe_service}-deployment"
        replicas       = var.fe_replicas
        image          = "${var.fe_image}:${var.environment}"
        ccePolicy      = var.fe_ccepolicy
        ports = {
          grpc = var.fe_grpc_port
          http = var.fe_http_port
        }

        enableHttpPort = var.fe_http_port != null ? true : false
        resources = {
          cpuLimit    = var.frontend_cpu_limit
          memoryLimit = var.frontend_memory_limit
        }
        hpa = {
          minReplicas           = var.hpa_fe_min_replicas
          maxReplicas           = var.hpa_fe_max_replicas
          averageCpuUtilization = var.hpa_fe_avg_cpu_utilization
        }
      }
      backend = {
        serviceName    = var.service
        deploymentName = "${var.service}-deployment"
        replicas       = var.replicas
        image          = "${var.image}:${var.environment}"
        ccePolicy      = var.ccepolicy
        ports = {
          grpc = var.grpc_port
        }
        resources = {
          cpuLimit    = var.backend_cpu_limit
          memoryLimit = var.backend_memory_limit
        }
        hpa = {
          minReplicas           = var.hpa_min_replicas
          maxReplicas           = var.hpa_max_replicas
          averageCpuUtilization = var.hpa_avg_cpu_utilization
        }
      }
    })
  ]
}

# Data resource provides the IP address of the Frontend Service
data "kubernetes_service" "fe_service" {
  metadata {
    name      = "${var.fe_service}-service"
    namespace = var.namespace
  }
  depends_on = [helm_release.bidding_auction_servers]
}

# Data resource provides the IP address of the Kubernetes Service
data "kubernetes_service" "service" {
  metadata {
    name      = "${var.service}-service"
    namespace = var.namespace
  }
  depends_on = [helm_release.bidding_auction_servers]
}
