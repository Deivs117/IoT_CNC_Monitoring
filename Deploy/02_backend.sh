#!/usr/bin/env bash
# =============================================================================
# 02_backend.sh — Despliegue de Azure Functions para CNC PCB Monitor
#
# Pasos:
#   1. Crear el Function App (Consumption Plan, Python 3.11, Linux).
#   2. Publicar el código desde backend/azure_functions/.
#   3. Inyectar TODAS las variables de entorno requeridas en App Settings.
#   4. Configurar CORS.
#   5. Exportar FUNC_BASE_URL y FUNC_KEY en infra_outputs.env.
#
# Requiere: infra_outputs.env generado por 01_infraestructura.sh
#           TELEGRAM_BOT_TOKEN y TELEGRAM_CHAT_ID en el entorno o en infra_outputs.env
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENV_FILE="${SCRIPT_DIR}/infra_outputs.env"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "ERROR: No se encontró ${ENV_FILE}. Ejecuta primero 01_infraestructura.sh." >&2
  exit 1
fi

# shellcheck source=infra_outputs.env
source "${ENV_FILE}"

# Verificar variables obligatorias
for var in RG_NAME LOCATION FUNC_APP_NAME FUNC_STORAGE FUNC_STORAGE_CONN \
           IOTHUB_EVENTS_CONNECTION_STRING IOTHUB_SERVICE_CONNECTION_STRING \
           IOT_HUB_EVENTHUB_NAME IOT_DEVICE_ID COSMOSDB_CONNECTION; do
  if [[ -z "${!var:-}" ]]; then
    echo "ERROR: Variable '${var}' está vacía en ${ENV_FILE}." >&2
    exit 1
  fi
done

if [[ -z "${TELEGRAM_BOT_TOKEN:-}" ]] || [[ -z "${TELEGRAM_CHAT_ID:-}" ]]; then
  echo "ADVERTENCIA: TELEGRAM_BOT_TOKEN o TELEGRAM_CHAT_ID están vacíos. Las alertas de Telegram quedarán deshabilitadas." >&2
fi

echo "==> [02] Desplegando Azure Functions '${FUNC_APP_NAME}'..."

# ---------------------------------------------------------------------------
# 1. Crear Function App
# ---------------------------------------------------------------------------
echo "==> Creando Function App..."
az functionapp create \
  --name "${FUNC_APP_NAME}" \
  --resource-group "${RG_NAME}" \
  --storage-account "${FUNC_STORAGE}" \
  --consumption-plan-location "${LOCATION}" \
  --runtime python \
  --runtime-version "3.11" \
  --functions-version 4 \
  --os-type Linux \
  --output none 2>/dev/null || \
az functionapp show \
  --name "${FUNC_APP_NAME}" \
  --resource-group "${RG_NAME}" \
  --output none

# ---------------------------------------------------------------------------
# 2. Inyectar App Settings (sin imprimir valores sensibles)
# ---------------------------------------------------------------------------
echo "==> Configurando App Settings..."
az functionapp config appsettings set \
  --name "${FUNC_APP_NAME}" \
  --resource-group "${RG_NAME}" \
  --settings \
    "AzureWebJobsStorage=${FUNC_STORAGE_CONN}" \
    "FUNCTIONS_WORKER_RUNTIME=python" \
    "IOTHUB_EVENTS_CONNECTION_STRING=${IOTHUB_EVENTS_CONNECTION_STRING}" \
    "IOT_HUB_EVENTHUB_NAME=${IOT_HUB_EVENTHUB_NAME}" \
    "IOTHUB_SERVICE_CONNECTION_STRING=${IOTHUB_SERVICE_CONNECTION_STRING}" \
    "IOT_DEVICE_ID=${IOT_DEVICE_ID}" \
    "COSMOSDB_CONNECTION=${COSMOSDB_CONNECTION}" \
    "TELEGRAM_BOT_TOKEN=${TELEGRAM_BOT_TOKEN:-}" \
    "TELEGRAM_CHAT_ID=${TELEGRAM_CHAT_ID:-}" \
    "TEMP_MIN=${TEMP_MIN:-15.0}" \
    "TEMP_MAX=${TEMP_MAX:-45.0}" \
    "HUM_MIN=${HUM_MIN:-20.0}" \
    "HUM_MAX=${HUM_MAX:-80.0}" \
    "VIBRATION_ANOMALY_THRESHOLD=${VIBRATION_ANOMALY_THRESHOLD:-0.80}" \
  --output none

# ---------------------------------------------------------------------------
# 3. Publicar código
# ---------------------------------------------------------------------------
echo "==> Publicando código de Azure Functions desde backend/azure_functions/..."
cd "${REPO_ROOT}/backend/azure_functions"
func azure functionapp publish "${FUNC_APP_NAME}" --python --build remote

# ---------------------------------------------------------------------------
# 4. Obtener URL base y key por defecto
# ---------------------------------------------------------------------------
echo "==> Obteniendo URL y key del Function App..."
FUNC_BASE_URL="https://${FUNC_APP_NAME}.azurewebsites.net/api"

FUNC_KEY=$(
  az functionapp keys list \
    --name "${FUNC_APP_NAME}" \
    --resource-group "${RG_NAME}" \
    --query "functionKeys.default" \
    --output tsv 2>/dev/null || echo ""
)

# ---------------------------------------------------------------------------
# 5. Configurar CORS inicial (se actualizará con la URL real en 03_frontend.sh)
# ---------------------------------------------------------------------------
echo "==> Configurando CORS temporal (*)..."
az functionapp cors add \
  --name "${FUNC_APP_NAME}" \
  --resource-group "${RG_NAME}" \
  --allowed-origins "*" \
  --output none

# ---------------------------------------------------------------------------
# 6. Actualizar infra_outputs.env con nuevas variables
# ---------------------------------------------------------------------------
# Usar sed para actualizar sin reescribir todo el archivo
sed -i "s|^FUNC_BASE_URL=.*|FUNC_BASE_URL=\"${FUNC_BASE_URL}\"|" "${ENV_FILE}"

# Agregar FUNC_KEY si no existe (la clave no se imprime en stdout)
if grep -q "^FUNC_KEY=" "${ENV_FILE}"; then
  sed -i "s|^FUNC_KEY=.*|FUNC_KEY=\"${FUNC_KEY}\"|" "${ENV_FILE}"
else
  echo "FUNC_KEY=\"${FUNC_KEY}\"" >> "${ENV_FILE}"
fi

echo "==> [02] Azure Functions desplegadas correctamente."
echo "    URL base: ${FUNC_BASE_URL}"
