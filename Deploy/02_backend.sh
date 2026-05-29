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
FUNC_SRC="${REPO_ROOT}/backend/azure_functions"

# ---------------------------------------------------------------------------
# Helpers de log
# ---------------------------------------------------------------------------
log()  { echo "[02_backend] $*"; }
ok()   { echo "[02_backend] ✓ $*"; }
warn() { echo "[02_backend] ⚠ $*"; }

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "[02_backend] ERROR: No se encontró ${ENV_FILE}. Ejecuta primero 01_infraestructura.sh." >&2
  exit 1
fi

# Cargar secretos de Telegram si no están ya en el entorno (ejecución standalone)
# shellcheck source=_shared_env.sh
source "${SCRIPT_DIR}/_shared_env.sh"

# shellcheck source=infra_outputs.env
source "${ENV_FILE}"

# Verificar variables obligatorias
for var in RG_NAME LOCATION FUNC_APP_NAME FUNC_STORAGE FUNC_STORAGE_CONN \
           IOTHUB_EVENTS_CONNECTION_STRING IOTHUB_SERVICE_CONNECTION_STRING \
           IOT_HUB_EVENTHUB_NAME IOT_DEVICE_ID COSMOSDB_CONNECTION; do
  if [[ -z "${!var:-}" ]]; then
    echo "[02_backend] ERROR: Variable '${var}' está vacía en ${ENV_FILE}." >&2
    exit 1
  fi
done

if [[ -z "${TELEGRAM_BOT_TOKEN:-}" ]] || [[ -z "${TELEGRAM_CHAT_ID:-}" ]]; then
  warn "TELEGRAM_BOT_TOKEN o TELEGRAM_CHAT_ID están vacíos. Las alertas de Telegram quedarán deshabilitadas."
fi

# Validar herramientas
if ! command -v func &>/dev/null; then
  echo "[02_backend] ERROR: 'func' (Azure Functions Core Tools) no está instalado." >&2
  echo "  Instala con: npm install -g azure-functions-core-tools@4" >&2
  exit 1
fi

# Validar directorio de código fuente
if [[ ! -d "${FUNC_SRC}" ]]; then
  echo "[02_backend] ERROR: Directorio '${FUNC_SRC}' no encontrado." >&2
  echo "  Verifica que el repositorio esté clonado correctamente." >&2
  exit 1
fi

log "Desplegando Azure Functions '${FUNC_APP_NAME}'..."

# ---------------------------------------------------------------------------
# 1. Crear Function App (idempotente)
# ---------------------------------------------------------------------------
log "Verificando Function App '${FUNC_APP_NAME}'..."
if az functionapp show \
     --name "${FUNC_APP_NAME}" \
     --resource-group "${RG_NAME}" &>/dev/null; then
  warn "Function App '${FUNC_APP_NAME}' ya existe — omitiendo creación."
else
  az functionapp create \
    --name "${FUNC_APP_NAME}" \
    --resource-group "${RG_NAME}" \
    --storage-account "${FUNC_STORAGE}" \
    --consumption-plan-location "${LOCATION}" \
    --runtime python \
    --runtime-version "3.11" \
    --functions-version 4 \
    --os-type Linux \
    --output none
  ok "Function App '${FUNC_APP_NAME}' creada (Python 3.11, consumption plan)."
fi

# ---------------------------------------------------------------------------
# 2. Inyectar App Settings (sin imprimir valores sensibles)
# ---------------------------------------------------------------------------
log "Configurando App Settings..."
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
ok "App Settings configurados."

# ---------------------------------------------------------------------------
# 3. Publicar código
# ---------------------------------------------------------------------------
log "Publicando código de Azure Functions desde backend/azure_functions/..."
cd "${FUNC_SRC}"
func azure functionapp publish "${FUNC_APP_NAME}" --python --build remote
ok "Código publicado en '${FUNC_APP_NAME}'."

# ---------------------------------------------------------------------------
# 4. Obtener URL base y key por defecto
# ---------------------------------------------------------------------------
log "Obteniendo URL y key del Function App..."
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
log "Configurando CORS temporal (*)..."
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

ok "Azure Functions desplegadas correctamente."
log "URL base: ${FUNC_BASE_URL}"
