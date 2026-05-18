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
# Valores por defecto (override con variables de entorno antes de ejecutar)
# ---------------------------------------------------------------------------
RG_NAME="${RG_NAME:-rg-cnc-iot}"
LOCATION="${LOCATION:-eastus}"
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

echo "==> [01] Iniciando aprovisionamiento de infraestructura..."
echo "    RG:          ${RG_NAME}"
echo "    Ubicación:   ${LOCATION}"
echo "    IoT Hub:     ${IOT_HUB_NAME}"
echo "    Cosmos DB:   ${COSMOS_ACCOUNT}"

# ---------------------------------------------------------------------------
# 1. Resource Group
# ---------------------------------------------------------------------------
echo "==> Creando Resource Group '${RG_NAME}'..."
az group create \
  --name "${RG_NAME}" \
  --location "${LOCATION}" \
  --output none

# ---------------------------------------------------------------------------
# 2. Azure IoT Hub (F1 free — upgrade a S1 en producción)
# ---------------------------------------------------------------------------
echo "==> Creando IoT Hub '${IOT_HUB_NAME}'..."
az iot hub create \
  --name "${IOT_HUB_NAME}" \
  --resource-group "${RG_NAME}" \
  --location "${LOCATION}" \
  --sku F1 \
  --partition-count 2 \
  --output none 2>/dev/null || \
az iot hub show \
  --name "${IOT_HUB_NAME}" \
  --resource-group "${RG_NAME}" \
  --output none

# Registro del dispositivo ESP32
echo "==> Registrando dispositivo '${IOT_DEVICE_ID}' en IoT Hub..."
az iot hub device-identity create \
  --hub-name "${IOT_HUB_NAME}" \
  --device-id "${IOT_DEVICE_ID}" \
  --output none 2>/dev/null || true

# ---------------------------------------------------------------------------
# 3. Azure Cosmos DB — modo Serverless
# ---------------------------------------------------------------------------
echo "==> Creando cuenta Cosmos DB '${COSMOS_ACCOUNT}' (Serverless)..."
az cosmosdb create \
  --name "${COSMOS_ACCOUNT}" \
  --resource-group "${RG_NAME}" \
  --locations regionName="${LOCATION}" failoverPriority=0 isZoneRedundant=false \
  --default-consistency-level "Session" \
  --capabilities EnableServerless \
  --output none

echo "==> Creando base de datos '${COSMOS_DB}'..."
az cosmosdb sql database create \
  --account-name "${COSMOS_ACCOUNT}" \
  --resource-group "${RG_NAME}" \
  --name "${COSMOS_DB}" \
  --output none

echo "==> Creando contenedor '${COSMOS_CONTAINER}' (partition-key: /device_id)..."
az cosmosdb sql container create \
  --account-name "${COSMOS_ACCOUNT}" \
  --resource-group "${RG_NAME}" \
  --database-name "${COSMOS_DB}" \
  --name "${COSMOS_CONTAINER}" \
  --partition-key-path "/device_id" \
  --output none

# ---------------------------------------------------------------------------
# 4. Storage Account para Azure Functions
# ---------------------------------------------------------------------------
echo "==> Creando Storage Account para Functions '${FUNC_STORAGE}'..."
az storage account create \
  --name "${FUNC_STORAGE}" \
  --resource-group "${RG_NAME}" \
  --location "${LOCATION}" \
  --sku Standard_LRS \
  --kind StorageV2 \
  --output none

# ---------------------------------------------------------------------------
# 5. Storage Account para Frontend (Static Website)
# ---------------------------------------------------------------------------
echo "==> Creando Storage Account para Frontend '${FRONTEND_SA}'..."
az storage account create \
  --name "${FRONTEND_SA}" \
  --resource-group "${RG_NAME}" \
  --location "${LOCATION}" \
  --sku Standard_LRS \
  --kind StorageV2 \
  --output none

# ---------------------------------------------------------------------------
# 6. Extraer cadenas de conexión y guardar en infra_outputs.env
#    Las variables sensibles se guardan sin imprimirlas en stdout.
# ---------------------------------------------------------------------------
echo "==> Extrayendo cadenas de conexión..."

IOTHUB_EVENTS_CONNECTION_STRING=$(
  az iot hub connection-string show \
    --hub-name "${IOT_HUB_NAME}" \
    --policy-name service \
    --key-type primary \
    --query "connectionString" \
    --output tsv 2>/dev/null
)

IOTHUB_SERVICE_CONNECTION_STRING="${IOTHUB_EVENTS_CONNECTION_STRING}"

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
echo "==> Guardando outputs en '${ENV_FILE}'..."
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

echo "==> [01] Infraestructura aprovisionada correctamente."
echo "    Archivo de outputs: ${ENV_FILE}"
