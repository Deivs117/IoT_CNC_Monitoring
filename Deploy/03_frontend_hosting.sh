#!/usr/bin/env bash
# =============================================================================
# 03_frontend_hosting.sh — Despliegue del Frontend estático para CNC PCB Monitor
#
# Pasos:
#   1. Habilitar Static Website en el Storage Account del frontend.
#   2. Inyectar API_BASE_URL y API_FUNCTION_KEY en app.js (placeholders → valores reales).
#   3. Ejecutar npm install && npm run build si existe package.json.
#   4. Subir archivos al contenedor $web mediante az storage blob upload-batch.
#   5. Actualizar CORS del Function App con la URL real del frontend.
#   6. Guardar FRONTEND_URL en infra_outputs.env.
#
# Requiere: infra_outputs.env generado por 01 y actualizado por 02.
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

for var in FRONTEND_SA FUNC_BASE_URL FUNC_APP_NAME RG_NAME; do
  if [[ -z "${!var:-}" ]]; then
    echo "ERROR: Variable '${var}' está vacía en ${ENV_FILE}." >&2
    exit 1
  fi
done

FRONTEND_SRC="${REPO_ROOT}/frontend"
BUILD_DIR="${FRONTEND_SRC}"

echo "==> [03] Desplegando frontend estático..."

# ---------------------------------------------------------------------------
# 1. Habilitar Static Website
# ---------------------------------------------------------------------------
echo "==> Habilitando Static Website en '${FRONTEND_SA}'..."
az storage blob service-properties update \
  --account-name "${FRONTEND_SA}" \
  --static-website \
  --index-document "index.html" \
  --404-document "index.html" \
  --output none

FRONTEND_URL=$(
  az storage account show \
    --name "${FRONTEND_SA}" \
    --resource-group "${RG_NAME}" \
    --query "primaryEndpoints.web" \
    --output tsv 2>/dev/null
)
# Quitar slash final si existe
FRONTEND_URL="${FRONTEND_URL%/}"

# ---------------------------------------------------------------------------
# 2. Preparar directorio de build en /tmp para no modificar el repo
# ---------------------------------------------------------------------------
TMP_BUILD=$(mktemp -d)
trap 'rm -rf "${TMP_BUILD}"' EXIT

cp -r "${FRONTEND_SRC}/." "${TMP_BUILD}/"

# ---------------------------------------------------------------------------
# 3. Inyectar variables en app.js (placeholders → valores reales)
# ---------------------------------------------------------------------------
APP_JS="${TMP_BUILD}/app.js"
if [[ -f "${APP_JS}" ]]; then
  echo "==> Inyectando API_BASE_URL y API_FUNCTION_KEY en app.js..."
  sed -i "s|__API_BASE_URL__|${FUNC_BASE_URL}|g" "${APP_JS}"
  sed -i "s|__API_FUNCTION_KEY__|${FUNC_KEY:-}|g" "${APP_JS}"
fi

# ---------------------------------------------------------------------------
# 4. Build npm (opcional — solo si existe package.json)
# ---------------------------------------------------------------------------
if [[ -f "${TMP_BUILD}/package.json" ]]; then
  echo "==> Ejecutando npm install && npm run build..."
  cd "${TMP_BUILD}"
  npm install --silent
  npm run build --silent
  # Si el build genera un subdirectorio dist/, apuntar ahí
  if [[ -d "${TMP_BUILD}/dist" ]]; then
    BUILD_DIR="${TMP_BUILD}/dist"
  else
    BUILD_DIR="${TMP_BUILD}"
  fi
else
  BUILD_DIR="${TMP_BUILD}"
fi

# ---------------------------------------------------------------------------
# 5. Subir archivos al contenedor $web
# ---------------------------------------------------------------------------
echo "==> Subiendo archivos al contenedor \$web de '${FRONTEND_SA}'..."
az storage blob upload-batch \
  --account-name "${FRONTEND_SA}" \
  --destination "\$web" \
  --source "${BUILD_DIR}" \
  --overwrite true \
  --output none

# ---------------------------------------------------------------------------
# 6. Actualizar CORS del Function App con la URL real
# ---------------------------------------------------------------------------
echo "==> Actualizando CORS del Function App con '${FRONTEND_URL}'..."
az functionapp cors remove \
  --name "${FUNC_APP_NAME}" \
  --resource-group "${RG_NAME}" \
  --allowed-origins "*" \
  --output none 2>/dev/null || true

az functionapp cors add \
  --name "${FUNC_APP_NAME}" \
  --resource-group "${RG_NAME}" \
  --allowed-origins "${FRONTEND_URL}" \
  --output none

# ---------------------------------------------------------------------------
# 7. Guardar FRONTEND_URL en infra_outputs.env
# ---------------------------------------------------------------------------
if grep -q "^FRONTEND_URL=" "${ENV_FILE}"; then
  sed -i "s|^FRONTEND_URL=.*|FRONTEND_URL=\"${FRONTEND_URL}\"|" "${ENV_FILE}"
else
  echo "FRONTEND_URL=\"${FRONTEND_URL}\"" >> "${ENV_FILE}"
fi

echo "==> [03] Frontend desplegado correctamente."
echo "    Dashboard: ${FRONTEND_URL}"
