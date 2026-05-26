// CNC IoT — ESP32 Nodo Principal | Azure IoT Hub (MQTTS, puerto 8883)
// Firmware:  Edge Impulse SDK (CNC_Monitor_Project_inferencing) + MPU-6050 + DHT22
// Conectividad: Azure IoT Hub directo con SAS token (HMAC-SHA256, mbedTLS)
// Actuador:  C2D + Direct Methods → ON / OFF / RESET → GPIO ACTUATOR_PIN
//
// Contrato JSON de telemetría publicado:
// {
//   "device_id":  "cnc_fresadora_01",
//   "timestamp":  <unix epoch>,
//   "sensors":    { "temperature": 24.5, "humidity": 45.2, "vibration_status": "normal" },
//   "predictions":{ "vibration_anomaly_score": 0.02, "visual_anomaly_score": null }
// }

#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "mbedtls/md.h"
#include "mbedtls/base64.h"

#include "config.h"
#include "edge_impulse_vibration.h"  // incluye CNC_Monitor_Project_inferencing.h
#include "sensors.h"

// =============================================================================
// Constantes de tiempo de muestreo
// EI_CLASSIFIER_INTERVAL_MS está definido por el SDK exportado de Edge Impulse.
// Si por algún motivo no está disponible se usa 10 ms (100 Hz).
// =============================================================================
#ifndef EI_CLASSIFIER_INTERVAL_MS
  #define EI_CLASSIFIER_INTERVAL_MS 10
#endif
static const unsigned long SAMPLE_DELAY_MS = (unsigned long)EI_CLASSIFIER_INTERVAL_MS;

// =============================================================================
// Buffer de entrada para Edge Impulse (datos crudos intercalados: ax, ay, az, ...)
// EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE = num_muestras × num_ejes
// =============================================================================
static float   ei_buffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
static size_t  ei_idx = 0;

// Callback requerido por signal_t del SDK de Edge Impulse
static int ei_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, ei_buffer + offset, length * sizeof(float));
    return EIDSP_OK;
}

// =============================================================================
// MQTT / Azure IoT Hub
// =============================================================================
WiFiClientSecure tls_client;
PubSubClient     mqtt_client(tls_client);
DHT              dht(DHT_PIN, DHT_TYPE);

// Topics construidos en runtime con DEVICE_ID
static String TOPIC_TELEMETRY;
static String TOPIC_COMMANDS;
static const char TOPIC_METHODS_SUB[] = "$iothub/methods/POST/#";

// SAS token
static unsigned long sas_expiry = 0;

// Lecturas de ambiente
static float         latest_temperature = 25.0f;
static float         latest_humidity    = 50.0f;
static unsigned long last_dht_ms        = 0;

// Timestamp mínimo válido (posterior al año 2001 aproximadamente)
static const unsigned long MIN_VALID_TS = 1000000000UL;

// =============================================================================
// Forward declarations
// =============================================================================
void        conectarWiFi();
void        setupTime();
String      urlEncode(const String &s);
String      createSasToken(const char *host, const char *deviceId,
                           const char *primaryKey, unsigned long ttlSeconds);
void        conectarAzureIoT();
void        ensureSasAndConnection();
void        publishTelemetry(const ei_impulse_result_t &result);
String      parseCommandFromPayload(const String &payload);
void        handleDirectMethod(const char *topic, const String &payload);
void        sendMethodResponse(const String &rid, int status, const String &body);
void        mqttCallback(char *topic, byte *payload, unsigned int length);
unsigned long resolveTimestamp();

// =============================================================================
// Helpers
// =============================================================================

unsigned long resolveTimestamp() {
    time_t now = time(nullptr);
    return (now > (time_t)MIN_VALID_TS) ? (unsigned long)now : millis() / 1000UL;
}

// Extrae el valor del campo "comando" desde JSON sencillo o usa texto plano
String parseCommandFromPayload(const String &payload) {
    String s = payload;
    s.trim();
    if (s.length() == 0) return "";

    if (s.charAt(0) == '{') {
        int idx = s.indexOf("\"comando\"");
        if (idx >= 0) {
            int colon = s.indexOf(':', idx);
            if (colon >= 0) {
                int q1 = s.indexOf('"', colon);
                if (q1 >= 0) {
                    int q2 = s.indexOf('"', q1 + 1);
                    if (q2 > q1) return s.substring(q1 + 1, q2);
                }
                int start = colon + 1;
                while (start < (int)s.length() && isSpace(s.charAt(start))) start++;
                int end = start;
                while (end < (int)s.length() &&
                       s.charAt(end) != ',' && s.charAt(end) != '}' &&
                       !isSpace(s.charAt(end))) end++;
                return s.substring(start, end);
            }
        }
        return s;
    }
    return s;
}

