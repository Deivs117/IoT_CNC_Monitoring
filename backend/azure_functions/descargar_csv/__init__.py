"""Azure Function: GET /api/datos/csv

Exporta toda la telemetría almacenada en Cosmos DB (CNCMonitor/Telemetry) como
un archivo CSV descargable, generado en memoria con io.StringIO.

Parámetros opcionales:
  - device_id  (str, filtra por dispositivo si se especifica)
"""
from __future__ import annotations

import csv
import io
import json
import logging
import os
from datetime import datetime, timezone

import azure.functions as func
from azure.cosmos import CosmosClient

logger = logging.getLogger("descargar_csv")

COSMOSDB_CONNECTION = os.environ["COSMOSDB_CONNECTION"]
COSMOS_DB = "CNCMonitor"
COSMOS_CONTAINER = "Telemetry"

CORS_HEADERS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, x-functions-key",
}

CSV_FIELDS = [
    "id",
    "device_id",
    "timestamp",
    "temperature",
    "humidity",
    "vibration_status",
    "vibration_anomaly_score",
    "visual_anomaly_score",
    "alert_active",
    "alert_reasons",
]


def main(req: func.HttpRequest) -> func.HttpResponse:
    if req.method == "OPTIONS":
        return func.HttpResponse(status_code=204, headers=CORS_HEADERS)

    device_id = req.params.get("device_id", "").strip() or None

    try:
        client = CosmosClient.from_connection_string(COSMOSDB_CONNECTION)
        container = (
            client.get_database_client(COSMOS_DB)
            .get_container_client(COSMOS_CONTAINER)
        )

        if device_id:
            query = (
                "SELECT * FROM c WHERE c.device_id = @device_id "
                "ORDER BY c.timestamp ASC"
            )
            params = [{"name": "@device_id", "value": device_id}]
        else:
            query = "SELECT * FROM c ORDER BY c.timestamp ASC"
            params = None

        items = list(
            container.query_items(
                query=query,
                parameters=params,
                enable_cross_partition_query=True,
            )
        )

        output = io.StringIO()
        writer = csv.DictWriter(output, fieldnames=CSV_FIELDS, extrasaction="ignore")
        writer.writeheader()

        for item in items:
            sensors = item.get("sensors") or {}
            predictions = item.get("predictions") or {}
            alerts = item.get("alerts") or {}
            writer.writerow(
                {
                    "id": item.get("id", ""),
                    "device_id": item.get("device_id", ""),
                    "timestamp": item.get("timestamp", ""),
                    "temperature": sensors.get("temperature", ""),
                    "humidity": sensors.get("humidity", ""),
                    "vibration_status": sensors.get("vibration_status", ""),
                    "vibration_anomaly_score": predictions.get("vibration_anomaly_score", ""),
                    "visual_anomaly_score": predictions.get("visual_anomaly_score", ""),
                    "alert_active": alerts.get("active", ""),
                    "alert_reasons": "; ".join(alerts.get("reasons") or []),
                }
            )

        ts = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        filename = f"telemetria_cnc_{ts}.csv"

        return func.HttpResponse(
            body=output.getvalue().encode("utf-8"),
            status_code=200,
            mimetype="text/csv",
            headers={
                **CORS_HEADERS,
                "Content-Disposition": f'attachment; filename="{filename}"',
            },
        )

    except Exception as exc:  # pragma: no cover
        logger.exception("Error al generar CSV: %s", exc)
        return func.HttpResponse(
            body=json.dumps({"error": "Error interno del servidor"}),
            status_code=500,
            mimetype="application/json",
            headers=CORS_HEADERS,
        )
