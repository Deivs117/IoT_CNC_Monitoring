#pragma once

// Wi-Fi (placeholders; reemplazar antes de desplegar según la sección de configuración del README)
constexpr char WIFI_SSID[] = "ADMINISTRACION";
constexpr char WIFI_PASSWORD[] = "administracion2026";

// MQTT broker / Azure IoT bridge
constexpr char MQTT_BROKER[] = "192.168.1.8";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USERNAME_PLACEHOLDER[] = "mqtt_user";
constexpr char MQTT_PASSWORD_PLACEHOLDER[] = "mqtt_password";
constexpr char MQTT_USERNAME[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char MQTT_TELEMETRY_TOPIC[] = "cnc/pcb/telemetry";
constexpr char DEVICE_ID[] = "cnc_fresadora_01";

// Pines ESP32-C3
constexpr int DHT_PIN = 0;
constexpr int DHT_TYPE = 22;   // DHT22
constexpr int MPU_ADDR = 0x68;
constexpr int SDA_PIN = 8;
constexpr int SCL_PIN = 9;
constexpr int STATUS_LED_PIN = 10;

// Muestreo
constexpr int WINDOW_SIZE = 32;
constexpr unsigned long SAMPLE_DELAY_MS = 10;
constexpr unsigned long DHT_INTERVAL_MS = 2000;
constexpr float ANOMALY_THRESHOLD = 0.70f;
