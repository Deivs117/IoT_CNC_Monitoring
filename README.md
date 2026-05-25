# IoT CNC PCB Monitor

Sistema IoT + Edge AI + Serverless para monitoreo analítico y preventivo de una fresadora CNC usada en manufactura de PCBs.

---

## 1. Objetivo del proyecto

`IoT_CNC_Monitoring` integra firmware embebido, funciones serverless en Azure y un dashboard web para detectar en tiempo real desviaciones de temperatura, humedad y vibración que puedan dañar placas de cobre o romper herramientas.

La arquitectura está preparada para integrar más adelante una **ESP32-CAM** y una capa de visión artificial sin rediseñar el backend ni el esquema de almacenamiento.

---

## 2. Arquitectura de la solución

```
ESP32 (MPU-6050 + DHT22 + TF Lite)
  │  MQTT → Mosquitto (broker local)
  ▼
mqtt_bridge.py  (puente local Python)
  │  MQTT con TLS → Azure IoT Hub
  ▼
Azure IoT Hub
  │  Event Hub-compatible endpoint
  ▼
Azure Function: telemetry_processor  ←── EventHub Trigger
  │  • Parsea payload JSON
  │  • Evalúa alertas (temperatura, humedad, vibración, anomaly score)
  │  • Envía notificación por Telegram si hay alerta (opcional)
  │  • Escribe documento en Cosmos DB vía Output Binding
  ▼
Azure Cosmos DB (Serverless)
  Base de datos : CNCMonitor
  Contenedor    : Telemetry   (partition key: /device_id)
  │
  ├── Azure Function: get_datos       GET  /api/datos
  ├── Azure Function: descargar_csv   GET  /api/datos/csv
  └── Azure Function: control_actuador POST /api/actuador
              │  1. Direct Method (invoke_device_method)
              │  2. SDK C2D  (send_c2d_message)
              │  3. REST C2D (token SAS manual)
              ▼
           ESP32: recibe ON / OFF / RESET → GPIO actuador
  ▼
Frontend (Azure Static Website / GitHub Pages / Vercel)
  Dashboard oscuro con métricas, tabla, control y placeholder de cámara.
```

---

## 3. Estructura del repositorio

```text
IoT_CNC_Monitoring/
├── .gitignore
├── README.md
├── firmware/
│   ├── cnc_main_node/              ← Nodo principal ESP32
│   │   ├── cnc_main_node.ino
│   │   ├── config.h
│   │   ├── edge_impulse_vibration.h
│   │   └── sensors.h
│   └── cnc_camera_node/
│       └── .gitkeep                ← Reservado para ESP32-CAM (futuro)
├── backend/
│   ├── README.md                   ← Documentación del backend
│   ├── mqtt_bridge.py              ← Puente Mosquitto → Azure IoT Hub
│   ├── requirements.txt            ← Dependencias del bridge (paho-mqtt, azure-iot-device)
│   └── azure_functions/            ← Raíz del Function App
│       ├── host.json
│       ├── local.settings.json     ← Variables locales (no commitear con valores reales)
│       ├── requirements.txt        ← Dependencias para despliegue en Azure
│       ├── shared_code/
│       │   ├── __init__.py
│       │   └── alerts.py           ← Umbrales + Telegram Bot API
│       ├── telemetry_processor/    ← Ingestión IoT Hub → Cosmos DB
│       │   ├── __init__.py
│       │   └── function.json
│       ├── get_datos/              ← GET /api/datos
│       │   ├── __init__.py
│       │   └── function.json
│       ├── descargar_csv/          ← GET /api/datos/csv
│       │   ├── __init__.py
│       │   └── function.json
│       └── control_actuador/       ← POST /api/actuador (ON/OFF/RESET)
│           ├── __init__.py
│           └── function.json
├── frontend/
│   ├── README.md                   ← Documentación del frontend
│   ├── index.html                  ← Dashboard oscuro CNC PCB
│   ├── app.js                      ← Lógica de UI (polling, actuador, CSV)
│   └── style.css                   ← Tema oscuro
└── Deploy/
    ├── deploy.sh                   ← Orquestador (flags: --no-infra, --no-front, etc.)
    ├── 01_infraestructura.sh       ← RG + IoT Hub + Cosmos DB Serverless + Storage Accounts
    ├── 02_backend.sh               ← Function App + App Settings + publicación
    ├── 03_frontend_hosting.sh      ← Static Website + inyección de variables + CORS
    ├── 04_cleanup.sh               ← Eliminación del entorno (confirmación interactiva)
    └── infra_outputs.env.template  ← Plantilla de variables (copiar a infra_outputs.env)
```

