#!/usr/bin/env bash
# =============================================================================
# deploy.sh — Orquestador principal de despliegue para CNC PCB Monitor
#
# Ejecuta los scripts de infraestructura, backend y frontend en secuencia,
# midiendo el tiempo total de cada fase.
#
# Uso:
#   ./deploy.sh                   # Despliegue completo (infra + backend + frontend)
#   ./deploy.sh --no-infra        # Omite 01 (infraestructura ya existe)
#   ./deploy.sh --no-backend      # Omite 02 (Functions ya desplegadas)
#   ./deploy.sh --no-front        # Omite 03 (solo infra + backend)
#   ./deploy.sh --only-infra      # Solo ejecuta 01
#   ./deploy.sh --only-backend    # Solo ejecuta 02
#   ./deploy.sh --only-front      # Solo ejecuta 03
#
# Requiere:
#   · az CLI instalado y autenticado (az login)
#   · func CLI instalado (Azure Functions Core Tools)
#   · TELEGRAM_BOT_TOKEN y TELEGRAM_CHAT_ID en el entorno (para 02_backend.sh)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/infra_outputs.env"

# ---------------------------------------------------------------------------
# Cargar secretos de Telegram (interactivo si faltan, persistente si se acepta)
# ---------------------------------------------------------------------------
# shellcheck source=_shared_env.sh
source "${SCRIPT_DIR}/_shared_env.sh"

# ---------------------------------------------------------------------------
# Parseo de flags
# ---------------------------------------------------------------------------
RUN_INFRA=true
RUN_BACKEND=true
RUN_FRONT=true

for arg in "$@"; do
  case "${arg}" in
    --no-infra)     RUN_INFRA=false ;;
    --no-backend)   RUN_BACKEND=false ;;
    --no-front)     RUN_FRONT=false ;;
    --only-infra)   RUN_BACKEND=false; RUN_FRONT=false ;;
    --only-backend) RUN_INFRA=false;   RUN_FRONT=false ;;
    --only-front)   RUN_INFRA=false;   RUN_BACKEND=false ;;
    --help|-h)
      grep "^#" "${BASH_SOURCE[0]}" | head -25 | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "Flag desconocido: ${arg}" >&2; exit 1 ;;
  esac
done

# ---------------------------------------------------------------------------
# Verificaciones previas
# ---------------------------------------------------------------------------
echo "==> Verificando autenticación en Azure..."
if ! az account show --output none 2>/dev/null; then
  echo "ERROR: No hay sesión activa en az CLI. Ejecuta 'az login' primero." >&2
  exit 1
fi

SUBSCRIPTION=$(az account show --query "name" --output tsv)
echo "    Suscripción activa: ${SUBSCRIPTION}"

# Validar func CLI solo si el backend va a desplegarse
if [[ "${RUN_BACKEND}" == "true" ]]; then
  if ! command -v func &>/dev/null; then
    echo "ERROR: 'func' (Azure Functions Core Tools) no está instalado." >&2
    echo "  Instala con: npm install -g azure-functions-core-tools@4" >&2
    exit 1
  fi
fi

DEPLOY_START=$(date +%s)

# ---------------------------------------------------------------------------
# Fase 1 — Infraestructura
# ---------------------------------------------------------------------------
if [[ "${RUN_INFRA}" == "true" ]]; then
  T0=$(date +%s)
  echo ""
  echo "════════════════════════════════════════"
  echo " Fase 1/3 — Infraestructura"
  echo "════════════════════════════════════════"
  bash "${SCRIPT_DIR}/01_infraestructura.sh"
  echo "    Duración fase 1: $(( $(date +%s) - T0 ))s"
fi

# Cargar outputs si existen (necesarios para fases 2 y 3)
if [[ -f "${ENV_FILE}" ]]; then
  # shellcheck source=infra_outputs.env
  source "${ENV_FILE}"
fi

# ---------------------------------------------------------------------------
# Fase 2 — Backend (Azure Functions)
# ---------------------------------------------------------------------------
if [[ "${RUN_BACKEND}" == "true" ]]; then
  T0=$(date +%s)
  echo ""
  echo "════════════════════════════════════════"
  echo " Fase 2/3 — Backend (Azure Functions)"
  echo "════════════════════════════════════════"
  bash "${SCRIPT_DIR}/02_backend.sh"
  echo "    Duración fase 2: $(( $(date +%s) - T0 ))s"
  # Recargar por si 02 actualizó FUNC_BASE_URL / FUNC_KEY
  [[ -f "${ENV_FILE}" ]] && source "${ENV_FILE}"
fi

# ---------------------------------------------------------------------------
# Fase 3 — Frontend (Static Website)
# ---------------------------------------------------------------------------
if [[ "${RUN_FRONT}" == "true" ]]; then
  T0=$(date +%s)
  echo ""
  echo "════════════════════════════════════════"
  echo " Fase 3/3 — Frontend (Static Website)"
  echo "════════════════════════════════════════"
  bash "${SCRIPT_DIR}/03_frontend_hosting.sh"
  echo "    Duración fase 3: $(( $(date +%s) - T0 ))s"
  [[ -f "${ENV_FILE}" ]] && source "${ENV_FILE}"
fi

# ---------------------------------------------------------------------------
# Resumen final
# ---------------------------------------------------------------------------
TOTAL=$(( $(date +%s) - DEPLOY_START ))
echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║           ✅  Despliegue completado                      ║"
echo "╠══════════════════════════════════════════════════════════╣"
printf  "║  IoT Hub:      %-40s ║\n" "${IOT_HUB_NAME:-N/A}"
printf  "║  Cosmos DB:    %-40s ║\n" "${COSMOS_ACCOUNT:-N/A}"
printf  "║  Function App: %-40s ║\n" "${FUNC_APP_NAME:-N/A}"
printf  "║  API URL:      %-40s ║\n" "${FUNC_BASE_URL:-N/A}"
printf  "║  Dashboard:    %-40s ║\n" "${FRONTEND_URL:-N/A}"
if [[ -n "${TELEGRAM_BOT_TOKEN:-}" ]] && [[ -n "${TELEGRAM_CHAT_ID:-}" ]]; then
  printf  "║  Telegram:     %-40s ║\n" "✅ credenciales cargadas"
else
  printf  "║  Telegram:     %-40s ║\n" "⚠  alertas deshabilitadas"
fi
printf  "║  Tiempo total: %-40s ║\n" "${TOTAL}s"
echo "╚══════════════════════════════════════════════════════════╝"
