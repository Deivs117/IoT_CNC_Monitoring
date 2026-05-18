# cnc-pcb-monitoreo

Sistema IoT + Edge AI + Serverless para monitoreo analítico y predictivo de una fresadora CNC usada en manufactura de PCBs.

## 1. Objetivo del proyecto

`cnc-pcb-monitoreo` inicializa la **Tarea 1 Versión 1** del proyecto final del curso con una arquitectura preparada para operar con bajo costo, alta seguridad y evolución incremental. El sistema monitorea variables tabulares de proceso —temperatura, humedad y vibración inferida desde acelerometría 3 ejes— para detectar desviaciones que puedan dañar placas de cobre o romper herramientas.

La solución queda estructurada para integrar más adelante una **ESP32-CAM** y una capa de visión artificial sin rehacer el backend ni rediseñar el esquema de almacenamiento.

## 2. Problema industrial

Durante el fresado CNC de PCBs, pequeñas variaciones térmicas o vibraciones anómalas pueden producir:

- placas defectuosas por pérdida de precisión;
- desgaste acelerado de la herramienta;
- tiempos muertos por recalibración o mantenimiento;
- pérdida de propiedad intelectual si los datos o diseños son expuestos sin control.

Por ello el sistema debe ser:

- **económico**, para ajustarse a una suscripción de estudiante;
- **seguro**, evitando credenciales hardcodeadas y favoreciendo App Settings;
- **rápido de implementar**, priorizando servicios administrados y una arquitectura desacoplada.

## 3. Decisiones arquitectónicas y justificación

### Cloud: Azure Functions Serverless (Consumption Plan)

Se adopta Azure Functions para evitar costos fijos de una VM o contenedor 24/7. Si la CNC no está operando, el costo tiende a cero. Además, el modelo serverless acelera el desarrollo y reduce la carga operativa del equipo.

### Protocolo: MQTT

MQTT reduce la sobrecarga respecto a HTTP y se adapta mejor al ESP32. El nodo principal publica la telemetría y puede desacoplarse de consumidores posteriores sin conocerlos directamente.

### Persistencia: Azure Cosmos DB Serverless

Cosmos DB Serverless permite almacenar documentos JSON con pago por uso. Esto encaja con el crecimiento progresivo del sistema y con la necesidad de recibir, en el futuro, datos adicionales de visión artificial sin migraciones de esquema rígidas.

### Alertas: Telegram Bot API

Telegram ofrece una vía de notificación gratuita y rápida. El backend solo necesita un `POST` HTTP a la Bot API usando variables de entorno para el token y el chat de destino.

## 4. Cómo se fundamenta esta versión en los repositorios previos

Esta inicialización fusiona dos bases de conocimiento ya desarrolladas:

