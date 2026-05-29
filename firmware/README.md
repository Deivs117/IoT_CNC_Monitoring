# Firmware — CNC PCB IoT Monitor

Esta carpeta contiene todo el firmware embebido del sistema, organizado por nodo fisico.

---

## Estructura

```
firmware/
├── cnc_main_node/          <- Nodo principal: ESP32 con sensores + Edge Impulse
├── cnc_camera_node/        <- Nodo de vision: ESP32-CAM con Edge Impulse + MQTTS
│   ├── capture_express/    <- Firmware de captura de imagenes para dataset
│   ├── dataset_capture/    <- Automatizacion Python para captura del dataset
│   └── cnc_camera_node/    <- Firmware de inferencia y publicacion MQTTS
└── model/
    └── dataset/
        └── dataset_balanced.csv  <- Dataset tabular balanceado del nodo principal
```

---

## Nodos del sistema

### Nodo principal (`cnc_main_node`)

- **Hardware:** ESP32 + MPU-6050 (acelerometro/giroscopio) + DHT22 (temperatura/humedad)
- **Funcion:** mide temperatura, humedad y ejecuta inferencia de anomalia vibracional con Edge Impulse SDK
- **Conectividad:** MQTTS directo al Azure IoT Hub (puerto 8883, autenticacion SAS HMAC-SHA256)
- **Publicacion:** telemetria cada 5 segundos al topic `devices/cnc_fresadora_01/messages/events/`
- **Recepcion:** escucha Direct Methods y mensajes C2D para control de actuador

### Nodo de vision (`cnc_camera_node`)

- **Hardware:** ESP32-CAM AI Thinker con modulo de camara OV2640
- **Funcion:** clasifica el tipo de PCB en la bancada usando un modelo Edge Impulse (96x96 RGB565)
- **Conectividad:** MQTTS directo al Azure IoT Hub (mismo patron que el nodo principal)
- **Publicacion:** resultado de inferencia al topic `devices/cnc_camera_01/messages/events/` cada 10 s o ante cambio de clase

---

## Libreria Edge Impulse

El nodo principal usa la libreria exportada desde Edge Impulse Studio:

- `cnc_main_node/ei-cnc_monitor_project-arduino-*.zip` — instalar en Arduino IDE via Sketch -> Include Library -> Add .ZIP Library

El nodo de camara usa una libreria separada con el clasificador de PCBs:

- Se exporta desde Edge Impulse Studio tras entrenar el modelo con las imagenes del dataset
- El nombre de la libreria exportada debe coincidir con el `#include` en `cnc_camera_node.ino`

---

## Dataset tabular

`firmware/model/dataset/dataset_balanced.csv` contiene el dataset balanceado utilizado para entrenar o evaluar el modelo de deteccion de anomalias vibracionales del nodo principal fuera de Edge Impulse.

Las columnas corresponden a las variables sensoriales (acelerometro, temperatura, humedad) etiquetadas con el estado operativo (normal / anomalia).

---

## Dependencias comunes (Arduino IDE)

| Libreria | Autor | Uso |
|---|---|---|
| ArduinoJson | Benoit Blanchon | Serializacion JSON del payload MQTT |
| PubSubClient | Nick O'Leary | Cliente MQTT |
| DHT sensor library | Adafruit | Lectura del sensor DHT22 |
| Adafruit MPU6050 | Adafruit | Lectura del MPU-6050 (solo nodo principal) |
| esp32 board package | Espressif | Soporte de plataforma ESP32 y ESP32-CAM |

Ver cada subdirectorio para dependencias especificas de cada firmware.
