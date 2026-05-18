from __future__ import annotations

import json
import logging
import time
import uuid
from typing import Any, Dict, List, Optional

import azure.functions as func
from requests import RequestException

from shared_code.alerts import compose_telegram_message, evaluate_alert, send_telegram_alert

logger = logging.getLogger("telemetry_processor")


def main(events: List[func.EventHubEvent], documents: func.Out[List[Dict[str, Any]]]) -> None:
    batch: List[Dict[str, Any]] = []

    for event in events:
        try:
            payload = json.loads(event.get_body().decode("utf-8"))
            batch.append(_build_document(payload))
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:  # pragma: no cover - runtime observability path
            raw_preview = event.get_body().decode("utf-8", errors="replace")[:500]
            logger.exception("No fue posible procesar un evento: %s | body=%r", exc, raw_preview)

    if batch:
        documents.set(batch)
        logger.info("Se enviaron %s documentos a Cosmos DB", len(batch))


def _build_document(payload: Dict[str, Any]) -> Dict[str, Any]:
    device_id = str(payload.get("device_id") or "cnc_fresadora_01")
    timestamp = _coerce_timestamp(payload.get("timestamp"))

    sensors = payload.get("sensors") or {}
    predictions = payload.get("predictions") or {}

    temperature = _coerce_float(sensors.get("temperature"), default=0.0)
    humidity = _coerce_float(sensors.get("humidity"), default=0.0)
    vibration_status = str(sensors.get("vibration_status") or "desconocido")
    vibration_anomaly_score = _coerce_optional_float(predictions.get("vibration_anomaly_score"))
    visual_anomaly_score = _coerce_optional_float(predictions.get("visual_anomaly_score"))

    reasons = evaluate_alert(
        temperature=temperature,
        humidity=humidity,
        vibration_status=vibration_status,
        vibration_anomaly_score=vibration_anomaly_score,
    )

    document: Dict[str, Any] = {
        "id": f"{device_id}-{timestamp}-{uuid.uuid4().hex[:8]}",
        "device_id": device_id,
        "timestamp": timestamp,
        "sensors": {
            "temperature": temperature,
            "humidity": humidity,
            "vibration_status": vibration_status,
        },
        "predictions": {
            "vibration_anomaly_score": vibration_anomaly_score,
            "visual_anomaly_score": visual_anomaly_score,
        },
        "alerts": {
            "active": bool(reasons),
            "reasons": reasons,
        },
        "raw_payload": payload,
    }

    if reasons:
        message = compose_telegram_message(device_id=device_id, timestamp=timestamp, reasons=reasons)
        try:
            sent = send_telegram_alert(message)
            document["alerts"]["telegram_sent"] = sent
        except RequestException as exc:  # pragma: no cover - runtime observability path
            logger.exception("No fue posible enviar alerta por Telegram: %s", exc)
            document["alerts"]["telegram_sent"] = False
            document["alerts"]["telegram_error"] = str(exc)
    else:
        document["alerts"]["telegram_sent"] = False

    return document


def _coerce_timestamp(value: Any) -> int:
    if value is None:
        return int(time.time())

    try:
        return int(value)
    except (TypeError, ValueError):
        return int(time.time())


def _coerce_float(value: Any, default: float) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _coerce_optional_float(value: Any) -> Optional[float]:
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None
