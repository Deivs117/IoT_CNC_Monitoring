#!/usr/bin/env bash
# =============================================================================
# 01_infraestructura.sh — Provisión de infraestructura Azure para CNC PCB Monitor
#
# Crea (de forma idempotente) todos los recursos necesarios:
#   · Resource Group
#   · Azure IoT Hub + registro del dispositivo ESP32
#   · Azure Cosmos DB (modo Serverless) → base de datos CNCMonitor, contenedor Telemetry
#   · Storage Account para Azure Functions
#   · Storage Account para el hosting estático del Frontend
#
# Genera el archivo Deploy/infra_outputs.env con todas las cadenas de conexión.
# NUNCA imprime cadenas de conexión ni claves en la salida estándar del script.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/infra_outputs.env"

# ---------------------------------------------------------------------------
# Helpers de log
# ---------------------------------------------------------------------------
log()  { echo "[01_infra] $*"; }
ok()   { echo "[01_infra] ✓ $*"; }
warn() { echo "[01_infra] ⚠ $*"; }

# ---------------------------------------------------------------------------
# Valores por defecto (override con variables de entorno antes de ejecutar)
# ---------------------------------------------------------------------------
RG_NAME="${RG_NAME:-rg-cnc-iot}"
LOCATION="${LOCATION:-centralus}"
IOT_HUB_NAME="${IOT_HUB_NAME:-cnc-iot-hub}"
IOT_DEVICE_ID="${IOT_DEVICE_ID:-cnc_fresadora_01}"
COSMOS_ACCOUNT="${COSMOS_ACCOUNT:-cnc-iot-cosmos}"
COSMOS_DB="CNCMonitor"
COSMOS_CONTAINER="Telemetry"
FUNC_APP_NAME="${FUNC_APP_NAME:-cnc-iot-func}"

# Sufijos únicos basados en hash del nombre del hub (deterministas entre runs)
HASH_SUFFIX=$(echo -n "${IOT_HUB_NAME}" | md5sum | head -c 8)
FUNC_STORAGE="${FUNC_STORAGE:-cnciotfunc${HASH_SUFFIX}}"
FRONTEND_SA="${FRONTEND_SA:-cnciotfront${HASH_SUFFIX}}"

log "Iniciando aprovisionamiento de infraestructura..."
log "  RG:          ${RG_NAME}"
log "  Ubicación:   ${LOCATION}"
log "  IoT Hub:     ${IOT_HUB_NAME}"
log "  Cosmos DB:   ${COSMOS_ACCOUNT}"

# ---------------------------------------------------------------------------
# 1. Resource Group
# ---------------------------------------------------------------------------
log "Verificando Resource Group '${RG_NAME}'..."
if az group show --name "${RG_NAME}" &>/dev/null; then
  warn "Resource Group '${RG_NAME}' ya existe — omitiendo creación."
else
  az group create \
    --name "${RG_NAME}" \
    --location "${LOCATION}" \
    --output none
  ok "Resource Group '${RG_NAME}' creado en '${LOCATION}'."
fi

# ---------------------------------------------------------------------------
# 2. Azure IoT Hub (F1 free — upgrade a S1 en producción)
# ---------------------------------------------------------------------------
log "Verificando IoT Hub '${IOT_HUB_NAME}'..."
if az iot hub show --name "${IOT_HUB_NAME}" --resource-group "${RG_NAME}" &>/dev/null; then
  warn "IoT Hub '${IOT_HUB_NAME}' ya existe — omitiendo creación."
else
  az iot hub create \
    --name "${IOT_HUB_NAME}" \
    --resource-group "${RG_NAME}" \
    --location "${LOCATION}" \
    --sku F1 \
    --partition-count 2 \
    --output none
  ok "IoT Hub '${IOT_HUB_NAME}' creado (SKU F1)."
fi

# Registro del dispositivo ESP32
log "Verificando dispositivo '${IOT_DEVICE_ID}' en IoT Hub..."
if az iot hub device-identity show \
     --hub-name "${IOT_HUB_NAME}" \
     --device-id "${IOT_DEVICE_ID}" &>/dev/null; then
  warn "Dispositivo '${IOT_DEVICE_ID}' ya existe — omitiendo creación."
