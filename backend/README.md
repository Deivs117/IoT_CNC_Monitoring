# Backend — FLUX CNC IoT Monitoring

## Arquitectura

```
ESP32-C3
  └── WiFi + MQTT (puerto 1883)
        └── Mosquitto (broker local)
              └── mqtt_bridge.py
                    └── Azure IoT Hub
                          └── Event Hub (integrado)
                                └── Azure Function: telemetry_processor
                                      ├── Cosmos DB (CNCMonitor/Telemetry)
                                      └── Telegram Bot (alertas)

Dashboard (David)
  └── GET /api/datos
        └── Azure Function: get_datos
              └── Cosmos DB
```

---

## Estructura de carpetas

```
backend/
├── mqtt_bridge.py                        # Script puente: Mosquitto → Azure IoT Hub
├── requirements.txt                      # Dependencias del bridge (pip)
└── azure_functions/
    ├── host.json                         # Configuración del runtime de Azure Functions
    ├── local.settings.json               # Variables de entorno LOCALES (no subir a Git)
    ├── requirements.txt                  # Dependencias de las funciones (pip)
    ├── telemetry_processor/
    │   ├── __init__.py                   # Procesa eventos del IoT Hub → guarda en Cosmos DB → alerta Telegram
    │   └── function.json                 # Trigger: Event Hub | Output: Cosmos DB
    ├── get_datos/
    │   ├── __init__.py                   # GET /api/datos → devuelve lecturas de Cosmos DB
    │   └── function.json                 # Trigger: HTTP GET
    ├── descargar_csv/
    │   ├── __init__.py                   # GET /api/datos/csv → descarga histórico en CSV
    │   └── function.json                 # Trigger: HTTP GET
    ├── control_actuador/
    │   ├── __init__.py                   # POST /api/actuador → envía comando al ESP32 vía IoT Hub
    │   └── function.json                 # Trigger: HTTP POST
    └── shared_code/
        ├── alerts.py                     # Lógica de umbrales y envío de alertas Telegram
        └── __init__.py
```

---

## Recursos Azure necesarios

| Recurso | Nombre en el proyecto | Para qué sirve |
|---|---|---|
| Resource Group | `cnc-iot-rg` | Contenedor de todos los recursos |
| IoT Hub | `cnc-iot-hub` | Recibe mensajes del ESP32 |
| Cosmos DB | `cnc-cosmos-db` | Almacena las lecturas de telemetría |
| Function App | `cnc-functions` | Corre el backend serverless en Python |

### Recursos dentro de Cosmos DB
- **Base de datos:** `CNCMonitor`
- **Contenedor:** `Telemetry`
- **Partition key:** `/device_id`

### Dispositivo registrado en IoT Hub
- **Device ID:** `cnc_fresadora_01` (debe coincidir con `DEVICE_ID` en `config.h` del firmware)

---

## Variables de entorno (`local.settings.json`)

Este archivo **nunca se sube a GitHub** (está en `.gitignore`). Contiene las credenciales reales.