// Aplica el comando ON / OFF / RESET al actuador y devuelve true si reconocido
static bool applyActuatorCommand(const String &rawCmd) {
    String cmd = rawCmd;
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "ON") {
        digitalWrite(ACTUATOR_PIN, HIGH);
        Serial.println("[ACT] Actuador ENCENDIDO");
        return true;
    }
    if (cmd == "OFF") {
        digitalWrite(ACTUATOR_PIN, LOW);
        Serial.println("[ACT] Actuador APAGADO");
        return true;
    }
    if (cmd == "RESET") {
        digitalWrite(ACTUATOR_PIN, LOW);
        delay(100);
        digitalWrite(ACTUATOR_PIN, HIGH);
        delay(100);
        digitalWrite(ACTUATOR_PIN, LOW);
        Serial.println("[ACT] Actuador RESET");
        return true;
    }
    Serial.printf("[ACT] Comando no reconocido: %s\n", cmd.c_str());
    return false;
}

// =============================================================================
// Direct Methods ($iothub/methods/POST/{method}/?$rid={rid})
// =============================================================================

void sendMethodResponse(const String &rid, int status, const String &body) {
    if (rid.length() == 0) {
        Serial.println("[METHOD] Sin rid — no se puede responder");
        return;
    }
    String topicRes = String("$iothub/methods/res/") + String(status) + "/?$rid=" + rid;
    bool ok = mqtt_client.publish(topicRes.c_str(), body.c_str());
    Serial.printf("[METHOD] Respuesta topic=%s status=%d ok=%d\n",
                  topicRes.c_str(), status, ok);
}

void handleDirectMethod(const char *topic, const String &payload) {
    String t = String(topic);
    int p1 = t.indexOf("/POST/");
    if (p1 < 0) {
        Serial.printf("[METHOD] Topic inesperado: %s\n", topic);
        return;
    }
    int startMethod = p1 + 6;
    int qmark = t.indexOf("/?", startMethod);
    if (qmark < 0) qmark = t.indexOf('?', startMethod);

    String methodName;
    String rid;
    if (qmark > startMethod) {
        methodName = t.substring(startMethod, qmark);
        int ridIdx = t.indexOf("$rid=", qmark);
        if (ridIdx >= 0) {
            rid = t.substring(ridIdx + 5);
            int amp = rid.indexOf('&');
            if (amp >= 0) rid = rid.substring(0, amp);
        }
    } else {
        methodName = t.substring(startMethod);
    }
    methodName.trim();
    rid.trim();

    Serial.printf("[METHOD] method='%s' rid='%s' payload=%s\n",
                  methodName.c_str(), rid.c_str(), payload.c_str());

    if (methodName == "actuador") {
        String cmd = parseCommandFromPayload(payload);
        if (applyActuatorCommand(cmd)) {
            String body = String("{\"result\":\"OK\",\"comando\":\"") + cmd + "\"}";
            body.toUpperCase();
            sendMethodResponse(rid, 200, body);
        } else {
            sendMethodResponse(rid, 404, "{\"error\":\"Comando no reconocido\"}");
        }
    } else {
        sendMethodResponse(rid, 404, "{\"error\":\"Method not supported\"}");
    }
}

// =============================================================================
// Callback MQTT: C2D y Direct Methods
// =============================================================================

void mqttCallback(char *topic, byte *payload, unsigned int length) {
    String t = String(topic);
    String pl = "";
    for (unsigned int i = 0; i < length; i++) pl += (char)payload[i];
    pl.trim();

    Serial.printf("[MQTT] Mensaje en topic: %s\n  payload: %s\n", topic, pl.c_str());

    if (t.startsWith("$iothub/methods/POST/")) {
        handleDirectMethod(topic, pl);
        return;
    }

    if (t.indexOf("/messages/devicebound/") >= 0) {
        String cmd = parseCommandFromPayload(pl);
        applyActuatorCommand(cmd);
        return;
    }

    Serial.println("[MQTT] Topic no gestionado");
}

