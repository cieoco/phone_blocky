#pragma once
#include "Wire.h"
#define SSD1306_SWITCHCAPVCC 2
#define SSD1306_WHITE 1
#define SSD1306_BLACK 0
class Adafruit_SSD1306 {
public:
  Adafruit_SSD1306(int,int,TwoWire*,int,int,int) {}
  bool begin(int,uint8_t,bool,bool periphBegin) { return !periphBegin; }
  void clearDisplay() {}
  void setTextColor(int) {}
  void setTextSize(int) {}
  void setTextWrap(bool) {}
  void fillRect(int,int,int,int,int) {}
  void setCursor(int,int) {}
  void print(const String&) {}
  void display() {}
};
