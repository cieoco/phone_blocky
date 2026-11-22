#pragma once
#include <Wire.h>
#include "config.h"

// jsonTask owns this bus. Drivers never end/reinitialize Wire independently.
class PeripheralI2cBus {
  bool started_ = false;
public:
  bool ensure() {
    if (started_) return true;
    if (!Wire.begin(I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN, 100000)) return false;
    Wire.setTimeOut(20);
    started_ = true;
    return true;
  }
  void end() {
    if (started_) Wire.end();
    started_ = false;
  }
};
