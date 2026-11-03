#pragma once
#include <Adafruit_SSD1306.h>
#include "config.h"

// Owned exclusively by jsonTask. No Wire calls from WS or I2C callbacks.
class OledDisplay {
  Adafruit_SSD1306 display_{OLED_WIDTH, OLED_HEIGHT, &Wire, -1, 400000, 100000};
  bool busStarted_ = false;
  uint8_t address_ = 0;
  String rows_[4];
  bool blank_ = true;
public:
  void end() {
    if (busStarted_) Wire.end();
    busStarted_ = false;
    address_ = 0;
  }
  bool ensure(uint8_t address) {
    if (!busStarted_) {
      if (!Wire.begin(I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN, 100000)) return false;
      Wire.setTimeOut(20);
      busStarted_ = true;
    }
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) { address_ = 0; return false; }
    if (address_ == address) return true;
    if (!display_.begin(SSD1306_SWITCHCAPVCC, address, false, false)) return false;
    address_ = address;
    display_.clearDisplay();
    for (auto &row : rows_) row = "";
    blank_ = true;
    display_.setTextColor(SSD1306_WHITE);
    display_.setTextSize(1);
    display_.setTextWrap(false);
    return true;
  }
  bool write(uint8_t address, int row, const String &text, bool clear) {
    // Do not flood I2C in the 10ms Blockly loop for unchanged content.
    if (address_ == address && ((clear && blank_) || (!clear && rows_[row] == text.substring(0, 21)))) return true;
    if (!ensure(address)) return false;
    if (clear) {
      display_.clearDisplay();
      for (auto &line : rows_) line = "";
      blank_ = true;
    }
    else {
      display_.fillRect(0, row * 8, 128, 8, SSD1306_BLACK);
      display_.setCursor(0, row * 8);
      display_.print(text.substring(0, 21));
      rows_[row] = text.substring(0, 21);
      blank_ = false;
    }
    display_.display();
    return true;
  }
};
