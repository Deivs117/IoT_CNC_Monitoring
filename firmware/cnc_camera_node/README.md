# Firmware: cnc_camera_node — Nodo ESP32-CAM

Firmware del nodo de vision del sistema IoT CNC PCB Monitor. El nodo ESP32-CAM captura imagenes de la bancada de la fresadora CNC, ejecuta inferencia con un modelo Edge Impulse para clasificar el tipo de PCB, y publica el resultado al Azure IoT Hub via MQTTS.

Esta carpeta contiene tres submodulos con responsabilidades distintas:

---

## Submodulos

### `capture_express/` — Firmware de captura para dataset

Firmware minimalista que expone un servidor HTTP en el ESP32-CAM para capturar imagenes JPEG en tiempo real. Utilizado exclusivamente durante la fase de construccion del dataset de entrenamiento.

- Expone `GET /capture` que retorna un JPEG crudo (SVGA 800x600)
- Expone `GET /` para informacion de estado
- No realiza inferencia ni publica MQTT
- No debe usarse en produccion

### `dataset_capture/` — Automatizacion Python de captura

Script Python que automatiza la captura masiva de imagenes invocando `GET /capture` sobre el firmware `capture_express`. Organiza las imagenes por clase en la estructura esperada por Edge Impulse.

Ver `dataset_capture/README.md` para instrucciones detalladas.

### `cnc_camera_node/` — Firmware de inferencia y publicacion MQTTS

Firmware de produccion del nodo de vision. Realiza inferencia con el modelo Edge Impulse en tiempo real y publica los resultados al Azure IoT Hub via MQTTS.

---

## Flujo de trabajo completo

```
1. Cargar capture_express.ino en el ESP32-CAM
   |
2. Ejecutar dataset_capture/capture.py para capturar imagenes por clase
   |
3. Subir el dataset a Edge Impulse Studio y entrenar el modelo
   |
4. Exportar la libreria Arduino desde Edge Impulse Studio
   |
5. Cargar cnc_camera_node.ino con la libreria exportada
   |
6. ESP32-CAM clasifica PCBs y publica telemetria al IoT Hub via MQTTS
```

---

## Firmware de inferencia: `cnc_camera_node/cnc_camera_node.ino`

### Hardware requerido

| Componente | Descripcion |
|---|---|
| ESP32-CAM AI Thinker | Modulo ESP32 con camara OV2640 integrada |
| Adaptador FTDI (USB-UART) | Para flasheo y depuracion via Serial |

### Configuracion inicial

1. Copiar `cnc_camera_node/camera_secrets.h.template` como `cnc_camera_node/camera_secrets.h`
2. Rellenar los valores en `camera_secrets.h`:
   - `WIFI_SSID` / `WIFI_PASS`
   - `IOT_HUB_HOST` — FQDN del IoT Hub
   - `DEVICE_PRIMARY_KEY` — clave primaria de `cnc_camera_01` en base64
3. Instalar la libreria Edge Impulse exportada desde Edge Impulse Studio
4. Configurar en Arduino IDE:
   - Board: `AI Thinker ESP32-CAM`
   - Partition Scheme: `Huge APP (3MB No OTA/1MB SPIFFS)`
5. Cargar el sketch

### Contrato JSON publicado

Topic: `devices/cnc_camera_01/messages/events/`

```json
{
  "device_id": "cnc_camera_01",
  "timestamp": 1716076810,
  "camera": {
    "pcb_class": "PCB_SMD",
    "confidence": 0.873,
    "model_version": "1.0.0",
    "inference_ms": 241,
    "probabilities": {
      "PCB_Mixta": 0.051,
      "PCB_SMD":   0.873,
      "PCB_TH":    0.028,
      "Sin_PCB":   0.048
    }
  }
}
```

### Regla de publicacion (control de tasa)

El firmware publica unicamente cuando se cumple al menos una de estas condiciones:
1. Han transcurrido `PUBLISH_INTERVAL_MS` ms (por defecto 10 000 ms / 10 s) desde la ultima publicacion
2. La clase predicha cambio respecto a la ultima clase publicada

Esto evita saturar el IoT Hub con lecturas redundantes y respeta la cuota del tier gratuito.

### Resolucion de inferencia

El modelo Edge Impulse clasifica frames de 96x96 pixeles en formato RGB565. La captura se realiza directamente desde el sensor OV2640 en esa resolucion.

### Clases de clasificacion

| Clase | Descripcion |
|---|---|
| `PCB_Mixta` | PCB con componentes through-hole y SMD coexistiendo |
| `PCB_SMD` | PCB con componentes de montaje superficial unicamente |
| `PCB_TH` | PCB con componentes through-hole / insercion unicamente |
| `Sin_PCB` | Bancada vacia, sin placa visible |

---

## Firmware de captura: `capture_express/capture_express.ino`

### Configuracion inicial

1. Copiar `capture_express/camera_secrets.h.template` como `capture_express/camera_secrets.h`
2. Rellenar `WIFI_SSID` y `WIFI_PASS` en `camera_secrets.h`
3. Configurar en Arduino IDE:
   - Board: `AI Thinker ESP32-CAM`
   - Partition Scheme: `Huge APP (3MB No OTA/1MB SPIFFS)`
4. Cargar el sketch
5. Anotar la IP del ESP32-CAM desde el Serial Monitor (115200 baud)
6. Verificar: `curl http://<IP>/capture -o test.jpg`

---

## Dependencias Arduino IDE

| Libreria | Fuente |
|---|---|
| `CNC_Image_Clasification_inferencing` | Edge Impulse Studio (exportar como Arduino library) |
| `ArduinoJson` (>= 6.21) | Library Manager |
| `PubSubClient` (>= 2.8) | Library Manager |
| `esp32` board package (>= 2.0.0) | Espressif via Boards Manager |

---

## Seguridad

- `camera_secrets.h` con credenciales reales NO debe commiterse. Agregar al `.gitignore`:
  ```
  firmware/cnc_camera_node/**/camera_secrets.h
  ```
- Las claves de dispositivo se obtienen desde el Portal de Azure o con:
  ```bash
  az iot hub device-identity show \
    --hub-name <hub-name> --device-id cnc_camera_01 \
    --query "authentication.symmetricKey.primaryKey" --output tsv
  ```
