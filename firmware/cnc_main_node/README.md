# Firmware: cnc_main_node — Nodo Principal ESP32

Firmware del nodo principal del sistema IoT CNC PCB Monitor. Mide temperatura, humedad y vibracion, ejecuta inferencia de anomalia en el microcontrolador mediante Edge Impulse SDK, y publica telemetria al Azure IoT Hub via MQTTS.

---

## Hardware requerido

| Componente | Descripcion |
|---|---|
| ESP32 (o ESP32-C3) | Microcontrolador principal |
| MPU-6050 | Acelerometro/giroscopio 6-ejes por I2C |
| DHT22 | Sensor de temperatura y humedad |
| Rele o LED | Actuador controlable por Direct Method / C2D |

---

## Archivos del firmware

| Archivo | Descripcion |
|---|---|
| `cnc_main_node.ino` | Sketch principal: inicializacion, loop de muestreo, inferencia, publicacion MQTT y control de actuador |
| `config.h.template` | Plantilla de configuracion — copiar como `config.h` con credenciales reales (no commitear) |
| `sensors.h` | Funciones de lectura del MPU-6050 y DHT22, calculo de caracteristicas estadisticas |
| `edge_impulse_vibration.h` | Adaptador del SDK Edge Impulse — incluye `CNC_Monitor_Project_inferencing.h` |
| `ei-cnc_monitor_project-arduino-*.zip` | Libreria Edge Impulse exportada desde Edge Impulse Studio |

---

## Configuracion inicial

1. Copiar `config.h.template` como `config.h`:
   ```bash
   cp config.h.template config.h
   ```
2. Editar `config.h` con los valores reales:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `IOT_HUB_HOST` — FQDN del IoT Hub (ej. `cnc-iot-hub.azure-devices.net`)
   - `DEVICE_ID` — `cnc_fresadora_01`
   - `DEVICE_PRIMARY_KEY` — clave primaria en base64 del dispositivo
   - Pines de hardware: `DHT_PIN`, `SDA_PIN`, `SCL_PIN`, `ACTUATOR_PIN`
3. Instalar la libreria Edge Impulse en Arduino IDE:
   - Sketch -> Include Library -> Add .ZIP Library -> seleccionar `ei-cnc_monitor_project-arduino-*.zip`
4. Seleccionar la placa correcta en Arduino IDE y cargar el sketch.

---

## Comportamiento en ejecucion

### Cadencia de muestreo

El firmware toma muestras del MPU-6050 a la frecuencia definida por `EI_CLASSIFIER_INTERVAL_MS` (determinado por el modelo Edge Impulse, tipicamente 10 ms / 100 Hz). El buffer de entrada del clasificador se completa con `EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE` muestras intercaladas (ax, ay, az, ...).

### Cadencia de publicacion

La telemetria se publica al IoT Hub **una vez cada 5 segundos** (`PUBLISH_INTERVAL_MS`), independientemente de la frecuencia de muestreo. Esto protege la cuota diaria del tier gratuito de IoT Hub (~8000 mensajes/dia).

### Contrato JSON publicado

Topic: `devices/cnc_fresadora_01/messages/events/`

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

- `vibration_status`: `"normal"` si el score esta por debajo de `ANOMALY_THRESHOLD`, `"anomalia"` en caso contrario
- `vibration_anomaly_score`: valor de anomalia del clasificador Edge Impulse (0.0 - 1.0)
- `visual_anomaly_score`: reservado para extension futura, publicado como `null`

### Control de actuador

El firmware escucha en el topic de suscripcion del IoT Hub y procesa:
- **Direct Methods**: respuesta inmediata con confirmacion
- **Cloud-to-Device (C2D)**: comandos encolados procesados en el loop principal

Comandos validos: `ON`, `OFF`, `RESET`. El pin de actuador se define en `ACTUATOR_PIN` de `config.h`.

### Autenticacion SAS

La conexion MQTTS utiliza autenticacion SAS (HMAC-SHA256) generada en el propio microcontrolador usando `mbedTLS`. El token tiene validez de `SAS_TTL` segundos (por defecto 3600) y se renueva automaticamente `SAS_RENEW_BEFORE` segundos antes de expirar.

---

## Dependencias Arduino IDE

| Libreria | Fuente |
|---|---|
| `CNC_Monitor_Project_inferencing` | Edge Impulse Studio (exportar como Arduino library) |
| `ArduinoJson` (>= 6.21) | Library Manager |
| `PubSubClient` (>= 2.8) | Library Manager |
| `DHT sensor library` | Adafruit via Library Manager |
| `Adafruit MPU6050` | Adafruit via Library Manager |
| `esp32` board package (>= 2.0.0) | Espressif via Boards Manager |

---

## Seguridad

- `config.h` con credenciales reales NO debe commiterse. Esta incluido en `.gitignore`.
- El certificado raiz de Azure IoT Hub se define en `config.h.template` y se usa para validar la conexion TLS.
- Las claves de dispositivo se obtienen desde el Portal de Azure o con `az iot hub device-identity show`.
