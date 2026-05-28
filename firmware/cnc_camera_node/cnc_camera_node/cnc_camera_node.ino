// =============================================================================
// cnc_camera_node.ino — ESP32-CAM AI Thinker | Inferencia Edge Impulse + MQTTS
// =============================================================================
//
// Clasificador local de PCBs para el sistema IoT CNC PCB Monitor.
// Clases: PCB_Mixta | PCB_SMD | PCB_TH | Sin_PCB
//
// Arquitectura (producción):
//   ESP32-CAM → [Edge Impulse SDK] → MQTTS → Azure IoT Hub
//   (mismo patrón de conectividad que el nodo principal cnc_main_node)
//
// HTTP para depuración:
//   El firmware capture_express.ino sigue siendo el camino recomendado para
//   la captura del dataset. Este firmware NO expone endpoints HTTP en producción.
//
// Contrato JSON publicado por MQTT (topic: devices/cnc_camera_01/messages/events/):
// {
//   "device_id":  "cnc_camera_01",
//   "timestamp":  <unix epoch>,
//   "camera": {
//     "pcb_class":     "PCB_SMD",
//     "confidence":    0.873,
//     "inference_ms":  241,
//     "model_version": "1.0.0",
//     "probabilities": {
//       "PCB_Mixta": 0.051,
//       "PCB_SMD":   0.873,
//       "PCB_TH":    0.028,
//       "Sin_PCB":   0.048
//     }
//   }
// }
//
// Regla de publicación (control de tasa):
//   1. Publicar si transcurrieron >= PUBLISH_INTERVAL_MS desde la última publicación.
//   2. Publicar de inmediato si la clase detectada cambió respecto a la última publicada.
//   3. No publicar si no se cumple ninguna de las dos condiciones anteriores.
//   Esto evita saturar IoT Hub y mantiene la señal útil.
//
// Dependencias Arduino IDE:
//   - esp32 board package (Espressif, >= 2.0.0)
//   - ArduinoJson (Benoit Blanchon, >= 6.21)
//   - PubSubClient (Nick O'Leary, >= 2.8)
//   - CNC_Image_Clasification_inferencing (exportar desde Edge Impulse como librería Arduino,
//     instalar con: Sketch > Include Library > Add .ZIP Library)
//
// Board settings:
//   - Board: AI Thinker ESP32-CAM
//   - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
//   - Upload Speed: 115200 o 921600 con adaptador FTDI
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include <time.h>
#include "mbedtls/md.h"
#include "mbedtls/base64.h"

// ── Librería exportada de Edge Impulse ────────────────────────────────────────
// Después de entrenar el modelo en Edge Impulse Studio:
//   Deployment → Arduino Library → Build → descargar el .zip
//   Arduino IDE: Sketch → Include Library → Add .ZIP Library
// Librería exportada: ei-cnc_image_clasification-arduino-1.0.1-impulse-#1.zip
// Nombre del paquete confirmado en library.properties:
//   name=CNC_Image_Clasification_inferencing
//   includes=CNC_Image_Clasification_inferencing.h
#include <CNC_Image_Clasification_inferencing.h>

// ── Credenciales WiFi y Azure IoT Hub ─────────────────────────────────────────
// Copiar camera_secrets.h.template → camera_secrets.h y rellenar los valores.
// camera_secrets.h está en .gitignore y NUNCA debe commitearse con credenciales reales.
#include "camera_secrets.h"  // ← crear desde camera_secrets.h.template

// ── Identificación del dispositivo ───────────────────────────────────────────
#define CAMERA_DEVICE_ID  "cnc_camera_01"
#define MODEL_VERSION     "1.0.0"

// ── Control de publicación ────────────────────────────────────────────────────
// Publicar cada 10 segundos o inmediatamente si la clase cambia.
#define PUBLISH_INTERVAL_MS  10000UL

// ── NTP ───────────────────────────────────────────────────────────────────────
#define NTP_SERVER       "pool.ntp.org"

// ── SAS token ─────────────────────────────────────────────────────────────────
#define SAS_TTL          3600UL   // validez del token en segundos
#define SAS_RENEW_BEFORE  300UL   // renovar el token 5 minutos antes de que expire

