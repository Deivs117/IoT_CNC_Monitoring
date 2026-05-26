// =============================================================================
// cnc_camera_node.ino — ESP32-CAM AI Thinker | Inferencia Edge Impulse
// =============================================================================
//
// Clasificador local de PCBs para el sistema IoT CNC PCB Monitor.
// Clases: PCB_Mixta | PCB_SMD | PCB_TH | Sin_PCB
//
// Arquitectura:
//   ESP32-CAM → [Edge Impulse SDK] → HTTP POST → Azure Function /api/camara
//
// Contrato JSON publicado al backend:
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
//   - CNC_PCB_Classifier_inferencing (exportar desde Edge Impulse como librería Arduino,
//     instalar con: Sketch > Include Library > Add .ZIP Library)
//
// Board settings:
//   - Board: AI Thinker ESP32-CAM
//   - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
//   - Upload Speed: 115200 o 921600 con adaptador FTDI
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include <time.h>

// ── Librería exportada de Edge Impulse ────────────────────────────────────────
// Después de entrenar el modelo en Edge Impulse Studio:
//   Deployment → Arduino Library → Build → descargar el .zip
//   Arduino IDE: Sketch → Include Library → Add .ZIP Library
// El nombre del archivo .h varía según el nombre del proyecto en Edge Impulse.
// Ajustar el include a continuación con el nombre real del archivo generado:
#include <CNC_PCB_Classifier_inferencing.h>

// ── Credenciales WiFi ─────────────────────────────────────────────────────────
#define WIFI_SSID        "TU_SSID"
#define WIFI_PASS        "TU_PASSWORD"

// ── Backend Azure Function ────────────────────────────────────────────────────
// Reemplazar con la URL y clave reales del Function App.
// Obtener la clave con: az functionapp keys list --name <app-name> --resource-group <rg>
#define BACKEND_URL      "https://<func-app-name>.azurewebsites.net/api/camara"
#define BACKEND_FUNC_KEY "<function-host-key>"

// ── Identificación del dispositivo ───────────────────────────────────────────
#define DEVICE_ID        "cnc_camera_01"
#define MODEL_VERSION    "1.0.0"

// ── Control de publicación ────────────────────────────────────────────────────
// Publicar cada 10 segundos o inmediatamente si la clase cambia.
#define PUBLISH_INTERVAL_MS  10000UL

// ── NTP ───────────────────────────────────────────────────────────────────────
#define NTP_SERVER       "pool.ntp.org"
#define GMT_OFFSET_SEC   0
#define DST_OFFSET_SEC   0

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
// Estado de publicación (control de tasa)
// =============================================================================
static unsigned long last_publish_ms  = 0;
static char          last_published_class[32] = "";   // Clase publicada por última vez

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
    // EI espera el pixel empaquetado como 0x00RRGGBB en un float
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

  // EI necesita RGB565 para el callback de pixels definido arriba.
  // La resolución debe coincidir exactamente con EI_CLASSIFIER_INPUT_WIDTH × EI_CLASSIFIER_INPUT_HEIGHT.
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

  // Capturar frame
  g_fb = esp_camera_fb_get();
  if (!g_fb) {
    Serial.println("[EI] Error: no se pudo capturar frame");
    return result;
  }

  // Verificar dimensiones del frame
  if ((int)g_fb->width  != EI_CLASSIFIER_INPUT_WIDTH ||
      (int)g_fb->height != EI_CLASSIFIER_INPUT_HEIGHT) {
    Serial.printf("[EI] Dimensiones incorrectas: %ux%u (esperado %dx%d)\n",
      g_fb->width, g_fb->height,
      EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
    esp_camera_fb_return(g_fb);
    g_fb = nullptr;
    return result;
  }

  // Construir señal para el clasificador
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data     = &ei_camera_get_data;

  ei_impulse_result_t ei_result = {};
  unsigned long t_start = millis();
  EI_IMPULSE_ERROR err  = run_classifier(&signal, &ei_result, false);
  result.inference_ms   = (int)(millis() - t_start);

  // Liberar buffer de cámara
  esp_camera_fb_return(g_fb);
  g_fb = nullptr;

  if (err != EI_IMPULSE_OK) {
    Serial.printf("[EI] run_classifier falló: %d\n", err);
    return result;
  }

  // Encontrar clase con mayor probabilidad
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
//
// Regla:
//   1. Publicar si >= PUBLISH_INTERVAL_MS transcurrieron desde la última publicación.
//   2. Publicar de inmediato si la clase cambió respecto a la última publicación.
//   3. No publicar en caso contrario.
// =============================================================================
bool shouldPublish(const char *current_class) {
  unsigned long now     = millis();
  bool timeout_reached  = (now - last_publish_ms) >= PUBLISH_INTERVAL_MS;
  bool class_changed    = (strncmp(current_class, last_published_class,
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
// Construir y enviar el payload JSON al backend Azure
// =============================================================================
bool sendToBackend(const InferenceResult &res) {
  StaticJsonDocument<512> doc;
  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = (long)time(nullptr);

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

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  String url = String(BACKEND_URL) + "?code=" + BACKEND_FUNC_KEY;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  int code = http.POST(body);
  String response = http.getString();
  http.end();

  if (code >= 200 && code < 300) {
    Serial.printf("[HTTP] POST OK (%d)\n", code);
    return true;
  }

  Serial.printf("[HTTP] POST falló: HTTP %d | %s\n", code, response.c_str());
  return false;
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] cnc_camera_node — ESP32-CAM Edge Impulse PCB Classifier");
  Serial.printf("[BOOT] PUBLISH_INTERVAL_MS=%lu\n", PUBLISH_INTERVAL_MS);

  if (!initCamera()) {
    Serial.println("[BOOT] Fallo crítico de cámara. Reiniciando en 5s...");
    delay(5000);
    ESP.restart();
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Conectando a %s", WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WIFI] Sin conexión. Reiniciando...");
    delay(2000);
    ESP.restart();
  }
  Serial.printf("\n[WIFI] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());

  // Sincronizar hora con NTP (necesario para timestamps Unix del payload)
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
  Serial.print("[NTP] Sincronizando");
  time_t now = 0;
  unsigned long t1 = millis();
  while (now < 1000000000L && millis() - t1 < 15000) {
    time(&now);
    delay(200);
    Serial.print(".");
  }
  if (now < 1000000000L) {
    Serial.println("\n[NTP] No se pudo sincronizar. Se usará timestamp 0.");
  } else {
    Serial.printf("\n[NTP] Hora sincronizada: %ld\n", (long)now);
  }

  Serial.println("[BOOT] Listo. Iniciando inferencia...");
}

// =============================================================================
// Loop — inferencia continua con publicación controlada
// =============================================================================
void loop() {
  // Reconectar WiFi si es necesario
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Reconectando...");
    WiFi.reconnect();
    delay(3000);
    return;
  }

  // Ejecutar inferencia
  InferenceResult res = runInference();
  if (!res.valid) {
    delay(500);
    return;
  }

  // Aplicar regla de publicación controlada
  if (shouldPublish(res.pcb_class)) {
    if (sendToBackend(res)) {
      last_publish_ms = millis();
      strncpy(last_published_class, res.pcb_class, sizeof(last_published_class) - 1);
      last_published_class[sizeof(last_published_class) - 1] = '\0';
    }
  }

  // Breve pausa para no sobrecalentar el SoC; la inferencia ya tarda ~200-300ms
  delay(200);
}
