#!/usr/bin/env bash
# =============================================================================
# _shared_env.sh — Helper de secretos compartido para los scripts de despliegue
#
# Responsabilidades:
#   1. Intentar cargar ~/iot_cnc_secrets.env si existe.
#   2. Si TELEGRAM_BOT_TOKEN o TELEGRAM_CHAT_ID siguen vacíos, pedirlos
#      de forma interactiva (el token en modo oculto).
#   3. Exportar ambas variables para la sesión actual.
#   4. Ofrecer guardarlas en ~/iot_cnc_secrets.env de forma segura (permisos 600).
#
# Uso (incluir con source desde cualquier script de Deploy/):
#   source "$(dirname "${BASH_SOURCE[0]}")/_shared_env.sh"
#
# Comportamiento según entorno:
#   - Si stdin NO es un terminal (pipeline, CI), se omite el prompt interactivo y
#     se continúa con los valores disponibles (vacíos generan advertencia, no error).
#   - Si stdin ES un terminal y faltan variables, se piden interactivamente.
# =============================================================================

_SECRETS_FILE="${HOME}/iot_cnc_secrets.env"

# ---------------------------------------------------------------------------
# Función interna: guardar o actualizar ~/iot_cnc_secrets.env de forma segura
# ---------------------------------------------------------------------------
_save_secrets_file() {
  local tmp_file
  tmp_file=$(mktemp)

  # Si ya existe el archivo, conservar otras variables que no sean Telegram
  if [[ -f "${_SECRETS_FILE}" ]]; then
    grep -v '^export TELEGRAM_BOT_TOKEN=' "${_SECRETS_FILE}" \
      | grep -v '^export TELEGRAM_CHAT_ID=' \
      | grep -v '^# Generado' \
      > "${tmp_file}" || true
  else
    # Archivo nuevo: escribir encabezado
    cat > "${tmp_file}" <<'HEADER'
# Generado por Deploy/_shared_env.sh
# Contiene secretos — permisos 600 — no subir al repositorio
# Para recargar en una nueva sesión: source ~/iot_cnc_secrets.env

HEADER
  fi

  # Escribir variables de Telegram al final
  {
    echo "export TELEGRAM_BOT_TOKEN=\"${TELEGRAM_BOT_TOKEN:-}\""
    echo "export TELEGRAM_CHAT_ID=\"${TELEGRAM_CHAT_ID:-}\""
  } >> "${tmp_file}"

  mv "${tmp_file}" "${_SECRETS_FILE}"
  chmod 600 "${_SECRETS_FILE}"
  echo "  ✅ Guardado en '${_SECRETS_FILE}' (permisos 600)."
}

# ---------------------------------------------------------------------------
# 1. Intentar cargar el archivo persistente si existe
# ---------------------------------------------------------------------------
if [[ -f "${_SECRETS_FILE}" ]]; then
  # shellcheck source=/dev/null
  source "${_SECRETS_FILE}"
fi

# ---------------------------------------------------------------------------
# 2. Si faltan variables y estamos en un terminal interactivo, pedirlas
# ---------------------------------------------------------------------------
if [[ -z "${TELEGRAM_BOT_TOKEN:-}" ]] || [[ -z "${TELEGRAM_CHAT_ID:-}" ]]; then
  if [[ -t 0 ]]; then
    echo ""
    echo "──────────────────────────────────────────────────────"
    echo "  Configuración de Telegram para alertas de CNC"
    echo "──────────────────────────────────────────────────────"
    if [[ -f "${_SECRETS_FILE}" ]]; then
      echo "  ℹ️  Se cargó '${_SECRETS_FILE}' pero faltan variables."
      echo "     Completa las que faltan a continuación."
    else
      echo "  No se encontró '${_SECRETS_FILE}'."
      echo "  Se pedirán las credenciales de Telegram."
    fi
    echo ""
    echo "  Si no tienes un bot de Telegram configurado, presiona"
    echo "  Enter dos veces para omitir (las alertas quedarán deshabilitadas)."
    echo "──────────────────────────────────────────────────────"
    echo ""

    if [[ -z "${TELEGRAM_BOT_TOKEN:-}" ]]; then
      while true; do
        # -s: modo oculto (sin eco en pantalla)
        read -r -s -p "  Introduce TELEGRAM_BOT_TOKEN: " _input_token
        echo ""
        if [[ -z "${_input_token}" ]]; then
          echo "  ⚠  TELEGRAM_BOT_TOKEN vacío — las alertas de Telegram quedarán deshabilitadas."
          break
        fi
        export TELEGRAM_BOT_TOKEN="${_input_token}"
        break
      done
    fi

    if [[ -z "${TELEGRAM_CHAT_ID:-}" ]]; then
      while true; do
        read -r -p "  Introduce TELEGRAM_CHAT_ID: " _input_chat
        echo ""
        if [[ -z "${_input_chat}" ]]; then
          echo "  ⚠  TELEGRAM_CHAT_ID vacío — las alertas de Telegram quedarán deshabilitadas."
          break
        fi
        export TELEGRAM_CHAT_ID="${_input_chat}"
        break
      done
    fi

    # -------------------------------------------------------------------------
    # 3. Ofrecer guardar en el archivo persistente
    # -------------------------------------------------------------------------
    if [[ -n "${TELEGRAM_BOT_TOKEN:-}" ]] || [[ -n "${TELEGRAM_CHAT_ID:-}" ]]; then
      echo ""
      if [[ -f "${_SECRETS_FILE}" ]]; then
        read -r -p "  ¿Actualizar '${_SECRETS_FILE}' con los valores ingresados? [s/N]: " _save_answer
      else
        read -r -p "  ¿Guardar credenciales en '${_SECRETS_FILE}' para próximas ejecuciones? [s/N]: " _save_answer
      fi

      if [[ "${_save_answer,,}" == "s" || "${_save_answer,,}" == "si" || "${_save_answer,,}" == "sí" || "${_save_answer,,}" == "y" || "${_save_answer,,}" == "yes" ]]; then
        _save_secrets_file
      else
        echo "  ℹ️  Credenciales disponibles solo para esta sesión."
      fi
    fi
    echo ""
  else
    # No es terminal: solo advertir si faltan
    if [[ -z "${TELEGRAM_BOT_TOKEN:-}" ]] || [[ -z "${TELEGRAM_CHAT_ID:-}" ]]; then
      echo "[_shared_env] ⚠ TELEGRAM_BOT_TOKEN y/o TELEGRAM_CHAT_ID no están definidos." >&2
      echo "[_shared_env]   Las alertas de Telegram quedarán deshabilitadas." >&2
    fi
  fi
fi

# Exportar las variables disponibles (aunque estén vacías, para no romper set -u)
export TELEGRAM_BOT_TOKEN="${TELEGRAM_BOT_TOKEN:-}"
export TELEGRAM_CHAT_ID="${TELEGRAM_CHAT_ID:-}"
