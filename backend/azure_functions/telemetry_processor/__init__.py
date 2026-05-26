from __future__ import annotations

import json
import logging
import os
import time
import uuid
from typing import Any, Dict, List, Optional

import azure.functions as func
from azure.cosmos import CosmosClient
from requests import RequestException

from shared_code.alerts import compose_telegram_message, evaluate_alert, send_telegram_alert

logger = logging.getLogger("telemetry_processor")
RAW_EVENT_PREVIEW_LENGTH = 500

_COSMOS_CONN      = os.environ.get("COSMOSDB_CONNECTION", "")
_COSMOS_DB        = "CNCMonitor"
_COSMOS_CONTAINER = "Telemetry"

VALID_PCB_CLASSES = frozenset({"PCB_Mixta", "PCB_SMD", "PCB_TH", "Sin_PCB"})

# Instanciar el cliente una sola vez a nivel de módulo para reutilizarlo entre
# invocaciones y evitar overhead de conexión innecesario.
if not _COSMOS_CONN:
    raise EnvironmentError(
        "La variable de entorno COSMOSDB_CONNECTION no está configurada. "
        "Revisa local.settings.json o la configuración de la Function App."
    )
_cosmos_container = (
    CosmosClient.from_connection_string(_COSMOS_CONN)
    .get_database_client(_COSMOS_DB)
    .get_container_client(_COSMOS_CONTAINER)
)


def main(events: List[func.EventHubEvent]) -> None:
    for event in events:
        try:
            payload  = json.loads(event.get_body().decode("utf-8-sig"))
            # Dispatch: camera payload tiene campo "camera"; vibración no.
            if "camera" in payload:
                document = _build_camera_document(payload)
            else:
                document = _build_vibration_document(payload)
            _cosmos_container.upsert_item(document)
            logger.info("Documento guardado en Cosmos DB: %s", document["id"])
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            raw_preview = event.get_body().decode("utf-8", errors="replace")[:RAW_EVENT_PREVIEW_LENGTH]
            logger.exception("No fue posible procesar un evento: %s | body=%r", exc, raw_preview)
        except Exception as exc:  # pragma: no cover - runtime observability path
            logger.exception("Error inesperado procesando evento: %s", exc)


def _build_vibration_document(payload: Dict[str, Any]) -> Dict[str, Any]:
    """Construye el documento Cosmos DB para el nodo principal (vibración/sensores)."""
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


def _build_camera_document(payload: Dict[str, Any]) -> Dict[str, Any]:
    """Construye el documento Cosmos DB para el nodo ESP32-CAM (clasificación PCB)."""
    device_id = str(payload.get("device_id") or "cnc_camera_01")
    timestamp = _coerce_timestamp(payload.get("timestamp"))
    camera    = payload.get("camera") or {}

    # Normalizar probabilidades: incluir siempre las 4 clases
    raw_probs   = camera.get("probabilities") or {}
    probs_clean = {
        cls: _coerce_float(raw_probs.get(cls), default=0.0)
        for cls in sorted(VALID_PCB_CLASSES)
    }

    pcb_class = str(camera.get("pcb_class") or "Sin_PCB")
    if pcb_class not in VALID_PCB_CLASSES:
        logger.warning("pcb_class desconocido '%s' — forzando 'Sin_PCB'", pcb_class)
        pcb_class = "Sin_PCB"

    return {
        "id":          f"{device_id}-{timestamp}-{uuid.uuid4().hex[:8]}",
        "device_id":   device_id,
        "device_type": "camera",
        "timestamp":   timestamp,
        "camera": {
            "pcb_class":     pcb_class,
            "confidence":    _coerce_optional_float(camera.get("confidence")),
            "inference_ms":  int(camera.get("inference_ms") or 0),
            "model_version": str(camera.get("model_version") or "unknown"),
            "probabilities": probs_clean,
        },
        "raw_payload": payload,
    }


def _coerce_timestamp(value: Any) -> int:
    fallback_timestamp = int(time.time())
    if value is None:
        return fallback_timestamp

    try:
        return int(value)
    except (TypeError, ValueError):
        return fallback_timestamp


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
