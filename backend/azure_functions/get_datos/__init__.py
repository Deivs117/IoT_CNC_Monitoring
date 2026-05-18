"""Azure Function: GET /api/datos

Retorna los últimos 100 registros de telemetría desde Cosmos DB
(base de datos: CNCMonitor, contenedor: Telemetry), ordenados por timestamp descendente.

Parámetros opcionales:
  - limit      (int, 1-500, default 100)
  - device_id  (str, filtra por dispositivo si se especifica)
"""
from __future__ import annotations

import json
import logging
import os

import azure.functions as func
from azure.cosmos import CosmosClient

logger = logging.getLogger("get_datos")

COSMOSDB_CONNECTION = os.environ["COSMOSDB_CONNECTION"]
COSMOS_DB = "CNCMonitor"
COSMOS_CONTAINER = "Telemetry"

CORS_HEADERS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, x-functions-key",
}


def main(req: func.HttpRequest) -> func.HttpResponse:
    if req.method == "OPTIONS":
        return func.HttpResponse(status_code=204, headers=CORS_HEADERS)

    try:
        limit = max(1, min(int(req.params.get("limit", 100)), 500))
    except ValueError:
        limit = 100

    device_id = req.params.get("device_id", "").strip() or None

    try:
        client = CosmosClient.from_connection_string(COSMOSDB_CONNECTION)
        container = (
            client.get_database_client(COSMOS_DB)
            .get_container_client(COSMOS_CONTAINER)
        )

        if device_id:
            query = (
                f"SELECT * FROM c WHERE c.device_id = @device_id "
                f"ORDER BY c.timestamp DESC OFFSET 0 LIMIT {limit}"
            )
            params = [{"name": "@device_id", "value": device_id}]
        else:
            query = f"SELECT * FROM c ORDER BY c.timestamp DESC OFFSET 0 LIMIT {limit}"
            params = None

        items = list(
            container.query_items(
                query=query,
                parameters=params,
                enable_cross_partition_query=True,
            )
        )

        clean = [
            {k: v for k, v in item.items() if not k.startswith("_")}
            for item in items
        ]

        return func.HttpResponse(
            body=json.dumps(clean, ensure_ascii=False, default=str),
            status_code=200,
            mimetype="application/json",
            headers=CORS_HEADERS,
        )

    except Exception as exc:  # pragma: no cover
        logger.exception("Error al consultar Cosmos DB: %s", exc)
        return func.HttpResponse(
            body=json.dumps({"error": "Error interno del servidor"}),
            status_code=500,
            mimetype="application/json",
            headers=CORS_HEADERS,
        )
