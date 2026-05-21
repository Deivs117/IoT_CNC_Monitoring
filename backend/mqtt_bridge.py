"""
mqtt_bridge.py — FLUX CNC IoT
Recibe mensajes MQTT del ESP32 via Mosquitto local
y los reenvía al Azure IoT Hub.
"""
import json
import logging
import os
import time

import paho.mqtt.client as mqtt
from azure.iot.device import IoTHubDeviceClient, Message

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("mqtt_bridge")

# ── Configuración ──────────────────────────────────────────────────────────
LOCAL_BROKER   = "localhost"
LOCAL_PORT     = 1883
LOCAL_TOPIC    = "cnc/pcb/telemetry"
DEVICE_CONN_STR = os.environ.get("DEVICE_CONN_STR", "") # <-- reemplazar

# ── Callbacks ──────────────────────────────────────────────────────────────
iothub_client = None

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        logger.info("[MQTT Local] Conectado al broker")
        client.subscribe(LOCAL_TOPIC)
        logger.info(f"[MQTT Local] Suscrito a {LOCAL_TOPIC}")
    else:
        logger.error(f"[MQTT Local] Error de conexión rc={rc}")

def on_message(client, userdata, msg):
    payload = msg.payload.decode("utf-8")
    logger.info(f"[MQTT Local] Recibido: {payload[:80]}...")
    try:
        message = Message(payload)
        message.content_type = "application/json"
        message.content_encoding = "utf-8"
        iothub_client.send_message(message)
        logger.info("[IoT Hub] Mensaje enviado")
    except Exception as e:
        logger.error(f"[IoT Hub] Error al enviar: {e}")

# ── Main ───────────────────────────────────────────────────────────────────
def main():
    global iothub_client

    logger.info("[IoT Hub] Conectando...")
    iothub_client = IoTHubDeviceClient.create_from_connection_string(DEVICE_CONN_STR)
    iothub_client.connect()
    logger.info("[IoT Hub] Conectado")

    client = mqtt.Client(client_id="mqtt-bridge")
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(LOCAL_BROKER, LOCAL_PORT, keepalive=60)
    logger.info("[Bridge] Iniciado — esperando mensajes del ESP32...")
    client.loop_forever()

if __name__ == "__main__":
    main()