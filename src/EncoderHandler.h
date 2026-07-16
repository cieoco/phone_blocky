#ifndef ENCODER_HANDLER_H
#define ENCODER_HANDLER_H

#include "config.h"
#include "driver/pcnt.h"
#include <Arduino.h>

class EncoderHandler {
public:
  EncoderHandler() {}

  void begin();
  long getM3Ticks();
  long getM4Ticks();
  void reset();
  void resetM3();
  void resetM4();

private:
  static const int M3_PIN_A = ENCODER_M3_PIN_A;
  static const int M3_PIN_B = ENCODER_M3_PIN_B;
  static const int M4_PIN_A = ENCODER_M4_PIN_A;
  static const int M4_PIN_B = ENCODER_M4_PIN_B;

  static volatile int16_t m3HighCount;
  static volatile int16_t m4HighCount;

  static void IRAM_ATTR pcnt_intr_handler(void *arg);
  void initPCNT(pcnt_unit_t unit, int pinA, int pinB);
};

#endif
