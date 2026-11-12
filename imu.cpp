#include "imu.h"
#include <Arduino.h>
#include <Wire.h>

static uint8_t gAddr = 0;

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(gAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(gAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(gAddr, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void imuBegin(bool enable) {
  gAddr = 0;
  const uint8_t addrs[] = { 0x6B, 0x6A };
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) continue;
    if (Wire.requestFrom(a, 1) != 1 || Wire.read() != 0x05) continue;
    gAddr = a;
    break;
  }
  if (!gAddr) return;
  if (!enable) {
    writeReg(0x08, 0x00);  // CTRL7: accel and gyro off
    writeReg(0x02, 0x01);  // CTRL1: sensor disable
    gAddr = 0;
    return;
  }
  writeReg(0x02, 0x40);          // address auto-increment, sensor enabled
  writeReg(0x03, (1 << 4) | 0x0D);  // CTRL2: ±4 g, low-power ~21 Hz
  writeReg(0x08, 0x01);          // CTRL7: accelerometer only
}

bool imuRaised() {
  if (!gAddr) return false;
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 200) return false;
  last = now;
  uint8_t raw[6];
  if (!readRegs(0x35, raw, 6)) return false;  // AX_L
  int16_t ax = (int16_t)(raw[0] | (raw[1] << 8));
  int16_t ay = (int16_t)(raw[2] | (raw[3] << 8));
  int16_t az = (int16_t)(raw[4] | (raw[5] << 8));
  static int16_t px, py, pz;
  static bool have = false;
  int32_t dot = (int32_t)ax * px + (int32_t)ay * py + (int32_t)az * pz;
  int32_t m1 = (int32_t)ax * ax + (int32_t)ay * ay + (int32_t)az * az;
  int32_t m2 = (int32_t)px * px + (int32_t)py * py + (int32_t)pz * pz;
  px = ax; py = ay; pz = az;
  if (!have) { have = true; return false; }
  // ~1 g on the ±4 g scale is 8192 counts. Ignore free-fall and the first garbage sample.
  if (m1 < 20000000 || m2 < 20000000) return false;
  // cos(theta) < 0.85 (about 32 degrees) counts as a lift.
  int64_t lhs = (int64_t)dot * dot;
  int64_t rhs = (int64_t)m1 * m2 * 72 / 100;
  return lhs < rhs;
}
