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

resource "helm_release" "otel_collector" {
  name             = "${var.operator}-${var.environment}-${var.region}-otel-collector"
  chart            = "../../../helm/otel-collector-chart"
  namespace        = var.namespace
  create_namespace = true
  timeout          = 240
  atomic           = true

  values = [
    "${file("../../../helm/otel-collector-chart/values.yaml")}",
    yamlencode({
      namespace                                     = var.namespace
      otel_grpc_port                                = var.otel_grpc_port
      application_insights_otel_connection_string   = var.application_insights_otel_connection_string
      application_insights_otel_instrumentation_key = var.application_insights_otel_instrumentation_key
      otel_replicas                                 = var.otel_replicas
      otel_image                                    = var.otel_image
      service = {
        annotations = {
        "service.beta.kubernetes.io/azure-load-balancer-internal" = "true" }
      }
    })
  ]
}

# Data resource to obtain IP from OTel collector IP service
data "kubernetes_service" "otel-collector_service" {
  metadata {
    name      = "otel-collector-service"
    namespace = var.namespace
  }
  depends_on = [helm_release.otel_collector]
}
