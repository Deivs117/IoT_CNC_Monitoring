#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <TensorFlowLite_ESP32.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "config.h"
#include "edge_impulse_vibration.h"
#include "sensors.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
  tflite::ErrorReporter* error_reporter = nullptr;
  const tflite::Model* tf_model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* input_tensor = nullptr;
  TfLiteTensor* output_tensor = nullptr;

  static tflite::MicroErrorReporter micro_error_reporter;
  static tflite::AllOpsResolver resolver;

  constexpr size_t TENSOR_ARENA_SIZE = 8 * 1024;
  alignas(16) uint8_t tensor_arena[TENSOR_ARENA_SIZE];
}

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);
DHT dht(DHT_PIN, DHT_TYPE);

float buffer_x[WINDOW_SIZE];
float buffer_y[WINDOW_SIZE];
float buffer_z[WINDOW_SIZE];
float latest_temperature = 25.0f;
float latest_humidity = 50.0f;
unsigned long last_dht_read_ms = 0;
int buffer_index = 0;
bool window_ready = false;

constexpr const char* LABELS[] = {"reposo", "normal", "anomalia"};
constexpr unsigned long MIN_VALID_UNIX_TIMESTAMP = 100000UL;

bool isPlaceholderCredential(const char* value) {
  return value == nullptr || value[0] == '\0' ||
         strcmp(value, MQTT_USERNAME_PLACEHOLDER) == 0 ||
         strcmp(value, MQTT_PASSWORD_PLACEHOLDER) == 0;
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }

  Serial.println("\n[WiFi] Conectado");
}

void connectMqtt() {
  mqtt_client.setServer(MQTT_BROKER, MQTT_PORT);

  while (!mqtt_client.connected()) {
    Serial.print("[MQTT] Conectando...");

    bool connected = mqtt_client.connect(
        DEVICE_ID,
        isPlaceholderCredential(MQTT_USERNAME) ? nullptr : MQTT_USERNAME,
        isPlaceholderCredential(MQTT_PASSWORD) ? nullptr : MQTT_PASSWORD);

    if (connected) {
      Serial.println(" conectado");
      return;
    }

    Serial.printf(" fallo rc=%d\n", mqtt_client.state());
    delay(2000);
  }
}

unsigned long resolveTimestamp() {
  time_t now = time(nullptr);
  // Filtra timestamps inválidos cercanos a cero antes de caer al respaldo con millis()/1000.
  if (now > MIN_VALID_UNIX_TIMESTAMP) {
    return static_cast<unsigned long>(now);
  }
  return millis() / 1000UL;
}

void normalizeFeatures(float* features) {
  for (int i = 0; i < NUM_INPUTS; ++i) {
    features[i] = (features[i] - SCALER_MEAN[i]) / (SCALER_STD[i] + EPSILON_DIVISION_GUARD);
  }
}

int argmax(const float* values, int size) {
  int best_index = 0;
  for (int i = 1; i < size; ++i) {
    if (values[i] > values[best_index]) {
      best_index = i;
    }
  }
  return best_index;
}

void publishTelemetry(const float* probabilities, int predicted_class) {
  StaticJsonDocument<384> doc;
  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = resolveTimestamp();

  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["temperature"] = latest_temperature;
  sensors["humidity"] = latest_humidity;
  sensors["vibration_status"] = LABELS[predicted_class];

  JsonObject predictions = doc.createNestedObject("predictions");
  predictions["vibration_anomaly_score"] = probabilities[2];
  predictions["visual_anomaly_score"] = nullptr;

  char payload[384];
  serializeJson(doc, payload, sizeof(payload));
  mqtt_client.publish(MQTT_TELEMETRY_TOPIC, payload);

  Serial.println(payload);
  digitalWrite(STATUS_LED_PIN, probabilities[2] >= ANOMALY_THRESHOLD ? HIGH : LOW);
}

void setupModel() {
  error_reporter = &micro_error_reporter;
  tf_model = tflite::GetModel(g_model);

  if (tf_model->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter, "Version de modelo incompatible");
    return;
  }

  static tflite::MicroInterpreter static_interpreter(
      tf_model,
      resolver,
      tensor_arena,
      TENSOR_ARENA_SIZE,
      error_reporter);

  interpreter = &static_interpreter;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("[TFLite] AllocateTensors fallo");
    return;
  }

  input_tensor = interpreter->input(0);
  output_tensor = interpreter->output(0);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  initMpu();

  dht.begin();
  delay(2000);
  updateEnvironment(dht, &latest_temperature, &latest_humidity);
  last_dht_read_ms = millis();

  connectWifi();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  connectMqtt();
  setupModel();

  Serial.println("[BOOT] Nodo principal CNC listo");
}

void loop() {
  if (!mqtt_client.connected()) {
    connectMqtt();
  }
  mqtt_client.loop();

  if (millis() - last_dht_read_ms >= DHT_INTERVAL_MS) {
    updateEnvironment(dht, &latest_temperature, &latest_humidity);
    last_dht_read_ms = millis();
  }

  float accel_x = 0.0f;
  float accel_y = 0.0f;
  float accel_z = 0.0f;
  if (!readAcceleration(&accel_x, &accel_y, &accel_z)) {
    Serial.println("[MPU] Lectura I2C invalida");
    delay(SAMPLE_DELAY_MS);
    return;
  }

  buffer_x[buffer_index] = accel_x;
  buffer_y[buffer_index] = accel_y;
  buffer_z[buffer_index] = accel_z;
  buffer_index = (buffer_index + 1) % WINDOW_SIZE;

  if (buffer_index == 0) {
    window_ready = true;
  }

  if (window_ready && interpreter != nullptr && input_tensor != nullptr && output_tensor != nullptr) {
    float features[NUM_INPUTS] = {0.0f};
    computeFeatures(buffer_x, buffer_y, buffer_z, WINDOW_SIZE, features);
    features[6] = latest_temperature;
    features[7] = latest_humidity;
    normalizeFeatures(features);

    for (int i = 0; i < NUM_INPUTS; ++i) {
      input_tensor->data.f[i] = features[i];
    }

    if (interpreter->Invoke() == kTfLiteOk) {
      float probabilities[NUM_OUTPUTS];
      for (int i = 0; i < NUM_OUTPUTS; ++i) {
        probabilities[i] = output_tensor->data.f[i];
      }
      publishTelemetry(probabilities, argmax(probabilities, NUM_OUTPUTS));
    } else {
      Serial.println("[TFLite] Invoke fallo");
    }

    window_ready = false;
  }

  delay(SAMPLE_DELAY_MS);
}