```json
{
  "IsEncrypted": false,
  "Values": {
    "AzureWebJobsStorage": "UseDevelopmentStorage=true",
    "FUNCTIONS_WORKER_RUNTIME": "python",
    "IOTHUB_EVENTS_CONNECTION_STRING": "...",
    "IOT_HUB_EVENTHUB_NAME": "...",
    "IOTHUB_SERVICE_CONNECTION_STRING": "...",
    "IOT_DEVICE_ID": "cnc_fresadora_01",
    "COSMOSDB_CONNECTION": "...",
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

### Cómo obtener cada valor

#### `IOTHUB_EVENTS_CONNECTION_STRING`
Cadena de conexión del **Event Hub integrado** del IoT Hub. Se usa para que la Azure Function lea los mensajes entrantes.

**Dónde obtenerla:**
Portal Azure → `cnc-iot-hub` → Configuración del centro de conectividad → Puntos de conexión integrados → **Punto de conexión compatible con Event Hubs**

Formato:
```
Endpoint=sb://ihsuprod***.servicebus.windows.net/;SharedAccessKeyName=iothubowner;SharedAccessKey=***
```

---

#### `IOT_HUB_EVENTHUB_NAME`
Nombre corto del Event Hub integrado. Se usa junto con la cadena anterior.

**Dónde obtenerlo:**
Misma pantalla que lo anterior → **Nombre compatible con Event Hubs**

Formato:
```
iothub-ehub-cnc-iot-123456
```

---

#### `IOTHUB_SERVICE_CONNECTION_STRING`
Cadena de conexión del **servicio** del IoT Hub. Se usa para enviar comandos al ESP32 (control_actuador).

**Dónde obtenerla:**
Portal Azure → `cnc-iot-hub` → Configuración de seguridad → Directivas de acceso compartido → **service** → Cadena de conexión principal

Formato:
```
HostName=cnc-iot-hub.azure-devices.net;SharedAccessKeyName=service;SharedAccessKey=***
```

---

#### `COSMOSDB_CONNECTION`
Cadena de conexión de la cuenta de Cosmos DB.

**Dónde obtenerla:**
Portal Azure → `cnc-cosmos-db` → Claves → **Cadena de conexión principal**

Formato:
```
AccountEndpoint=https://cnc-cosmos-db.documents.azure.com:443/;AccountKey=***;
```

---

#### `TELEGRAM_BOT_TOKEN`
Token del bot de Telegram que envía las alertas de anomalía.

**Cómo crearlo:**
1. Abre Telegram y busca `@BotFather`
2. Escríbele `/newbot`
3. Sigue las instrucciones — elige nombre y username (debe terminar en `_bot`)
4. BotFather te dará el token

Formato:
```
123456789:ABCdefGHIjklMNOpqrsTUVwxyz
```

---

#### `TELEGRAM_CHAT_ID`
ID del chat donde llegan las alertas. Puede ser tu chat personal o un grupo.

**Cómo obtenerlo:**
1. Busca tu bot en Telegram y escríbele cualquier mensaje
2. Abre en el navegador: `https://api.telegram.org/bot<TU_TOKEN>/getUpdates`
3. Busca el campo `"id"` dentro de `"chat"`

---

#### Umbrales de alerta (opcionales, tienen valores por defecto)

| Variable | Valor por defecto | Significado |
|---|---|---|
| `TEMP_MIN` | 15.0 | Temperatura mínima en °C |
| `TEMP_MAX` | 45.0 | Temperatura máxima en °C |
| `HUM_MIN` | 20.0 | Humedad mínima en % |
| `HUM_MAX` | 80.0 | Humedad máxima en % |
| `VIBRATION_ANOMALY_THRESHOLD` | 0.80 | Score mínimo para disparar alerta de vibración |

---

## MQTT Bridge (`mqtt_bridge.py`)

Script Python que corre en la misma PC que Mosquitto. Actúa como puente entre el ESP32 y Azure IoT Hub, evitando la necesidad de TLS directo en el microcontrolador.

### Variables de entorno necesarias

Antes de correr el bridge, define la variable de entorno con la cadena de conexión del dispositivo:

**Windows (PowerShell):**
```powershell
$env:DEVICE_CONN_STR = "HostName=cnc-iot-hub.azure-devices.net;DeviceId=cnc_fresadora_01;SharedAccessKey=***"
```

**Dónde obtener `DEVICE_CONN_STR`:**
Portal Azure → `cnc-iot-hub` → Administración de dispositivos → Dispositivos → `cnc_fresadora_01` → **Cadena de conexión principal**

### Instalación de dependencias

```bash
pip install paho-mqtt azure-iot-device
```

### Cómo ejecutar

1. Asegúrate de que Mosquitto esté corriendo:
```powershell
& "C:\Program Files\mosquitto\mosquitto.exe" -c "C:\Program Files\mosquitto\flux.conf" -v
```

2. Corre el bridge:
```powershell
$env:DEVICE_CONN_STR = "TU_CADENA_CONEXION_DISPOSITIVO"
python backend/mqtt_bridge.py
```

---

## Endpoints de la API

Todos los endpoints requieren la **API key** de la función. Se obtiene en:
Portal Azure → `cnc-functions` → la función → Claves de función → `default`

| Endpoint | Método | Descripción |
|---|---|---|
| `/api/datos?code=<key>` | GET | Últimas 100 lecturas de telemetría |
| `/api/datos?code=<key>&limit=N` | GET | Últimas N lecturas (máx 500) |
| `/api/datos?code=<key>&device_id=cnc_fresadora_01` | GET | Filtrar por dispositivo |
| `/api/datos/csv?code=<key>` | GET | Descarga histórico en CSV |
| `/api/actuador?code=<key>` | POST | Enviar comando al ESP32 |

**URL base:**
```
https://cnc-functions-c3f2a5afcwf7cmb9.centralus-01.azurewebsites.net
```

---

## Deploy del backend

```powershell
cd backend/azure_functions
func azure functionapp publish cnc-functions --python
```

Requiere tener instalado:
- Azure Functions Core Tools v4: `npm install -g azure-functions-core-tools@4`
- Azure CLI: `az login`
