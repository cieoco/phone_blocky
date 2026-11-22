#pragma once
#include <Arduino.h>
#include "PeripheralI2cBus.h"
#include "Tcs34725Math.h"

// Same ATIME/gain as motorControl, with checked transfers and nonblocking warmup.
class Tcs34725Sensor {
  PeripheralI2cBus &bus_;
  enum Phase { OFF, POWERING, INTEGRATING, READY, FAILED } phase_ = OFF;
  uint32_t since_ = 0, sampled_ = 0;
  uint16_t channels_[4] = {}; // R G B C
  bool cached_ = false;
  int cycles_ = 10, gain_ = 1;
  uint32_t sampleMs() const { return (cycles_ * 24U + 9) / 10; }
  uint8_t gainReg() const { return gain_ == 60 ? 3 : gain_ == 16 ? 2 : gain_ == 4 ? 1 : 0; }
  bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(TCS34725_ADDRESS);
    Wire.write(0x80 | reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }
  bool readRegs(uint8_t reg, uint8_t *out, uint8_t count) {
    Wire.beginTransmission(TCS34725_ADDRESS);
    Wire.write(0xA0 | reg); // command + auto increment
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(TCS34725_ADDRESS, count) != count) {
      while (Wire.available()) Wire.read();
      return false;
    }
    for (uint8_t i = 0; i < count; ++i) out[i] = Wire.read();
    return true;
  }
  int fail(uint32_t now) {
    phase_ = FAILED; since_ = now; cached_ = false;
    return -1;
  }
public:
  explicit Tcs34725Sensor(PeripheralI2cBus &bus) : bus_(bus) {}
  void reset() { phase_ = OFF; cached_ = false; }
  void defaults() { cycles_ = 10; gain_ = 1; reset(); }
  bool setGain(int gain) {
    if (gain != 1 && gain != 4 && gain != 16 && gain != 60) return false;
    if (gain != gain_) { gain_ = gain; reset(); }
    return true;
  }
  bool setCycles(int cycles) {
    if (cycles != 1 && cycles != 10 && cycles != 21 && cycles != 42 && cycles != 64 && cycles != 256) return false;
    if (cycles != cycles_) { cycles_ = cycles; reset(); }
    return true;
  }
  int rgb(int channel) {
    if (channel < 0 || channel > 2) return -1;
    const int clear = readChannel(3);
    if (clear <= 0) return clear;
    const int scaled = (channels_[channel] * 255 + clear / 2) / clear;
    return scaled > 255 ? 255 : scaled;
  }
  int derived(int item) {
    if (readChannel(3) < 0) return -1;
    return item == 2 ? Tcs34725Math::color(channels_, cycles_) :
      Tcs34725Math::estimate(channels_, cycles_, gain_, item == 1);
  }
  bool failed() const { return phase_ == FAILED; }
  int readChannel(int channel) {
    if (channel < 0 || channel > 3) return -1;
    const uint32_t now = millis();
    if (phase_ == FAILED) {
      if (uint32_t(now - since_) < 500) return -1;
      phase_ = OFF;
    }
    if (phase_ == OFF) {
      uint8_t id = 0;
      if (!bus_.ensure() || !readRegs(0x12, &id, 1) || (id != 0x44 && id != 0x4d) ||
          !writeReg(0x00, 0x01) || // stop ADC before changing exposure
          !writeReg(0x01, uint8_t(256 - cycles_)) || !writeReg(0x0F, gainReg())) return fail(now);
      phase_ = POWERING; since_ = now;
      return -1;
    }
    if (phase_ == POWERING) {
      if (uint32_t(now - since_) < 3) return -1;
      if (!writeReg(0x00, 0x03)) return fail(now);
      phase_ = INTEGRATING; since_ = now;
      return -1;
    }
    if (phase_ == INTEGRATING && uint32_t(now - since_) < sampleMs()) return -1;
    if (cached_ && uint32_t(now - sampled_) < sampleMs()) return channels_[channel];
    uint8_t status = 0;
    if (!readRegs(0x13, &status, 1)) return fail(now);
    if (!(status & 0x01)) {
      if (uint32_t(now - (cached_ ? sampled_ : since_)) >= sampleMs() + 100) return fail(now);
      return -1;
    }
    uint8_t raw[8];
    if (!readRegs(0x14, raw, 8)) return fail(now);
    for (int i = 0; i < 4; ++i) {
      const int offset = i == 3 ? 0 : (i + 1) * 2;
      channels_[i] = uint16_t(raw[offset]) | (uint16_t(raw[offset + 1]) << 8);
    }
    sampled_ = now; cached_ = true; phase_ = READY;
    return channels_[channel];
  }
};
