"""Lógica de umbrales y notificaciones para telemetría CNC PCB."""
from __future__ import annotations

import os
import time
import threading
from typing import Dict, Iterable, Optional

import requests

TEMP_MIN = float(os.getenv("TEMP_MIN", "15.0"))
TEMP_MAX = float(os.getenv("TEMP_MAX", "45.0"))
HUM_MIN = float(os.getenv("HUM_MIN", "20.0"))
HUM_MAX = float(os.getenv("HUM_MAX", "80.0"))
VIBRATION_ANOMALY_THRESHOLD = float(os.getenv("VIBRATION_ANOMALY_THRESHOLD", "0.80"))
NEUTRAL_VIBRATION_STATUSES = {"normal", "reposo", "desconocido"}

# Cooldown entre recordatorios mientras la condición de fallo persiste (segundos).
# Por defecto 5 minutos (300 s). Configurable vía variable de entorno.
ALERT_COOLDOWN_SECONDS = int(os.getenv("ALERT_COOLDOWN_SECONDS", "300"))

# Estado de alertas por dispositivo, mantenido en memoria mientras el proceso viva.
# Estructura: { device_id: {"active": bool, "last_sent_at": float} }
# Permite aplicar la política anti-spam sin necesidad de almacenamiento externo.
# Nota: el lock protege lecturas/escrituras concurrentes si el worker usa múltiples
# hilos; en el modelo síncrono por defecto de Azure Functions esto es precautorio.
_alert_state: Dict[str, dict] = {}
_alert_state_lock = threading.Lock()


def evaluate_alert(
    temperature: float,
    humidity: float,
    vibration_status: str,
    vibration_anomaly_score: Optional[float],
) -> list[str]:
    reasons: list[str] = []

    if temperature < TEMP_MIN:
        reasons.append(f"Temperatura baja ({temperature:.2f}°C < {TEMP_MIN:.2f}°C)")
    elif temperature > TEMP_MAX:
        reasons.append(f"Temperatura alta ({temperature:.2f}°C > {TEMP_MAX:.2f}°C)")

    if humidity < HUM_MIN:
        reasons.append(f"Humedad baja ({humidity:.2f}% < {HUM_MIN:.2f}%)")
    elif humidity > HUM_MAX:
        reasons.append(f"Humedad alta ({humidity:.2f}% > {HUM_MAX:.2f}%)")

    normalized_status = normalize_status(vibration_status)
    if normalized_status and normalized_status not in NEUTRAL_VIBRATION_STATUSES:
        reasons.append(f"Estado vibracional reportado como {normalized_status}")

    if vibration_anomaly_score is not None and vibration_anomaly_score >= VIBRATION_ANOMALY_THRESHOLD:
        reasons.append(
            "Puntaje de anomalía vibracional alto "
            f"({vibration_anomaly_score:.3f} >= {VIBRATION_ANOMALY_THRESHOLD:.3f})"
        )

    return reasons


def compose_telegram_message(device_id: str, timestamp: int, reasons: Iterable[str]) -> str:
    joined_reasons = "\n- ".join(reasons)
    return (
        "🚨 Alerta CNC PCB\n"
        f"Dispositivo: {device_id}\n"
        f"Timestamp: {timestamp}\n"
        f"Motivos:\n- {joined_reasons}"
    )


def send_telegram_alert(message: str) -> bool:
    """Envía un mensaje a Telegram usando las variables de entorno TELEGRAM_BOT_TOKEN y TELEGRAM_CHAT_ID.

    Eleva requests.HTTPError si el servidor devuelve un código de error HTTP.
    Devuelve False si las credenciales no están configuradas.
    """
    telegram_bot_token = os.getenv("TELEGRAM_BOT_TOKEN", "")
    telegram_chat_id = os.getenv("TELEGRAM_CHAT_ID", "")

    if not telegram_bot_token or not telegram_chat_id:
        return False

    response = requests.post(
        f"https://api.telegram.org/bot{telegram_bot_token}/sendMessage",
        json={"chat_id": telegram_chat_id, "text": message, "parse_mode": "Markdown"},
        timeout=10,
    )
    response.raise_for_status()
    return True


def maybe_send_telegram_alert(device_id: str, message: str, has_alert: bool) -> bool:
    """Envía una alerta de Telegram aplicando política anti-spam con cooldown.

    Política:
    - Envía inmediatamente cuando se detecta una nueva condición de alerta
      (transición NORMAL → FALLO o primera detección).
    - Suprime notificaciones repetidas mientras la condición persiste y no ha
      transcurrido ALERT_COOLDOWN_SECONDS (por defecto 5 minutos).
    - Envía un recordatorio cuando el cooldown expira y la condición sigue activa.
    - Si la condición se resuelve (has_alert=False) limpia el estado en memoria,
      permitiendo que un futuro fallo vuelva a disparar una alerta inmediata.

    Args:
        device_id: Identificador del dispositivo, usado como clave de estado.
        message:   Texto del mensaje a enviar (ignorado cuando has_alert=False).
        has_alert: True si existe una condición de alerta activa, False si es NORMAL.

    Returns:
        True si se envió la notificación, False si fue suprimida o no había alerta.
    """
    if not has_alert:
        with _alert_state_lock:
            _alert_state.pop(device_id, None)
        return False

    now = time.time()
    with _alert_state_lock:
        state = _alert_state.get(device_id, {"active": False, "last_sent_at": 0.0})
        elapsed = now - state["last_sent_at"]
        should_send = not state["active"] or elapsed >= ALERT_COOLDOWN_SECONDS

    if should_send:
        sent = send_telegram_alert(message)
        if sent:
            with _alert_state_lock:
                _alert_state[device_id] = {"active": True, "last_sent_at": now}
        return sent

    return False


def normalize_status(vibration_status: Optional[str]) -> str:
    """Normaliza estados publicados por firmware o payloads parciales para comparaciones consistentes."""
    return (vibration_status or "").strip().lower()