---

## 4. Contrato JSON de telemetría

Payload publicado por el nodo principal vía MQTT:

```json
{
  "device_id": "cnc_fresadora_01",
  "timestamp": 1716076800,
  "sensors": {
    "temperature": 24.5,
    "humidity": 45.2,
    "vibration_status": "normal"
  },
  "predictions": {
    "vibration_anomaly_score": 0.02,
    "visual_anomaly_score": null
  }
}
```

`visual_anomaly_score` queda en `null` hasta integrar la ESP32-CAM. El backend y el frontend ya están preparados para recibirlo.

---

## 5. Documento almacenado en Cosmos DB

```json
{
  "id": "cnc_fresadora_01-1716076800-a1b2c3d4",
  "device_id": "cnc_fresadora_01",
  "timestamp": 1716076800,
  "sensors": {
    "temperature": 24.5,
    "humidity": 45.2,
    "vibration_status": "normal"
  },
  "predictions": {
    "vibration_anomaly_score": 0.02,
    "visual_anomaly_score": null
  },
  "alerts": {
    "active": false,
    "reasons": [],
    "telegram_sent": false
  },
  "raw_payload": { "...": "..." }
}
```

---

## 6. Azure Functions — endpoints

| Función | Trigger | Ruta | Auth |
|---|---|---|---|
| `telemetry_processor` | EventHub (IoT Hub) | — | — |
| `get_datos` | HTTP GET | `/api/datos` | function |
| `descargar_csv` | HTTP GET | `/api/datos/csv` | function |
| `control_actuador` | HTTP POST | `/api/actuador` | function |

### `get_datos` — parámetros opcionales
| Parámetro | Tipo | Default | Descripción |
|---|---|---|---|
| `limit` | int | 100 | Máximo 500 registros |
| `device_id` | string | todos | Filtrar por dispositivo |

### `descargar_csv` — parámetros opcionales
| Parámetro | Tipo | Descripción |
|---|---|---|
| `device_id` | string | Filtrar por dispositivo |

### `control_actuador` — body JSON
```json
{ "comando": "ON" }
```
Comandos válidos: `ON`, `OFF`, `RESET`.
El `device_id` se fuerza desde `IOT_DEVICE_ID` (variable de entorno), ignorando lo que envíe el cliente.

---

## 7. Variables de entorno requeridas

Todas las variables se configuran como App Settings en Azure (nunca hardcodeadas).
Para desarrollo local, completar `backend/azure_functions/local.settings.json`.