// ── Timestamp mínimo válido ───────────────────────────────────────────────────
static const unsigned long MIN_VALID_TS = 1000000000UL;

// ── Pines ESP32-CAM AI Thinker (OV2640) ──────────────────────────────────────
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// =============================================================================
// MQTT / Azure IoT Hub — mismo patrón que cnc_main_node
// =============================================================================
WiFiClientSecure tls_client;
PubSubClient     mqtt_client(tls_client);

// Topics construidos en runtime con CAMERA_DEVICE_ID
static String TOPIC_TELEMETRY;

// SAS token
static unsigned long sas_expiry = 0;

// =============================================================================
// Estado de publicación (control de tasa)
// =============================================================================
static unsigned long last_publish_ms = 0;
static char          last_published_class[32] = "";

// =============================================================================
// Buffer de frame activo para el callback de Edge Impulse
// =============================================================================
static camera_fb_t *g_fb = nullptr;

// Callback requerido por signal_t del SDK de Edge Impulse.
// EI llama a esta función para leer pixels en formato empaquetado RGB (uint24 en float).
// El buffer de la cámara está en formato RGB565 (2 bytes por pixel).
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
  const uint8_t *buf = g_fb->buf;
  for (size_t i = 0; i < length; i++) {
    size_t   px_idx = offset + i;
    uint16_t pixel  = ((uint16_t)buf[px_idx * 2] << 8) | buf[px_idx * 2 + 1];
    uint8_t  r = ((pixel >> 11) & 0x1F) << 3;
    uint8_t  g = ((pixel >>  5) & 0x3F) << 2;
    uint8_t  b =  (pixel        & 0x1F) << 3;
    out_ptr[i] = (float)((uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b);
  }
  return EIDSP_OK;
}

// =============================================================================
// Estructura de resultado de inferencia
// =============================================================================
struct InferenceResult {
  char  pcb_class[32];
  float confidence;
  float probs[EI_CLASSIFIER_LABEL_COUNT];
  int   inference_ms;
  bool  valid;
};

// =============================================================================
// Inicialización de la cámara con resolución para Edge Impulse
// =============================================================================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0  = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2  = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4  = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6  = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;

  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size   = FRAMESIZE_96X96;
  config.jpeg_quality = 12;
  config.fb_count     = 1;
  config.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init falló: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_exposure_ctrl(s, 1);
  s->set_gain_ctrl(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);

  Serial.println("[CAM] Cámara inicializada (96×96 RGB565).");
  return true;
}

// =============================================================================
// Ejecutar inferencia local con Edge Impulse
// =============================================================================
InferenceResult runInference() {
  InferenceResult result = {};
  result.valid = false;

  g_fb = esp_camera_fb_get();
  if (!g_fb) {
    Serial.println("[EI] Error: no se pudo capturar frame");
    return result;
  }

  if ((int)g_fb->width  != EI_CLASSIFIER_INPUT_WIDTH ||
      (int)g_fb->height != EI_CLASSIFIER_INPUT_HEIGHT) {
    Serial.printf("[EI] Dimensiones incorrectas: %ux%u (esperado %dx%d)\n",
      g_fb->width, g_fb->height,
      EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
    esp_camera_fb_return(g_fb);
    g_fb = nullptr;
    return result;
  }

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data     = &ei_camera_get_data;

  ei_impulse_result_t ei_result = {};
  unsigned long t_start = millis();
  EI_IMPULSE_ERROR err  = run_classifier(&signal, &ei_result, false);
  result.inference_ms   = (int)(millis() - t_start);

  esp_camera_fb_return(g_fb);
  g_fb = nullptr;

  if (err != EI_IMPULSE_OK) {
    Serial.printf("[EI] run_classifier falló: %d\n", err);
    return result;
  }

  int best_idx = 0;
  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    result.probs[i] = ei_result.classification[i].value;
    if (result.probs[i] > result.probs[best_idx]) {
      best_idx = i;
    }
  }

  strncpy(result.pcb_class, ei_result.classification[best_idx].label,
          sizeof(result.pcb_class) - 1);
  result.pcb_class[sizeof(result.pcb_class) - 1] = '\0';
  result.confidence = result.probs[best_idx];
  result.valid      = true;

  Serial.printf("[EI] Clase: %-12s | Confianza: %.3f | Tiempo: %dms\n",
    result.pcb_class, result.confidence, result.inference_ms);

  return result;
}