// =============================================================================
// Wi-Fi
// =============================================================================

void conectarWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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

// =============================================================================
// NTP
// =============================================================================

void setupTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("[TIME] Esperando NTP");
    int tries = 0;
    while (time(nullptr) < (time_t)MIN_VALID_TS && tries < 60) {
        Serial.print(".");
        delay(500);
        tries++;
    }
    Serial.println();
    if (time(nullptr) < (time_t)MIN_VALID_TS) {
        Serial.println("[TIME] No se sincronizó NTP — SAS token podría fallar");
    } else {
        Serial.printf("[TIME] Sincronizado: %lu\n", (unsigned long)time(nullptr));
    }
}

// =============================================================================
// SAS token (Azure IoT Hub)
// Genera un SharedAccessSignature usando HMAC-SHA256 con mbedTLS.
// La clave primaria del dispositivo debe estar en base64 (campo en config.h).
// =============================================================================

String urlEncode(const String &str) {
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

String createSasToken(const char *host, const char *deviceId,
                      const char *primaryKey, unsigned long ttlSeconds) {
    String resource        = String(host) + "/devices/" + String(deviceId);
    String resourceEncoded = urlEncode(resource);
    unsigned long expiry   = (unsigned long)time(nullptr) + ttlSeconds;
    String toSign          = resourceEncoded + "\n" + String(expiry);

    // Decodificar clave primaria de base64 a bytes
    size_t keyLen = strlen(primaryKey);
    unsigned char keyBin[128];
    size_t keyBinLen = 0;
    if (mbedtls_base64_decode(keyBin, sizeof(keyBin), &keyBinLen,
                              (const unsigned char *)primaryKey, keyLen) != 0) {
        Serial.println("[SAS] Error decodificando clave base64");
        return "";
    }

    // HMAC-SHA256
    unsigned char hmac[32];
    const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdInfo) {
        Serial.println("[SAS] md_info NULL");
        return "";
    }
    if (mbedtls_md_hmac(mdInfo, keyBin, keyBinLen,
                        (const unsigned char *)toSign.c_str(), toSign.length(),
                        hmac) != 0) {
        Serial.println("[SAS] Error HMAC");
        return "";
    }

    // Codificar HMAC a base64
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

// =============================================================================
// Conexión a Azure IoT Hub (MQTTS)
// =============================================================================

void conectarAzureIoT() {
    tls_client.setCACert(AZURE_ROOT_CA);
    mqtt_client.setServer(IOT_HUB_HOST, MQTT_PORT);
    mqtt_client.setCallback(mqttCallback);
    mqtt_client.setBufferSize(1024);

    String username = String(IOT_HUB_HOST) + "/" + DEVICE_ID + "/?api-version=2021-04-12";
    String sas = createSasToken(IOT_HUB_HOST, DEVICE_ID, DEVICE_PRIMARY_KEY, SAS_TTL);
    if (sas == "") {
        Serial.println("[MQTT] No se pudo generar SAS token — reintentando más tarde");
        delay(5000);
        return;
    }
    sas_expiry = (unsigned long)time(nullptr) + SAS_TTL;

    Serial.printf("[MQTT] Conectando a %s (SAS expira en %lus)...\n",
                  IOT_HUB_HOST, SAS_TTL);
    if (mqtt_client.connect(DEVICE_ID, username.c_str(), sas.c_str())) {
        Serial.println("[MQTT] Conectado a Azure IoT Hub");
        if (!mqtt_client.subscribe(TOPIC_COMMANDS.c_str())) {
            Serial.println("[MQTT] Error suscribiendo a C2D");
        }
        if (!mqtt_client.subscribe(TOPIC_METHODS_SUB)) {
            Serial.println("[MQTT] Error suscribiendo a Direct Methods");
        }
    } else {
        Serial.printf("[MQTT] Error de conexión rc=%d\n", mqtt_client.state());
    }
}

// Verifica que el SAS no haya expirado y que la conexión esté activa;
// reconecta automáticamente si es necesario.
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
// Publicación de telemetría
// Lee la clase ganadora y el anomaly score del resultado de Edge Impulse.
// =============================================================================

void publishTelemetry(const ei_impulse_result_t &result) {
    // Identificar clase ganadora y anomaly score
    int   best_class    = 0;
    float anomaly_score = 0.0f;
    const char *vibration_status = "unknown";

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].value >
            result.classification[best_class].value) {
            best_class = i;
        }
        // Buscar la clase "anomalia" para extraer su probabilidad directamente
        if (strcmp(result.classification[i].label, "anomalia") == 0) {
            anomaly_score = result.classification[i].value;
        }
    }
    vibration_status = result.classification[best_class].label;

    // Construir payload JSON conforme al contrato del proyecto
    StaticJsonDocument<384> doc;
    doc["device_id"]  = DEVICE_ID;
    doc["timestamp"]  = resolveTimestamp();

    JsonObject sensors = doc.createNestedObject("sensors");
    sensors["temperature"]     = latest_temperature;
    sensors["humidity"]        = latest_humidity;
    sensors["vibration_status"] = vibration_status;

    JsonObject predictions = doc.createNestedObject("predictions");
    predictions["vibration_anomaly_score"] = anomaly_score;
    predictions["visual_anomaly_score"]    = nullptr;

    char payload[384];
    serializeJson(doc, payload, sizeof(payload));

    bool ok = mqtt_client.publish(TOPIC_TELEMETRY.c_str(), payload);
    Serial.printf("[MQTT] → %s  %s\n", payload, ok ? "OK" : "ERROR");

    // LED de estado: encendido si hay anomalía por encima del umbral
    digitalWrite(STATUS_LED_PIN, anomaly_score >= ANOMALY_THRESHOLD ? HIGH : LOW);
}