| Variable | Descripción |
|---|---|
| `AzureWebJobsStorage` | Cadena de conexión del Storage Account de Functions |
| `FUNCTIONS_WORKER_RUNTIME` | `python` |
| `IOTHUB_EVENTS_CONNECTION_STRING` | **Endpoint Event Hub-compatible** del IoT Hub — formato `Endpoint=sb://...` (para el trigger de `telemetry_processor`) |
| `IOT_HUB_EVENTHUB_NAME` | Nombre interno del Event Hub del IoT Hub |
| `IOTHUB_SERVICE_CONNECTION_STRING` | Cadena de conexión del servicio IoT Hub — formato `HostName=...` (para Direct Methods y C2D) |
| `IOT_DEVICE_ID` | ID del dispositivo ESP32 registrado en IoT Hub |
| `COSMOSDB_CONNECTION` | Cadena de conexión primaria de Cosmos DB |
| `TELEGRAM_BOT_TOKEN` | Token del bot de Telegram (de @BotFather) — opcional |
| `TELEGRAM_CHAT_ID` | ID del chat o grupo de Telegram para alertas — opcional |
| `TEMP_MIN` | Temperatura mínima normal (default: `15.0` °C) |
| `TEMP_MAX` | Temperatura máxima normal (default: `45.0` °C) |
| `HUM_MIN` | Humedad mínima normal (default: `20.0` %) |
| `HUM_MAX` | Humedad máxima normal (default: `80.0` %) |
| `VIBRATION_ANOMALY_THRESHOLD` | Score mínimo para disparar alerta (default: `0.80`) |

> **Importante:** `IOTHUB_EVENTS_CONNECTION_STRING` e `IOTHUB_SERVICE_CONNECTION_STRING` son cadenas con **formatos distintos**. El script `01_infraestructura.sh` las obtiene automáticamente con los comandos correctos del az CLI.

---

## 8. Flujo de despliegue

### Pre-requisitos

```bash
# Instalar az CLI y autenticarse
az login

# Instalar Azure Functions Core Tools
npm install -g azure-functions-core-tools@4 --unsafe-perm true
```

### Variables de Telegram (opcionales — antes de ejecutar)

```bash
export TELEGRAM_BOT_TOKEN="<token>"
export TELEGRAM_CHAT_ID="<chat_id>"
```

### Despliegue completo

```bash
cd Deploy/
./deploy.sh
```

### Despliegues parciales

```bash
./deploy.sh --no-infra      # Solo backend + frontend (infra ya existe)
./deploy.sh --no-front      # Solo infra + backend
./deploy.sh --only-backend  # Solo republica las Functions
./deploy.sh --only-front    # Solo actualiza el frontend
```

### Eliminar el entorno (ahorro de costos)

```bash
./04_cleanup.sh
# Pide confirmación interactiva antes de borrar.
# Usar FORCE_CLEANUP=true ./04_cleanup.sh en pipelines CI/CD.
```

---

## 9. Seguridad

- Ninguna credencial real se incluye en el repositorio. `local.settings.json` contiene únicamente placeholders.
- `Deploy/infra_outputs.env` está en `.gitignore`. Nunca commitear este archivo.
- Los endpoints HTTP usan `authLevel: "function"` para requerir una clave de acceso.
- El `device_id` del actuador se fuerza desde la variable de entorno `IOT_DEVICE_ID`.
- El script `01_infraestructura.sh` establece `chmod 600` en `infra_outputs.env`.
- Las cadenas de conexión nunca se imprimen en la salida estándar de los scripts.

---

## 10. Estado del proyecto y extensibilidad

### Listo para producción
- Pipeline completo: ESP32 → MQTT → IoT Hub → Cosmos DB → Dashboard
- Control de actuador (3 métodos en cascada: Direct Method, C2D SDK, REST C2D)
- Alertas de Telegram (opcional, se activa solo si los tokens están configurados)
- Scripts de despliegue repetibles con un solo comando

### Futuras extensiones
- **ESP32-CAM**: publicar `visual_anomaly_score` en el payload MQTT existente. El backend y la tabla del frontend ya leen ese campo; el placeholder del dashboard se activa automáticamente cuando el valor deja de ser `null`.
- **Nuevas alertas**: agregar condiciones en `shared_code/alerts.py` sin tocar las demás funciones.
- **Índices Cosmos DB**: para acelerar las consultas ordenadas por `timestamp`, agregar una política de índice compuesto `["/device_id ASC", "/timestamp DESC"]` en el contenedor `Telemetry`.
- **Frontend en Vercel**: ver `frontend/README.md` para instrucciones de despliegue en Vercel y GitHub Pages.
