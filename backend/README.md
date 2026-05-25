# Backend — CNC PCB IoT Monitoring

## Arquitectura

```
ESP32-C3 (MPU-6050 + DHT22 + TF Lite Micro)
  └── WiFi + MQTT (puerto 1883, sin TLS en microcontrolador)
        └── Mosquitto (broker local)
              └── mqtt_bridge.py  ← puente local Python
                    └── Azure IoT Hub
                          └── Event Hub (endpoint integrado)
                                └── Azure Function: telemetry_processor
                                      ├── Cosmos DB (CNCMonitor/Telemetry)
                                      └── Telegram Bot (alertas opcionales)

Frontend (dashboard web)
  ├── GET /api/datos        → Azure Function: get_datos       → Cosmos DB
  ├── GET /api/datos/csv    → Azure Function: descargar_csv   → Cosmos DB
  └── POST /api/actuador    → Azure Function: control_actuador → IoT Hub (Direct Method / C2D)
```

---

## Estructura de carpetas

```
backend/
├── mqtt_bridge.py                        # Puente Mosquitto → Azure IoT Hub
├── requirements.txt                      # Dependencias del bridge: paho-mqtt, azure-iot-device
└── azure_functions/                      # Raíz del Function App (desplegar con func CLI)
    ├── host.json                         # Configuración del runtime (extensionBundle 4.x)
    ├── local.settings.json               # Variables de entorno LOCALES — NO commitear
    ├── requirements.txt                  # Dependencias de las Functions para despliegue
    ├── shared_code/
    │   ├── __init__.py
    │   └── alerts.py                     # Umbrales de alerta + notificaciones Telegram
    ├── telemetry_processor/
    │   ├── __init__.py                   # EventHub Trigger → evalúa alertas → Cosmos DB
    │   └── function.json                 # Trigger: EventHub | Output Binding: Cosmos DB
    ├── get_datos/
    │   ├── __init__.py                   # GET /api/datos → últimas N lecturas de Cosmos DB
    │   └── function.json                 # HTTP GET, authLevel: function, route: datos
    ├── descargar_csv/
    │   ├── __init__.py                   # GET /api/datos/csv → CSV descargable
    │   └── function.json                 # HTTP GET, authLevel: function, route: datos/csv
    └── control_actuador/
        ├── __init__.py                   # POST /api/actuador → Direct Method / C2D al ESP32
        └── function.json                 # HTTP POST, authLevel: function, route: actuador
```

---

## Recursos Azure necesarios

| Recurso | Nombre por defecto | Propósito |
|---|---|---|
| Resource Group | `rg-cnc-iot` | Contenedor de todos los recursos |
| IoT Hub | `cnc-iot-hub` | Ingesta de mensajes del ESP32 |
| Cosmos DB (Serverless) | `cnc-iot-cosmos` | Almacén de telemetría |
| Storage Account (Functions) | `cnciotfunc<hash>` | AzureWebJobsStorage |
| Function App | `cnc-iot-func` | Runtime de las Azure Functions |
| Storage Account (Frontend) | `cnciotfront<hash>` | Static Website (opcional) |

> Los nombres por defecto se configuran en `01_infraestructura.sh` y pueden sobreescribirse con variables de entorno antes de ejecutar.

### Cosmos DB
- **Base de datos:** `CNCMonitor`
- **Contenedor:** `Telemetry`
- **Partition key:** `/device_id`

### Dispositivo IoT Hub
- **Device ID:** `cnc_fresadora_01` (debe coincidir con `DEVICE_ID` en `firmware/cnc_main_node/config.h`)

---

## Variables de entorno

Para desarrollo local, copiar la plantilla y rellenar valores reales en
`backend/azure_functions/local.settings.json` (nunca commitear con valores reales — está en `.gitignore`).

```json
{
  "IsEncrypted": false,
  "Values": {
    "AzureWebJobsStorage": "UseDevelopmentStorage=true",
    "FUNCTIONS_WORKER_RUNTIME": "python",
    "IOTHUB_EVENTS_CONNECTION_STRING": "Endpoint=sb://<namespace>.servicebus.windows.net/;...",
    "IOT_HUB_EVENTHUB_NAME": "iothub-ehub-<hub>-<id>",
    "IOTHUB_SERVICE_CONNECTION_STRING": "HostName=<hub>.azure-devices.net;SharedAccessKeyName=service;SharedAccessKey=...",
    "IOT_DEVICE_ID": "cnc_fresadora_01",
    "COSMOSDB_CONNECTION": "AccountEndpoint=https://<account>.documents.azure.com:443/;AccountKey=...;",
    "TELEGRAM_BOT_TOKEN": "",
    "TELEGRAM_CHAT_ID": "",
    "TEMP_MIN": "15.0",
    "TEMP_MAX": "45.0",
    "HUM_MIN": "20.0",
    "HUM_MAX": "80.0",
    "VIBRATION_ANOMALY_THRESHOLD": "0.80"
  }
}
```

### Descripción de cada variable