else
  az iot hub device-identity create \
    --hub-name "${IOT_HUB_NAME}" \
    --device-id "${IOT_DEVICE_ID}" \
    --output none
  ok "Dispositivo '${IOT_DEVICE_ID}' registrado en IoT Hub."
fi

# ---------------------------------------------------------------------------
# 3. Azure Cosmos DB — modo Serverless
# ---------------------------------------------------------------------------
log "Verificando cuenta Cosmos DB '${COSMOS_ACCOUNT}'..."
if az cosmosdb show --name "${COSMOS_ACCOUNT}" --resource-group "${RG_NAME}" &>/dev/null; then
  warn "Cuenta Cosmos DB '${COSMOS_ACCOUNT}' ya existe — omitiendo creación."
else
  az cosmosdb create \
    --name "${COSMOS_ACCOUNT}" \
    --resource-group "${RG_NAME}" \
    --locations regionName="${LOCATION}" failoverPriority=0 isZoneRedundant=false \
    --default-consistency-level "Session" \
    --capabilities EnableServerless \
    --output none
  ok "Cuenta Cosmos DB '${COSMOS_ACCOUNT}' creada (Serverless)."
fi

log "Verificando base de datos Cosmos '${COSMOS_DB}'..."
if az cosmosdb sql database show \
     --account-name "${COSMOS_ACCOUNT}" \
     --resource-group "${RG_NAME}" \
     --name "${COSMOS_DB}" &>/dev/null; then
  warn "Base de datos '${COSMOS_DB}' ya existe — omitiendo creación."
else
  az cosmosdb sql database create \
    --account-name "${COSMOS_ACCOUNT}" \
    --resource-group "${RG_NAME}" \
    --name "${COSMOS_DB}" \
    --output none
  ok "Base de datos '${COSMOS_DB}' creada."
fi

log "Verificando contenedor Cosmos '${COSMOS_CONTAINER}'..."
if az cosmosdb sql container show \
     --account-name "${COSMOS_ACCOUNT}" \
     --resource-group "${RG_NAME}" \
     --database-name "${COSMOS_DB}" \
     --name "${COSMOS_CONTAINER}" &>/dev/null; then
  warn "Contenedor '${COSMOS_CONTAINER}' ya existe — omitiendo creación."
else
  az cosmosdb sql container create \
    --account-name "${COSMOS_ACCOUNT}" \
    --resource-group "${RG_NAME}" \
    --database-name "${COSMOS_DB}" \
    --name "${COSMOS_CONTAINER}" \
    --partition-key-path "/device_id" \
    --output none
  ok "Contenedor '${COSMOS_CONTAINER}' creado (partition-key: /device_id)."
fi

# ---------------------------------------------------------------------------
# 4. Storage Account para Azure Functions
# ---------------------------------------------------------------------------
log "Verificando Storage Account para Functions '${FUNC_STORAGE}'..."
if az storage account show --name "${FUNC_STORAGE}" --resource-group "${RG_NAME}" &>/dev/null; then
  warn "Storage account '${FUNC_STORAGE}' ya existe — omitiendo creación."
else
  az storage account create \
    --name "${FUNC_STORAGE}" \
    --resource-group "${RG_NAME}" \
    --location "${LOCATION}" \
    --sku Standard_LRS \
    --kind StorageV2 \
    --output none
  ok "Storage account '${FUNC_STORAGE}' creado."
fi

# ---------------------------------------------------------------------------
# 5. Storage Account para Frontend (Static Website)
# ---------------------------------------------------------------------------
log "Verificando Storage Account para Frontend '${FRONTEND_SA}'..."
if az storage account show --name "${FRONTEND_SA}" --resource-group "${RG_NAME}" &>/dev/null; then
  warn "Storage account '${FRONTEND_SA}' ya existe — omitiendo creación."
else
  az storage account create \
    --name "${FRONTEND_SA}" \
    --resource-group "${RG_NAME}" \
    --location "${LOCATION}" \
    --sku Standard_LRS \
    --kind StorageV2 \
    --output none
  ok "Storage account '${FRONTEND_SA}' creado."
fi

# ---------------------------------------------------------------------------
# 6. Extraer cadenas de conexión y guardar en infra_outputs.env
#    Las variables sensibles se guardan sin imprimirlas en stdout.
# ---------------------------------------------------------------------------
log "Extrayendo cadenas de conexión..."

