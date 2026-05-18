"""Lógica de umbrales y notificaciones para telemetría CNC PCB."""
from __future__ import annotations

import os
from typing import Iterable, Optional

import requests

TEMP_MIN = float(os.getenv("TEMP_MIN", "15.0"))
TEMP_MAX = float(os.getenv("TEMP_MAX", "45.0"))
HUM_MIN = float(os.getenv("HUM_MIN", "20.0"))
HUM_MAX = float(os.getenv("HUM_MAX", "80.0"))
VIBRATION_ANOMALY_THRESHOLD = float(os.getenv("VIBRATION_ANOMALY_THRESHOLD", "0.70"))
NEUTRAL_VIBRATION_STATUSES = {"normal", "reposo", "desconocido"}


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

    normalized_status = (vibration_status or "").strip().lower()
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
    telegram_bot_token = os.getenv("TELEGRAM_BOT_TOKEN", "")
    telegram_chat_id = os.getenv("TELEGRAM_CHAT_ID", "")

    if not telegram_bot_token or not telegram_chat_id:
        return False

    response = requests.post(
        f"https://api.telegram.org/bot{telegram_bot_token}/sendMessage",
        json={"chat_id": telegram_chat_id, "text": message},
        timeout=10,
    )
    response.raise_for_status()
    return True