1. **[`ktalynagb/cnc-iot-ia`](https://github.com/ktalynagb/cnc-iot-ia)**
   - aporta el firmware de captura con ESP32 + MPU-6050 + DHT;
   - aporta la lógica TinyML/TF Lite Micro para inferencia embebida;
   - aporta los pesos exportados del modelo MLP y los parámetros de escalado.

2. **[`ktalynagb/cnc-iot-practica3`](https://github.com/ktalynagb/cnc-iot-practica3)**
   - aporta la base de Azure Functions;
   - aporta la estructura `host.json` y la función de procesamiento por lotes;
   - aporta la lógica previa de umbrales y alertas, aquí adaptada al nuevo payload JSON y extendida con Telegram.

El resultado no copia esos repositorios tal cual: los **reorganiza** dentro de una estructura nueva y los adapta al alcance específico de monitoreo de PCBs.

## 5. Estructura del repositorio

```text
cnc-pcb-monitoreo/
├── .gitignore
├── README.md
├── firmware/
│   ├── cnc_main_node/
│   │   ├── cnc_main_node.ino
│   │   ├── config.h
│   │   ├── edge_impulse_vibration.h
│   │   └── sensors.h
│   └── cnc_camera_node/
│       └── .gitkeep
├── backend/
│   ├── azure_functions/
│   │   ├── host.json
│   │   ├── local.settings.json
│   │   ├── telemetry_processor/
│   │   │   ├── __init__.py
│   │   │   └── function.json
│   │   └── shared_code/
│   │       ├── __init__.py
│   │       └── alerts.py
│   └── requirements.txt
└── frontend/
    └── .gitkeep
```

## 6. Flujo de extremo a extremo

1. El **ESP32 principal** toma muestras del MPU-6050 y del DHT22.
2. Con una ventana de 32 muestras calcula features estadísticas.
3. Ejecuta inferencia local con el **MLP exportado desde `cnc-iot-ia`**.
4. Publica un JSON vía MQTT con variables de sensores y predicciones.
5. Azure Functions consume eventos, normaliza el documento y lo escribe en Cosmos DB.
6. Si existen condiciones de alerta, el backend envía una notificación a Telegram.

## 7. Contrato JSON de telemetría

Payload objetivo del nodo principal:

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

### Razón del diseño

- `sensors` agrupa telemetría de proceso.
- `predictions` separa resultados analíticos/predictivos de los datos crudos.
- `visual_anomaly_score` queda en `null` para que la integración futura de ESP32-CAM sea solo una extensión del payload, no una ruptura de compatibilidad.

## 8. Firmware `firmware/cnc_main_node/`

El nodo principal ahora concentra tres piezas derivadas del trabajo previo:

- **sensado** reutilizando la lógica de `cnc_iot_esp32.ino` para MPU-6050 y DHT;
- **inferencia embebida** reutilizando la lógica de `cnc_mlp_inference.ino`;
- **publicación MQTT** adaptada al payload JSON del proyecto final.

### Archivos clave

#### `cnc_main_node.ino`
- inicializa Wi-Fi, MQTT, sensores y TF Lite Micro;
- genera features estadísticas sobre la vibración;
- ejecuta clasificación local;
- publica el JSON objetivo.

#### `config.h`
- concentra parámetros de red, broker MQTT, pines y umbrales;
- usa placeholders para evitar exponer credenciales reales.

#### `sensors.h`
- encapsula inicialización y lectura del MPU-6050;
- reutiliza la lógica de adquisición ambiental y cálculo de features.

#### `edge_impulse_vibration.h`
- incorpora los pesos del modelo MLP exportado y los parámetros `SCALER_MEAN` / `SCALER_STD` provenientes de `cnc-iot-ia`.

### Dependencias esperadas en Arduino IDE

- `DHT sensor library`
- `PubSubClient`
- `ArduinoJson`
- `TFLite_ESP32` o la variante de TensorFlow Lite Micro usada en el repositorio base

## 9. Backend `backend/azure_functions/`

El backend adapta `procesar_datos` del repositorio serverless previo a una función más alineada con esta versión del proyecto.

### `telemetry_processor/__init__.py`

- consume lotes desde Event Hub compatible con IoT Hub;
- parsea el JSON publicado por el firmware;
- construye un documento flexible para Cosmos DB;
- evalúa alertas por temperatura, humedad y score de anomalía vibracional;
- envía notificaciones a Telegram cuando aplica.

### `telemetry_processor/function.json`

Usa:
- **Event Hub Trigger** para la ingestión;
- **Cosmos DB Output Binding** para persistencia serverless.

Esto permite mantener una implementación ligera y coherente con el objetivo de prototipado rápido.

### `shared_code/alerts.py`

Extiende la lógica previa de umbrales del repositorio `cnc-iot-practica3` para:
- aceptar el nuevo payload anidado;
- evaluar `vibration_status` y `vibration_anomaly_score`;
- enviar mensajes por Telegram cuando existan alertas activas.

## 10. Documento almacenado en Cosmos DB

Ejemplo del documento persistido:

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
  "raw_payload": {
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
}
```

## 11. Configuración local mínima

### Backend

Instalar dependencias:

```bash
pip install -r backend/requirements.txt
```

Completar placeholders en:

- `backend/azure_functions/local.settings.json`
- App Settings en Azure para despliegue real

Variables importantes:

- `IOTHUB_CONNECTION_STRING`
- `IOT_HUB_EVENTHUB_NAME`
- `COSMOS_CONNECTION_STRING`
- `COSMOS_DATABASE`
- `COSMOS_CONTAINER`
- `TELEGRAM_BOT_TOKEN`
- `TELEGRAM_CHAT_ID`
- `TEMP_MIN`, `TEMP_MAX`, `HUM_MIN`, `HUM_MAX`
- `VIBRATION_ANOMALY_THRESHOLD`

### Firmware

Editar placeholders en:

- `firmware/cnc_main_node/config.h`

Campos mínimos:

- SSID y password Wi-Fi
- broker/endpoint MQTT
- topic de telemetría
- credenciales del broker

## 12. Extensibilidad para ESP32-CAM y frontend

Se dejaron dos espacios reservados:

- `firmware/cnc_camera_node/.gitkeep`
- `frontend/.gitkeep`

Esto formaliza desde ya la modularidad del sistema:

- la **cámara** podrá publicar `visual_anomaly_score` sin alterar el backend;
- el **frontend** podrá consumir Cosmos DB o APIs adicionales más adelante sin afectar la ingesta actual.

## 13. Seguridad y buenas prácticas incluidas en esta versión

- No se incluyen credenciales reales en el repositorio.
- `config.h` y `local.settings.json` quedan con valores placeholder.
- El backend usa variables de entorno para Telegram y Azure.
- La persistencia se basa en documentos JSON flexibles, evitando acoplamiento prematuro.

## 14. Estado actual de la inicialización

Esta versión deja listo el esqueleto funcional del proyecto para:

- continuar el firmware del nodo principal;
- desplegar la Azure Function de ingestión;
- conectar Cosmos DB Serverless;
- activar alertas en Telegram;
- incorporar posteriormente visión artificial y frontend sin romper el contrato actual.
