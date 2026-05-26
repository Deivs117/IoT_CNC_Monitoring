"""Azure Function: POST /api/camara

Recibe y persiste la telemetría de inferencia visual desde la ESP32-CAM
(dispositivo cnc_camera_01) en el mismo contenedor Cosmos DB que el nodo
principal (CNCMonitor / Telemetry), usando device_id como partition key.

Este endpoint es independiente de telemetry_processor (que procesa el nodo
principal vía EventHub). Los documentos de cámara coexisten en el mismo
contenedor identificados por device_id = "cnc_camera_01".

Payload esperado (enviado por cnc_camera_node.ino):
{
  "device_id":  "cnc_camera_01",
  "timestamp":  1716076800,
  "camera": {
    "pcb_class":     "PCB_SMD",
    "confidence":    0.873,
    "inference_ms":  241,
    "model_version": "1.0.0",
    "probabilities": {
      "PCB_Mixta": 0.051,
      "PCB_SMD":   0.873,
      "PCB_TH":    0.028,
      "Sin_PCB":   0.048
    }
  }
}

Documento almacenado en Cosmos DB:
{
  "id":          "cnc_camera_01-1716076800-a1b2c3d4",
  "device_id":   "cnc_camera_01",
  "device_type": "camera",
  "timestamp":   1716076800,
  "camera": { ... },
  "raw_payload": { ... }
}

El endpoint GET /api/datos ya soporta filtrado por device_id, por lo que
el frontend puede leer estos documentos sin modificar get_datos.
"""
from __future__ import annotations

import json
import logging
import os
import time
import uuid
from typing import Any, Dict, Optional

import azure.functions as func
from azure.cosmos import CosmosClient
from azure.cosmos.exceptions import CosmosHttpResponseError

logger = logging.getLogger("ingestar_camara")

_COSMOS_CONN      = os.environ["COSMOSDB_CONNECTION"]
_COSMOS_DB        = "CNCMonitor"
_COSMOS_CONTAINER = "Telemetry"

VALID_CLASSES = frozenset({"PCB_Mixta", "PCB_SMD", "PCB_TH", "Sin_PCB"})
CAMERA_DEVICE_ID_DEFAULT = "cnc_camera_01"

CORS_HEADERS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, x-functions-key",
}

# Reutilizar cliente entre invocaciones para reducir latencia de reconexión
_cosmos_container = (
    CosmosClient.from_connection_string(_COSMOS_CONN)
    .get_database_client(_COSMOS_DB)
    .get_container_client(_COSMOS_CONTAINER)
)


def main(req: func.HttpRequest) -> func.HttpResponse:
    if req.method == "OPTIONS":
        return func.HttpResponse(status_code=204, headers=CORS_HEADERS)

    try:
        payload = req.get_json()
    except ValueError:
        return _error(400, "Cuerpo JSON inválido o ausente")

    validation_error = _validate_payload(payload)
    if validation_error:
        return _error(400, validation_error)

    try:
        document = _build_document(payload)
        _cosmos_container.upsert_item(document)
        logger.info("Documento de cámara guardado: %s", document["id"])
        return func.HttpResponse(
            body=json.dumps({"ok": True, "id": document["id"]}, ensure_ascii=False),
            status_code=201,
            mimetype="application/json",
            headers=CORS_HEADERS,
        )
    except CosmosHttpResponseError as exc:  # pragma: no cover
        logger.exception("Error de Cosmos DB al persistir documento: %s", exc)
        return _error(503, "Error al persistir en la base de datos")
    except Exception as exc:  # pragma: no cover  # pylint: disable=broad-except
        logger.exception("Error inesperado al procesar telemetría de cámara: %s", exc)
        return _error(500, "Error interno del servidor")


def _validate_payload(payload: Any) -> Optional[str]:
    """Valida la estructura mínima del payload de la ESP32-CAM."""
    if not isinstance(payload, dict):
        return "El payload debe ser un objeto JSON"

    camera = payload.get("camera")
    if camera is None:
        return "Campo 'camera' obligatorio"
    if not isinstance(camera, dict):
        return "El campo 'camera' debe ser un objeto"

    pcb_class = camera.get("pcb_class")
    if not pcb_class:
        return "Campo 'camera.pcb_class' obligatorio"
    if pcb_class not in VALID_CLASSES:
        return (
            f"'camera.pcb_class' inválido: '{pcb_class}'. "
            f"Valores válidos: {', '.join(sorted(VALID_CLASSES))}"
        )

    return None


def _build_document(payload: Dict[str, Any]) -> Dict[str, Any]:
    """Construye el documento Cosmos DB a partir del payload validado."""
    device_id = str(payload.get("device_id") or CAMERA_DEVICE_ID_DEFAULT)
    timestamp = _coerce_timestamp(payload.get("timestamp"))
    camera    = payload["camera"]

    # Normalizar probabilidades: incluir siempre las 4 clases con valor por defecto 0.0
    raw_probs   = camera.get("probabilities") or {}
    probs_clean = {
        cls: _coerce_float(raw_probs.get(cls), default=0.0)
        for cls in sorted(VALID_CLASSES)
    }

    return {
        "id":          f"{device_id}-{timestamp}-{uuid.uuid4().hex[:8]}",
        "device_id":   device_id,
        "device_type": "camera",
        "timestamp":   timestamp,
        "camera": {
            "pcb_class":     str(camera.get("pcb_class", "Sin_PCB")),
            "confidence":    _coerce_optional_float(camera.get("confidence")),
            "inference_ms":  int(camera.get("inference_ms") or 0),
            "model_version": str(camera.get("model_version") or "unknown"),
            "probabilities": probs_clean,
        },
        "raw_payload": payload,
    }


def _coerce_timestamp(value: Any) -> int:
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


def _error(status: int, message: str) -> func.HttpResponse:
    return func.HttpResponse(
        body=json.dumps({"error": message}, ensure_ascii=False),
        status_code=status,
        mimetype="application/json",
        headers=CORS_HEADERS,
    )
