"""Azure Function: POST /api/actuador

Envía comandos de control al ESP32 mediante tres métodos en cascada:
  1. Direct Method (azure-iot-hub SDK) — preferido, confirmación en tiempo real.
  2. C2D SDK       (azure-iot-hub SDK) — fallback cuando el dispositivo está offline.
  3. REST HTTP C2D (construcción manual de token SAS) — último recurso si el SDK falla.

El device_id se fuerza siempre desde la variable de entorno IOT_DEVICE_ID para
evitar que el cliente inyecte un identificador arbitrario.

Comandos válidos: ON, OFF, RESET.
"""
from __future__ import annotations

import base64
import hashlib
import hmac
import json
import logging
import os
import time
import urllib.parse
from typing import Dict

import azure.functions as func
import requests as http_requests

logger = logging.getLogger("control_actuador")

IOTHUB_SERVICE_CONN = os.environ.get("IOTHUB_SERVICE_CONNECTION_STRING", "")
IOT_DEVICE_ID = os.environ.get("IOT_DEVICE_ID", "cnc_fresadora_01")

COMANDOS_VALIDOS = {"ON", "OFF", "RESET"}

CORS_HEADERS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, x-functions-key",
}


def main(req: func.HttpRequest) -> func.HttpResponse:
    if req.method == "OPTIONS":
        return func.HttpResponse(status_code=204, headers=CORS_HEADERS)

    try:
        body = req.get_json()
    except ValueError:
        return _error_response("Cuerpo JSON inválido o ausente", 400)

    comando = str(body.get("comando", "")).upper().strip()
    if comando not in COMANDOS_VALIDOS:
        return _error_response(
            f"Comando inválido. Válidos: {', '.join(sorted(COMANDOS_VALIDOS))}", 400
        )

    # Siempre forzar el device_id del entorno para mitigar inyección de identificador.
    device_id = IOT_DEVICE_ID

    # --- Intento 1: Direct Method ---
    try:
        from azure.iot.hub import IoTHubRegistryManager
        from azure.iot.hub.models import CloudToDeviceMethod

        registry = IoTHubRegistryManager.from_connection_string(IOTHUB_SERVICE_CONN)
        method = CloudToDeviceMethod(
            method_name="actuador",
            payload={"comando": comando},
            response_timeout_in_seconds=10,
            connect_timeout_in_seconds=10,
        )
        result = registry.invoke_device_method(device_id, method)
        if result.status is not None and result.status < 400:
            logger.info("Comando '%s' entregado vía Direct Method a %s", comando, device_id)
            return _ok_response(
                {"ok": True, "delivered": "direct_method", "device_id": device_id}
            )
    except Exception as exc:
        logger.warning("Direct Method falló para %s: %s", device_id, exc)

    # --- Intento 2: SDK C2D ---
    try:
        from azure.iot.hub import IoTHubRegistryManager

        registry = IoTHubRegistryManager.from_connection_string(IOTHUB_SERVICE_CONN)
        registry.send_c2d_message(device_id, comando)
        logger.info("Comando '%s' encolado vía SDK C2D a %s", comando, device_id)
        return _ok_response(
            {"ok": True, "delivered": "c2d_sdk", "queued": True, "device_id": device_id}
        )
    except Exception as exc:
        logger.warning("SDK C2D falló para %s: %s", device_id, exc)

    # --- Intento 3: REST HTTP C2D (token SAS manual) ---
    try:
        ok = _send_c2d_rest(IOTHUB_SERVICE_CONN, device_id, comando)
        if ok:
            logger.info("Comando '%s' encolado vía REST C2D a %s", comando, device_id)
            return _ok_response(
                {"ok": True, "delivered": "c2d_rest", "queued": True, "device_id": device_id}
            )
    except Exception as exc:
        logger.warning("REST C2D falló para %s: %s", device_id, exc)

    return _error_response("Todos los métodos de entrega fallaron", 500)


def _send_c2d_rest(connection_string: str, device_id: str, payload: str) -> bool:
    """Construye un token SAS manualmente y hace POST al endpoint REST de IoT Hub C2D."""
    params: Dict[str, str] = {}
    for part in connection_string.split(";"):
        if "=" in part:
            key, _, value = part.partition("=")
            params[key] = value

    host = params.get("HostName", "")
    key_name = params.get("SharedAccessKeyName", "")
    key = params.get("SharedAccessKey", "")

    if not host or not key:
        raise ValueError("Cadena de conexión inválida o incompleta")

    resource = urllib.parse.quote(f"{host}/devices/{device_id}", safe="")
    expiry = int(time.time()) + 3600
    string_to_sign = f"{resource}\n{expiry}"
    signature = base64.b64encode(
        hmac.new(
            base64.b64decode(key),
            string_to_sign.encode("utf-8"),
            hashlib.sha256,
        ).digest()
    ).decode("utf-8")

    sas_token = (
        f"SharedAccessSignature sr={resource}"
        f"&sig={urllib.parse.quote(signature, safe='')}"
        f"&se={expiry}"
        f"&skn={key_name}"
    )

    url = (
        f"https://{host}/devices/{device_id}/messages/deviceBound"
        "?api-version=2020-09-30"
    )
    resp = http_requests.post(
        url,
        data=payload.encode("utf-8"),
        headers={
            "Authorization": sas_token,
            "Content-Type": "application/octet-stream",
        },
        timeout=10,
    )
    return resp.status_code in (200, 201, 204)


def _ok_response(data: dict) -> func.HttpResponse:
    return func.HttpResponse(
        body=json.dumps(data, ensure_ascii=False),
        status_code=200,
        mimetype="application/json",
        headers=CORS_HEADERS,
    )


def _error_response(message: str, status_code: int) -> func.HttpResponse:
    return func.HttpResponse(
        body=json.dumps({"error": message}, ensure_ascii=False),
        status_code=status_code,
        mimetype="application/json",
        headers=CORS_HEADERS,
    )