// =============================================================================
// Determinar si se debe publicar (control de tasa)
// =============================================================================
bool shouldPublish(const char *current_class) {
  unsigned long now    = millis();
  bool timeout_reached = (now - last_publish_ms) >= PUBLISH_INTERVAL_MS;
  bool class_changed   = (strncmp(current_class, last_published_class,
                                  sizeof(last_published_class)) != 0);

  if (timeout_reached) {
    Serial.printf("[PUB] Timeout alcanzado (%lu ms). Publicando.\n",
                  now - last_publish_ms);
    return true;
  }
  if (class_changed) {
    Serial.printf("[PUB] Cambio de clase: '%s' → '%s'. Publicando.\n",
                  last_published_class, current_class);
    return true;
  }
  return false;
}

// =============================================================================
// Helpers WiFi, NTP, SAS token y MQTT
// — misma implementación que cnc_main_node con adaptaciones mínimas —
// =============================================================================

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] Conectando a %s ", WIFI_SSID);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++tries > 120) {
      Serial.println("\n[WiFi] Timeout — reintentando...");
      tries = 0;
    }
  }
  Serial.printf("\n[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
}

void setupTime() {
  configTime(0, 0, NTP_SERVER, "time.nist.gov");
  Serial.print("[NTP] Sincronizando");
  int tries = 0;
  while (time(nullptr) < (time_t)MIN_VALID_TS && tries < 60) {
    Serial.print(".");
    delay(500);
    tries++;
  }
  Serial.println();
  if (time(nullptr) < (time_t)MIN_VALID_TS) {
    Serial.println("[NTP] No se sincronizó — timestamp podría ser 0");
  } else {
    Serial.printf("[NTP] Sincronizado: %lu\n", (unsigned long)time(nullptr));
  }
}

static String urlEncode(const String &str) {
  String encoded = "";
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      char buf[5];
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

static String createSasToken(const char *host, const char *deviceId,
                              const char *primaryKey, unsigned long ttlSeconds) {
  String resource        = String(host) + "/devices/" + String(deviceId);
  String resourceEncoded = urlEncode(resource);
  unsigned long expiry   = (unsigned long)time(nullptr) + ttlSeconds;
  String toSign          = resourceEncoded + "\n" + String(expiry);

  size_t keyLen = strlen(primaryKey);
  unsigned char keyBin[128];
  size_t keyBinLen = 0;
  if (mbedtls_base64_decode(keyBin, sizeof(keyBin), &keyBinLen,
                            (const unsigned char *)primaryKey, keyLen) != 0) {
    Serial.println("[SAS] Error decodificando clave base64");
    return "";
  }

  unsigned char hmac[32];
  const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!mdInfo) { Serial.println("[SAS] md_info NULL"); return ""; }
  if (mbedtls_md_hmac(mdInfo, keyBin, keyBinLen,
                      (const unsigned char *)toSign.c_str(), toSign.length(),
                      hmac) != 0) {
    Serial.println("[SAS] Error HMAC");
    return "";
  }

  unsigned char sigB64[128];
  size_t sigB64Len = 0;
  if (mbedtls_base64_encode(sigB64, sizeof(sigB64), &sigB64Len, hmac, sizeof(hmac)) != 0) {
    Serial.println("[SAS] Error codificando firma base64");
    return "";
  }

  String sig        = String((char *)sigB64).substring(0, sigB64Len);
  String sigEncoded = urlEncode(sig);
  return "SharedAccessSignature sr=" + resourceEncoded +
         "&sig=" + sigEncoded + "&se=" + String(expiry);
}

