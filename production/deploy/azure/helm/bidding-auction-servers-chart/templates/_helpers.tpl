{{/*
Expand the name of the chart.
*/}}
{{- define "bidding-auction-servers-chart.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create chart name and version as used by the chart label.
*/}}
{{- define "bidding-auction-servers-chart.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels. These are standard Helm labels and supplement any app-specific labels
derived from Terraform variables.
*/}}
{{- define "bidding-auction-servers-chart.labels" -}}
helm.sh/chart: {{ include "bidding-auction-servers-chart.chart" . }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
{{- end }}