// =============================================================================
// setup()
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== CNC IoT ESP32 — Azure IoT Hub + Edge Impulse ===");

    // GPIO
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    // ACTUATOR_PIN puede coincidir con STATUS_LED_PIN en ESP32-C3; ajustar si difieren
    if (ACTUATOR_PIN != STATUS_LED_PIN) {
        pinMode(ACTUATOR_PIN, OUTPUT);
        digitalWrite(ACTUATOR_PIN, LOW);
    }

    // MPU-6050
    Wire.begin(SDA_PIN, SCL_PIN);
    initMpu();

    // DHT22
    dht.begin();
    delay(2000);
    updateEnvironment(dht, &latest_temperature, &latest_humidity);
    last_dht_ms = millis();

    // Wi-Fi → NTP → Azure IoT Hub
    conectarWiFi();
    setupTime();

    // Topics dinámicos construidos con DEVICE_ID
    TOPIC_TELEMETRY = String("devices/") + DEVICE_ID + "/messages/events/";
    TOPIC_COMMANDS  = String("devices/") + DEVICE_ID + "/messages/devicebound/#";

    conectarAzureIoT();

    Serial.println("[BOOT] Nodo principal CNC listo");
}

// =============================================================================
// loop()
// =============================================================================

void loop() {
    // Mantener conexión a Azure IoT Hub activa y SAS vigente
    ensureSasAndConnection();
    if (mqtt_client.connected()) {
        mqtt_client.loop();
    }

    // Actualizar lecturas de ambiente cada DHT_INTERVAL_MS
    if (millis() - last_dht_ms >= DHT_INTERVAL_MS) {
        updateEnvironment(dht, &latest_temperature, &latest_humidity);
        last_dht_ms = millis();
    }

    // Leer aceleración del MPU-6050
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    if (!readAcceleration(&ax, &ay, &az)) {
        Serial.println("[MPU] Lectura I2C inválida");
        delay(SAMPLE_DELAY_MS);
        return;
    }

    // Llenar buffer de Edge Impulse con datos crudos intercalados (ax, ay, az)
    if (ei_idx < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        ei_buffer[ei_idx + 0] = ax;
        ei_buffer[ei_idx + 1] = ay;
        ei_buffer[ei_idx + 2] = az;
        ei_idx += 3;
    }

    // Cuando el buffer está lleno, ejecutar inferencia
    if (ei_idx >= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        signal_t signal;
        signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
        signal.get_data     = &ei_get_data;

        ei_impulse_result_t result = {0};
        EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);

        if (err != EI_IMPULSE_OK) {
            Serial.printf("[EI] Error run_classifier: %d\n", err);
        } else {
            Serial.println("------ Inferencia Edge Impulse ------");
            for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
                Serial.printf("  %-12s: %.4f\n",
                              result.classification[i].label,
                              result.classification[i].value);
            }
            publishTelemetry(result);
        }

        ei_idx = 0;
    }

    delay(SAMPLE_DELAY_MS);
}