void conectarAzureIoT() {
  tls_client.setCACert(AZURE_ROOT_CA);
  mqtt_client.setServer(IOT_HUB_HOST, MQTT_PORT);
  mqtt_client.setBufferSize(1024);

  String username = String(IOT_HUB_HOST) + "/" + CAMERA_DEVICE_ID +
                    "/?api-version=2021-04-12";
  String sas = createSasToken(IOT_HUB_HOST, CAMERA_DEVICE_ID,
                              DEVICE_PRIMARY_KEY, SAS_TTL);
  if (sas == "") {
    Serial.println("[MQTT] No se pudo generar SAS token — reintentando más tarde");
    delay(5000);
    return;
  }
  sas_expiry = (unsigned long)time(nullptr) + SAS_TTL;

  Serial.printf("[MQTT] Conectando a %s (SAS expira en %lus)...\n",
                IOT_HUB_HOST, SAS_TTL);
  if (mqtt_client.connect(CAMERA_DEVICE_ID, username.c_str(), sas.c_str())) {
    Serial.println("[MQTT] Conectado a Azure IoT Hub");
  } else {
    Serial.printf("[MQTT] Error de conexión rc=%d\n", mqtt_client.state());
  }
}

// Verifica que el SAS no haya expirado y que la conexión esté activa.
void ensureSasAndConnection() {
  unsigned long now = (unsigned long)time(nullptr);
  if (sas_expiry > 0 && now >= (sas_expiry - SAS_RENEW_BEFORE)) {
    Serial.println("[SAS] Token próximo a expirar — reconectando para renovar");
    if (mqtt_client.connected()) {
      mqtt_client.disconnect();
      delay(200);
    }
  }
  if (!mqtt_client.connected()) {
    Serial.println("[MQTT] Sin conexión — intentando reconectar...");
    conectarAzureIoT();
  }
}

// =============================================================================
// Publicar el resultado de inferencia por MQTT a Azure IoT Hub
// =============================================================================
bool publishToIoTHub(const InferenceResult &res) {
  StaticJsonDocument<512> doc;
  doc["device_id"] = CAMERA_DEVICE_ID;

  unsigned long ts = time(nullptr);
  doc["timestamp"] = (ts > MIN_VALID_TS) ? (long)ts : (long)(millis() / 1000UL);

  JsonObject camera = doc.createNestedObject("camera");
  camera["pcb_class"]     = res.pcb_class;
  camera["confidence"]    = serialized(String(res.confidence, 3));
  camera["inference_ms"]  = res.inference_ms;
  camera["model_version"] = MODEL_VERSION;

  JsonObject probs = camera.createNestedObject("probabilities");
  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    probs[ei_classifier_inferencing_categories[i]] =
      serialized(String(res.probs[i], 3));
  }

  char payload[512];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  bool ok = mqtt_client.publish(TOPIC_TELEMETRY.c_str(), payload, len);
  Serial.printf("[MQTT] → %s  %s\n", payload, ok ? "OK" : "ERROR");
  return ok;
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] cnc_camera_node — ESP32-CAM + MQTTS + Edge Impulse PCB Classifier");
  Serial.printf("[BOOT] PUBLISH_INTERVAL_MS=%lu\n", PUBLISH_INTERVAL_MS);

  if (!initCamera()) {
    Serial.println("[BOOT] Fallo crítico de cámara. Reiniciando en 5s...");
    delay(5000);
    ESP.restart();
  }

  // Wi-Fi → NTP → Azure IoT Hub
  conectarWiFi();
  setupTime();

  // Topic dinámico con CAMERA_DEVICE_ID
  TOPIC_TELEMETRY = String("devices/") + CAMERA_DEVICE_ID + "/messages/events/";

  conectarAzureIoT();

  Serial.println("[BOOT] Listo. Iniciando inferencia...");
}

// =============================================================================
// Loop — inferencia continua con publicación controlada por MQTT
// =============================================================================
void loop() {
  // Mantener conexión a Azure IoT Hub activa y SAS vigente
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconectando...");
    WiFi.reconnect();
    delay(3000);
    return;
  }

  ensureSasAndConnection();
  if (mqtt_client.connected()) {
    mqtt_client.loop();
  }

  // Ejecutar inferencia
  InferenceResult res = runInference();
  if (!res.valid) {
    delay(500);
    return;
  }

  // Aplicar regla de publicación controlada
  if (shouldPublish(res.pcb_class)) {
    if (publishToIoTHub(res)) {
      last_publish_ms = millis();
      strncpy(last_published_class, res.pcb_class, sizeof(last_published_class) - 1);
      last_published_class[sizeof(last_published_class) - 1] = '\0';
    }
  }

  // Breve pausa para no sobrecalentar el SoC
  delay(200);
}
