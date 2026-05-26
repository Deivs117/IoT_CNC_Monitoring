// =============================================================================
// capture_express.ino — ESP32-CAM AI Thinker | Firmware de captura de dataset
// =============================================================================
//
// Propósito: adquirir rápidamente el dataset de imágenes para entrenar el
//   modelo de Edge Impulse (PCB_Mixta / PCB_SMD / PCB_TH / Sin_PCB).
//
// Uso:
//   1. Editar WIFI_SSID y WIFI_PASS.
//   2. Board: AI Thinker ESP32-CAM.  Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS).
//   3. Flashear, abrir Serial Monitor a 115200, anotar la IP.
//   4. Verificar: curl http://<IP>/capture -o test.jpg
//   5. Ejecutar el script Python de captura automática del dataset.
//
// Endpoint expuesto:
//   GET /          → info de estado
//   GET /capture   → retorna JPEG crudo (Content-Type: image/jpeg)
//
// Resolución para el dataset: SVGA (800×600).
//   Edge Impulse redimensiona automáticamente al importar el dataset.
//   Si se prefiere capturar ya a 96×96, cambiar FRAMESIZE_SVGA → FRAMESIZE_96X96.
//
// NO usar en producción; este firmware es solo para construir el dataset.
// =============================================================================

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ── Credenciales WiFi ─────────────────────────────────────────────────────────
// Copiar camera_secrets.h.template → camera_secrets.h y rellenar WIFI_SSID y WIFI_PASS.
// camera_secrets.h está en .gitignore y NUNCA debe commitearse con credenciales reales.
#include "camera_secrets.h"

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

WebServer server(80);

// =============================================================================
// Inicialización de la cámara
// =============================================================================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Resolución alta para dataset de calidad; EI redimensiona al importar.
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_SVGA;  // 800×600
    config.jpeg_quality = 8;               // 0=mejor calidad, 63=peor
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    // Sin PSRAM, reducir resolución para evitar OOM
    config.frame_size   = FRAMESIZE_VGA;   // 640×480
    config.jpeg_quality = 12;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] esp_camera_init falló: 0x%x\n", err);
    return false;
  }

  // Ajuste fino del sensor OV2640
  sensor_t *s = esp_camera_sensor_get();
  s->set_brightness(s, 0);      // -2 a 2  (0 = neutro)
  s->set_contrast(s, 0);        // -2 a 2
  s->set_saturation(s, 0);      // -2 a 2
  s->set_special_effect(s, 0);  // 0 = sin efecto
  s->set_whitebal(s, 1);        // AWB activado
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);         // 0 = auto
  s->set_exposure_ctrl(s, 1);   // AEC activado
  s->set_aec2(s, 0);
  s->set_ae_level(s, 0);        // -2 a 2
  s->set_aec_value(s, 300);     // 0-1200
  s->set_gain_ctrl(s, 1);       // AGC activado
  s->set_agc_gain(s, 0);
  s->set_gainceiling(s, (gainceiling_t)0);
  s->set_bpc(s, 0);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);
  s->set_dcw(s, 1);
  s->set_colorbar(s, 0);

  Serial.println("[CAM] Cámara inicializada correctamente.");
  return true;
}

// =============================================================================
// Handler  GET /capture
// =============================================================================
void handleCapture() {
  // Descartar primer frame: la exposición automática puede tardar un ciclo.
  camera_fb_t *discard = esp_camera_fb_get();
  if (discard) {
    esp_camera_fb_return(discard);
  }
  delay(80);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Error: esp_camera_fb_get() retornó NULL");
    Serial.println("[CAM] Error al capturar frame");
    return;
  }

  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  Serial.printf("[CAM] /capture → %u bytes\n", fb->len);

  esp_camera_fb_return(fb);
}

// =============================================================================
// Handler  GET /
// =============================================================================
void handleRoot() {
  String ip   = WiFi.localIP().toString();
  bool hasPsram = psramFound();
  String body =
    "ESP32-CAM Dataset Capture Express\n"
    "-----------------------------------\n"
    "GET /capture  → JPEG crudo\n\n"
    "IP    : " + ip + "\n"
    "PSRAM : " + String(hasPsram ? "SI" : "NO") + "\n"
    "Uso   : curl http://" + ip + "/capture -o imagen.jpg\n";
  server.send(200, "text/plain", body);
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] ESP32-CAM Capture Express — Dataset IoT CNC PCB");

  if (!initCamera()) {
    Serial.println("[BOOT] Fallo crítico de cámara. Reiniciando en 5s...");
    delay(5000);
    ESP.restart();
  }

  Serial.printf("[WIFI] Conectando a %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
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
  Serial.printf("[HTTP] Endpoint: http://%s/capture\n", WiFi.localIP().toString().c_str());

  server.on("/",        HTTP_GET, handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);
  server.begin();
  Serial.println("[HTTP] Servidor iniciado en puerto 80.");
}

// =============================================================================
// Loop
// =============================================================================
void loop() {
  server.handleClient();
}