# Event Hub-compatible endpoint used by the telemetry_processor EventHub trigger.
# Format: Endpoint=sb://<namespace>.servicebus.windows.net/;SharedAccessKeyName=...;SharedAccessKey=...;EntityPath=...
IOTHUB_EVENTS_CONNECTION_STRING=$(
  az iot hub connection-string show \
    --hub-name "${IOT_HUB_NAME}" \
    --default-eventhub \
    --query "connectionString" \
    --output tsv 2>/dev/null
)

# IoT Hub service connection string used by control_actuador (Direct Methods + C2D).
# Format: HostName=<hub>.azure-devices.net;SharedAccessKeyName=service;SharedAccessKey=...
IOTHUB_SERVICE_CONNECTION_STRING=$(
  az iot hub connection-string show \
    --hub-name "${IOT_HUB_NAME}" \
    --policy-name service \
    --key-type primary \
    --query "connectionString" \
    --output tsv 2>/dev/null
)

IOT_HUB_EVENTHUB_NAME=$(
  az iot hub show \
    --name "${IOT_HUB_NAME}" \
    --resource-group "${RG_NAME}" \
    --query "properties.eventHubEndpoints.events.path" \
    --output tsv 2>/dev/null
)

COSMOSDB_CONNECTION=$(
  az cosmosdb keys list \
    --name "${COSMOS_ACCOUNT}" \
    --resource-group "${RG_NAME}" \
    --type connection-strings \
    --query "connectionStrings[0].connectionString" \
    --output tsv 2>/dev/null
)

FUNC_STORAGE_CONN=$(
  az storage account show-connection-string \
    --name "${FUNC_STORAGE}" \
    --resource-group "${RG_NAME}" \
    --query "connectionString" \
    --output tsv 2>/dev/null
)

# ---------------------------------------------------------------------------
# Escribir infra_outputs.env (sin mostrar valores sensibles en consola)
# ---------------------------------------------------------------------------
if [[ -z "${TELEGRAM_BOT_TOKEN:-}" ]] || [[ -z "${TELEGRAM_CHAT_ID:-}" ]]; then
  warn "TELEGRAM_BOT_TOKEN y/o TELEGRAM_CHAT_ID no están definidos. Las alertas de Telegram quedarán inactivas hasta configurarlas en App Settings."
fi

log "Guardando outputs en '${ENV_FILE}'..."
cat > "${ENV_FILE}" <<ENVEOF
# Generado automáticamente por 01_infraestructura.sh — no modificar manualmente
RG_NAME="${RG_NAME}"
LOCATION="${LOCATION}"
IOT_HUB_NAME="${IOT_HUB_NAME}"
IOT_DEVICE_ID="${IOT_DEVICE_ID}"
COSMOS_ACCOUNT="${COSMOS_ACCOUNT}"
COSMOS_DB="${COSMOS_DB}"
COSMOS_CONTAINER="${COSMOS_CONTAINER}"
FUNC_STORAGE="${FUNC_STORAGE}"
FRONTEND_SA="${FRONTEND_SA}"
FUNC_APP_NAME="${FUNC_APP_NAME}"
IOT_HUB_EVENTHUB_NAME="${IOT_HUB_EVENTHUB_NAME}"
IOTHUB_EVENTS_CONNECTION_STRING="${IOTHUB_EVENTS_CONNECTION_STRING}"
IOTHUB_SERVICE_CONNECTION_STRING="${IOTHUB_SERVICE_CONNECTION_STRING}"
COSMOSDB_CONNECTION="${COSMOSDB_CONNECTION}"
FUNC_STORAGE_CONN="${FUNC_STORAGE_CONN}"
# Completar manualmente después de ejecutar 02_backend.sh:
FUNC_BASE_URL=""
FUNC_KEY=""
# Completar manualmente después de ejecutar 03_frontend_hosting.sh:
FRONTEND_URL=""
# Credenciales de Telegram (nunca hardcodear — completar en 02_backend.sh):
TELEGRAM_BOT_TOKEN="${TELEGRAM_BOT_TOKEN:-}"
TELEGRAM_CHAT_ID="${TELEGRAM_CHAT_ID:-}"
ENVEOF

chmod 600 "${ENV_FILE}"

ok "Infraestructura aprovisionada correctamente."
log "Archivo de outputs: ${ENV_FILE}"
