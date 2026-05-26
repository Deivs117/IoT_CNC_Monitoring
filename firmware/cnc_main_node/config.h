#pragma once

// =============================================================================
// config.h — Configuración del nodo CNC principal (ESP32-C3)
//
// INSTRUCCIONES:
//   1. Reemplaza WIFI_SSID / WIFI_PASSWORD con tus credenciales de red.
//   2. Tras ejecutar Deploy/01_infraestructura.sh, obtén las credenciales del
//      dispositivo con:
//        az iot hub device-identity show --hub-name cnc-iot-hub \
//          --device-id cnc_fresadora_01 --query "authentication.symmetricKey"
//   3. Copia la clave primaria en DEVICE_PRIMARY_KEY (base64, sin espacios).
//   4. Actualiza IOT_HUB_HOST con el FQDN de tu IoT Hub.
//
// NINGÚN secreto real debe commitearse en el repositorio.
// =============================================================================

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
constexpr char WIFI_SSID[]     = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

// ── Azure IoT Hub — conexión MQTTS directa (puerto 8883) ────────────────────
// FQDN del IoT Hub: <nombre>.azure-devices.net
constexpr char IOT_HUB_HOST[]       = "cnc-iot-hub.azure-devices.net";
constexpr char DEVICE_ID[]          = "cnc_fresadora_01";
// Clave primaria del dispositivo en formato base64 (de az iot hub device-identity show)
constexpr char DEVICE_PRIMARY_KEY[] = "<BASE64_DEVICE_PRIMARY_KEY>";

// Puerto MQTTS de Azure IoT Hub
constexpr uint16_t MQTT_PORT = 8883;

// ── Certificado raíz de Azure IoT Hub (DigiCert Global Root G2) ─────────────
// Válido para Azure IoT Hub en todas las regiones soportadas.
// Fuente: https://learn.microsoft.com/en-us/azure/iot-hub/reference-iot-hub-tls-support
static const char AZURE_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2YwZDAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwHwYDVR0jBBgwFoAUTiJUIBiV5uNu5g/6+rkS7QYXjzk
wDQYJKoZIhvcNAQELBQADggEBAGnwc0hVi0suvdCYz3MAbSRIMiPqRCYhsPZvBVj3
dqOsEoHaM1fWbAZWqU3jBlChMTFDe7GX5L5iuoFGBnHHxr9VPGGVz3l4KgMqxPT3
p3WmTQAJnj4P1n0A6eLqm3RmkP0UOjnxaHDq5yEP7d+LFJ1dNX5LOXioSC6Oe7U9
qhcHAoRCUXL7gBraqzSLbX0S7oBfBkHj0GBWrk1YjSx2RA+rLv7mSGpXFuMPwETZ
mXHGrFz9b8okfYnl1x9yHpW8GMXAywrjAr3XFgJqjxjf5pCuCxNPBfSe2eJTiZAn
9n7kSCgSpv/FaVX5CJ4gRGtBg7MOhLcFEGTUiOA=
-----END CERTIFICATE-----
)EOF";

// ── Pines ESP32-C3 ───────────────────────────────────────────────────────────
constexpr int DHT_PIN        = 0;
constexpr int DHT_TYPE       = 22;   // DHT22
constexpr int MPU_ADDR       = 0x68;
constexpr int SDA_PIN        = 8;
constexpr int SCL_PIN        = 9;
constexpr int STATUS_LED_PIN = 10;
// Pin del actuador (relé o LED de estado). Ajustar si se conecta en otro GPIO.
constexpr int ACTUATOR_PIN   = 10;

// ── Intervalos y umbrales ────────────────────────────────────────────────────
constexpr unsigned long DHT_INTERVAL_MS  = 2000;  // Lectura DHT cada 2 s
constexpr float         ANOMALY_THRESHOLD = 0.70f; // Score mínimo para alerta

// ── SAS token (Azure IoT Hub) ─────────────────────────────────────────────────
constexpr unsigned long SAS_TTL          = 3600UL; // Validez 1 hora
constexpr unsigned long SAS_RENEW_BEFORE = 300UL;  // Renovar 5 min antes de expirar
