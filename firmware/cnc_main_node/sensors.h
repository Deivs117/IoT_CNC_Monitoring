#pragma once

#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>

#include "config.h"

struct SensorSnapshot {
  float accel_x;
  float accel_y;
  float accel_z;
  float temperature;
  float humidity;
};

inline void initMpu() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x00);
  Wire.endTransmission(true);
}

inline void readAcceleration(float* ax, float* ay, float* az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  int16_t raw_x = (Wire.read() << 8) | Wire.read();
  int16_t raw_y = (Wire.read() << 8) | Wire.read();
  int16_t raw_z = (Wire.read() << 8) | Wire.read();

  constexpr float scale = 9.81f / 16384.0f;
  *ax = raw_x * scale;
  *ay = raw_y * scale;
  *az = raw_z * scale;
}

inline void updateEnvironment(DHT& dht, float* temperature, float* humidity) {
  float next_temperature = dht.readTemperature();
  float next_humidity = dht.readHumidity();

  if (!isnan(next_temperature) && !isnan(next_humidity)) {
    *temperature = next_temperature;
    *humidity = next_humidity;
  }
}

inline void computeFeatures(
    const float* buffer_x,
    const float* buffer_y,
    const float* buffer_z,
    int sample_count,
    float* features) {
  float sum_x = 0.0f;
  float sum_y = 0.0f;
  float sum_z = 0.0f;

  for (int i = 0; i < sample_count; ++i) {
    sum_x += buffer_x[i];
    sum_y += buffer_y[i];
    sum_z += buffer_z[i];
  }

  float mean_x = sum_x / sample_count;
  float mean_y = sum_y / sample_count;
  float mean_z = sum_z / sample_count;

  float variance_x = 0.0f;
  float variance_y = 0.0f;
  float variance_z = 0.0f;

  for (int i = 0; i < sample_count; ++i) {
    variance_x += (buffer_x[i] - mean_x) * (buffer_x[i] - mean_x);
    variance_y += (buffer_y[i] - mean_y) * (buffer_y[i] - mean_y);
    variance_z += (buffer_z[i] - mean_z) * (buffer_z[i] - mean_z);
  }

  features[0] = mean_x;
  features[1] = variance_x / sample_count;
  features[2] = mean_y;
  features[3] = variance_y / sample_count;
  features[4] = mean_z;
  features[5] = variance_z / sample_count;
}
