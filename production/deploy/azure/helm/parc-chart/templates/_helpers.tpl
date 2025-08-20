{{/*
Expand the name of the chart.
*/}}
{{- define "parc-chart.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
We truncate at 63 chars because some Kubernetes name fields are limited to this (by the DNS naming spec).
If release name contains chart name it will be used as a full name.
*/}}
{{- define "parc-chart.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Create chart version specific resource names.
*/}}
{{- define "parc-chart.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels.
The 'app' label uses .Values.serviceName to match the Terraform configuration's 'app = var.service'.
*/}}
{{- define "parc-chart.labels" -}}
helm.sh/chart: {{ include "parc-chart.chart" . }}
{{ include "parc-chart.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- range $key, $value := .Values.commonLabels }}
{{ $key | quote }}: {{ $value | quote }}
{{- end }}
{{- end }}

{{/*
Selector labels.
The 'app' label uses .Values.serviceName.
*/}}
{{- define "parc-chart.selectorLabels" -}}
app.kubernetes.io/name: {{ include "parc-chart.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app: {{ .Values.serviceName | quote }}
{{- end }}

{{/*
Generate the parameters.jsonl content based on values.yaml.
This replicates the logic from the Terraform 'locals' block:
  locals {
    original_json = jsondecode(file("${path.root}/${var.operator}_app_parameters.json"))
    updated_jsonl = join("\n", [
      for key, value in local.original_json : jsonencode({
        key   = "${var.operator}-${var.environment}-${key}"
        value = value
      })
    ])
  }
*/}}
{{- define "parc-chart.parametersJsonlContent" -}}
{{- $operator := .Values.parameters.operator -}}
{{- $environment := .Values.parameters.environment -}}
{{- $lines := list -}}
{{- if .Values.parameters.originalParametersJson }}
{{- range $key, $value := .Values.parameters.originalParametersJson -}}
{{- $newKey := printf "%s-%s-%s" $operator $environment $key -}}
{{- $dictForJson := dict "key" $newKey "value" $value }}
{{- $line := include "toJsonCompact" $dictForJson }}
{{- $lines = append $lines $line -}}
{{- end -}}
{{- end -}}
{{- join "\n" $lines }}
{{- end -}}

{{/* Helper to compact JSON, useful for JSONL */}}
{{- define "toJsonCompact" -}}
{{- . | toJson -}}
{{- end -}}