| Variable | Descripción | Dónde obtenerla |
|---|---|---|
| `AzureWebJobsStorage` | Cadena de conexión del Storage de Functions | Portal → Storage Account → Claves de acceso |
| `FUNCTIONS_WORKER_RUNTIME` | Siempre `python` | — |
| `IOTHUB_EVENTS_CONNECTION_STRING` | Endpoint Event Hub-compatible del IoT Hub (para el trigger de `telemetry_processor`) | Portal → IoT Hub → Endpoints integrados → **Punto de conexión compatible con Event Hubs**. Formato: `Endpoint=sb://...` |
| `IOT_HUB_EVENTHUB_NAME` | Nombre corto del Event Hub interno | Misma pantalla → **Nombre compatible con Event Hubs** |
| `IOTHUB_SERVICE_CONNECTION_STRING` | Cadena de conexión del servicio IoT Hub (para Direct Methods y C2D) | Portal → IoT Hub → Directivas de acceso compartido → **service** → Cadena de conexión principal. Formato: `HostName=...` |
| `IOT_DEVICE_ID` | ID del dispositivo ESP32 registrado | `cnc_fresadora_01` |
| `COSMOSDB_CONNECTION` | Cadena de conexión primaria de Cosmos DB | Portal → Cosmos DB → Claves → Cadena de conexión principal |
| `TELEGRAM_BOT_TOKEN` | Token del bot de alertas (de `@BotFather`) | Telegram → `@BotFather` → `/newbot` |
| `TELEGRAM_CHAT_ID` | ID del chat que recibe alertas | `https://api.telegram.org/bot<TOKEN>/getUpdates` → campo `chat.id` |
| `TEMP_MIN` / `TEMP_MAX` | Rango de temperatura normal (°C) | Default: `15.0` / `45.0` |
| `HUM_MIN` / `HUM_MAX` | Rango de humedad normal (%) | Default: `20.0` / `80.0` |
| `VIBRATION_ANOMALY_THRESHOLD` | Score mínimo para disparar alerta vibracional | Default: `0.80` |

> **Importante:** `IOTHUB_EVENTS_CONNECTION_STRING` e `IOTHUB_SERVICE_CONNECTION_STRING` son cadenas distintas con formatos distintos. El script `01_infraestructura.sh` las obtiene automáticamente con los comandos correctos de `az CLI`.

---

## Endpoints de la API

La URL base del Function App se obtiene al ejecutar `02_backend.sh` y queda guardada en `Deploy/infra_outputs.env` como `FUNC_BASE_URL`.

Todos los endpoints requieren la clave de función como query param `?code=<key>`.

| Endpoint | Método | Descripción |
|---|---|---|
| `/api/datos` | GET | Últimas 100 lecturas (ajustable con `?limit=N`, máx 500) |
| `/api/datos` | GET | Filtrar por dispositivo con `?device_id=cnc_fresadora_01` |
| `/api/datos/csv` | GET | Descarga CSV completo (filtrable con `?device_id=...`) |
| `/api/actuador` | POST | Envía comando al ESP32 — body: `{"comando": "ON\|OFF\|RESET"}` |

---

## MQTT Bridge (`mqtt_bridge.py`)

Script Python que corre localmente junto con Mosquitto. Actúa como puente entre el ESP32 (MQTT sin TLS) y Azure IoT Hub (MQTT con TLS), evitando la necesidad de TLS directo en el microcontrolador.

### Instalación de dependencias

```bash
pip install -r backend/requirements.txt
# Instala: paho-mqtt, azure-iot-device
```

### Variables de entorno

```bash
# Linux/macOS
export DEVICE_CONN_STR="HostName=<hub>.azure-devices.net;DeviceId=cnc_fresadora_01;SharedAccessKey=<key>"

# Windows PowerShell
$env:DEVICE_CONN_STR = "HostName=<hub>.azure-devices.net;DeviceId=cnc_fresadora_01;SharedAccessKey=<key>"
```

Obtener `DEVICE_CONN_STR`:
Portal Azure → IoT Hub → Administración de dispositivos → `cnc_fresadora_01` → **Cadena de conexión principal**

### Ejecución

```bash
# 1. Arrancar Mosquitto (Windows)
& "C:\Program Files\mosquitto\mosquitto.exe" -c mosquitto.conf -v

# 2. Correr el bridge
python backend/mqtt_bridge.py
```

---

## Despliegue del backend

El despliegue automatizado se realiza con los scripts de la carpeta `Deploy/`. Ver el [README raíz](../README.md#8-flujo-de-despliegue) para el flujo completo.

### Despliegue manual (sin scripts)

```bash
# Requisitos previos
az login
npm install -g azure-functions-core-tools@4 --unsafe-perm true

# Publicar desde la raíz del Function App
cd backend/azure_functions
func azure functionapp publish <FUNC_APP_NAME> --python --build remote
```

### Dependencias del Function App

El archivo `backend/azure_functions/requirements.txt` es el que Azure usa durante el despliegue:

```
azure-functions==1.21.3
requests==2.32.3
azure-cosmos==4.6.0
azure-iot-hub==2.6.1
```

> **No confundir** con `backend/requirements.txt`, que contiene únicamente las dependencias del MQTT bridge (`paho-mqtt`, `azure-iot-device`).
