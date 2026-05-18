#!/usr/bin/env bash
# =============================================================================
# 04_cleanup.sh — Eliminación completa del entorno Azure de CNC PCB Monitor
#
# ADVERTENCIA: Este script elimina el Resource Group y TODOS los recursos
# contenidos en él (IoT Hub, Cosmos DB, Function App, Storage Accounts).
# Esta acción es IRREVERSIBLE.
#
# Uso:
#   ./04_cleanup.sh                    # Pide confirmación interactiva
#   FORCE_CLEANUP=true ./04_cleanup.sh # Sin confirmación (útil en CI/CD)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/infra_outputs.env"

# ---------------------------------------------------------------------------
# Leer nombre del Resource Group
# ---------------------------------------------------------------------------
if [[ -f "${ENV_FILE}" ]]; then
  # shellcheck source=infra_outputs.env
  source "${ENV_FILE}"
fi

RG_NAME="${RG_NAME:-rg-cnc-iot}"

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║              ⚠  ADVERTENCIA: ELIMINACIÓN DE ENTORNO  ⚠          ║"
echo "╠══════════════════════════════════════════════════════════════════╣"
echo "║  Esto eliminará el Resource Group: ${RG_NAME}"
echo "║  y TODOS los recursos dentro de él:"
echo "║    · Azure IoT Hub"
echo "║    · Azure Cosmos DB"
echo "║    · Azure Function App"
echo "║    · Storage Accounts (Functions y Frontend)"
echo "║"
echo "║  Esta acción es IRREVERSIBLE y dejará de acumular costos."
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# ---------------------------------------------------------------------------
# Confirmación (interactiva o por variable de entorno)
# ---------------------------------------------------------------------------
if [[ "${FORCE_CLEANUP:-false}" != "true" ]]; then
  read -r -p "¿Confirmas la eliminación de '${RG_NAME}'? (escribe 'si' para continuar): " CONFIRM
  CONFIRM_LOWER=$(echo "${CONFIRM}" | tr '[:upper:]' '[:lower:]')
  if [[ "${CONFIRM_LOWER}" != "si" && "${CONFIRM_LOWER}" != "sí" && "${CONFIRM_LOWER}" != "yes" ]]; then
    echo "Operación cancelada. No se eliminó ningún recurso."
    exit 0
  fi
fi

# ---------------------------------------------------------------------------
# Eliminar Resource Group (--no-wait para no bloquear la terminal)
# ---------------------------------------------------------------------------
echo "==> Eliminando Resource Group '${RG_NAME}'... (proceso asíncrono)"
az group delete \
  --name "${RG_NAME}" \
  --yes \
  --no-wait

# ---------------------------------------------------------------------------
# Eliminar infra_outputs.env local
# ---------------------------------------------------------------------------
if [[ -f "${ENV_FILE}" ]]; then
  echo "==> Eliminando archivo local de outputs '${ENV_FILE}'..."
  rm -f "${ENV_FILE}"
fi

echo ""
echo "==> [04] Solicitud de eliminación enviada a Azure."
echo "    La eliminación real puede tardar varios minutos."
echo "    Puedes verificar el estado con:"
echo "    az group show --name '${RG_NAME}'"
