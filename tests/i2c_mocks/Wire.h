#pragma once
#include "Arduino.h"
#include <vector>
class TwoWire {
public:
  std::vector<uint8_t> sample = {0x34,0x12, 0x78,0x56, 0xbc,0x9a, 0xf0,0xde};
  bool connected = true, startOk = true, ready = true, shortRead = false;
  int nackReg = -1, begins = 0, ends = 0, reads = 0, bursts = 0;
  uint8_t id = 0x44, address = 0, reg = 0;
  uint8_t registers[256] = {};
  std::vector<uint8_t> tx, rx;
  unsigned cursor = 0;
  bool begin(int sda, int scl, uint32_t hz) {
    if (sda != 21 || scl != 22 || hz != 100000) return false;
    ++begins; return startOk;
  }
  void setTimeOut(int) {}
  void end() { ++ends; }
  void beginTransmission(uint8_t a) { address = a; tx.clear(); }
  void write(uint8_t v) { tx.push_back(v); }
  int endTransmission(bool = true) {
    if (address != 0x29) return address == 0x3c ? 0 : 2;
    if (!connected) return 2;
    if (!tx.empty()) reg = tx[0] & 0x1f;
    if (reg == nackReg) return 3;
    if (tx.size() == 2) registers[reg] = tx[1];
    return 0;
  }
  uint8_t requestFrom(uint8_t, uint8_t count) {
    ++reads; if (reg == 0x14) ++bursts;
    rx.clear(); cursor = 0;
    if (reg == 0x12) rx = {id};
    else if (reg == 0x13) rx = {uint8_t(ready ? 1 : 0)};
    else if (reg == 0x14) rx = sample;
    if (shortRead && !rx.empty()) rx.pop_back();
    return uint8_t(rx.size() < count ? rx.size() : count);
  }
  int available() { return int(rx.size() - cursor); }
  uint8_t read() { return rx.at(cursor++); }
};
extern TwoWire Wire;
