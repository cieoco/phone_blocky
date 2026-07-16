#include "EncoderHandler.h"
#include "soc/pcnt_struct.h"

// Initialize static members
volatile int16_t EncoderHandler::m3HighCount = 0;
volatile int16_t EncoderHandler::m4HighCount = 0;

void IRAM_ATTR EncoderHandler::pcnt_intr_handler(void *arg) {
  uint32_t intr_status = PCNT.int_st.val;
  for (int i = 0; i < PCNT_UNIT_MAX; i++) {
    if (intr_status & (BIT(i))) {
      if (PCNT.status_unit[i].h_lim_lat) {
        if (i == PCNT_UNIT_0)
          m3HighCount++;
        else if (i == PCNT_UNIT_1)
          m4HighCount++;
      } else if (PCNT.status_unit[i].l_lim_lat) {
        if (i == PCNT_UNIT_0)
          m3HighCount--;
        else if (i == PCNT_UNIT_1)
          m4HighCount--;
      }
      PCNT.int_clr.val = BIT(i);
    }
  }
}

void EncoderHandler::initPCNT(pcnt_unit_t unit, int pinA, int pinB) {
  pinMode(pinA, INPUT);
  pinMode(pinB, INPUT);

  pcnt_config_t pcnt_config = {
      .pulse_gpio_num = pinA,
      .ctrl_gpio_num = pinB,
      .lctrl_mode = PCNT_MODE_REVERSE, // Low: Reverse
      .hctrl_mode = PCNT_MODE_KEEP,    // High: Keep
      .pos_mode = PCNT_COUNT_INC,      // Rising edge: INC
      .neg_mode = PCNT_COUNT_DEC,      // Falling edge: DEC
      .counter_h_lim = 20000,
      .counter_l_lim = -20000,
      .unit = unit,
      .channel = PCNT_CHANNEL_0,
  };
  pcnt_unit_config(&pcnt_config);

  // Channel 1 for 4X decoding
  pcnt_config.pulse_gpio_num = pinB;
  pcnt_config.ctrl_gpio_num = pinA;
  pcnt_config.lctrl_mode = PCNT_MODE_KEEP;    // Low: Keep
  pcnt_config.hctrl_mode = PCNT_MODE_REVERSE; // High: Reverse
  pcnt_config.channel = PCNT_CHANNEL_1;
  pcnt_unit_config(&pcnt_config);

  pcnt_set_filter_value(unit, 100); // Filter noise
  pcnt_filter_enable(unit);

  pcnt_event_enable(unit, PCNT_EVT_H_LIM);
  pcnt_event_enable(unit, PCNT_EVT_L_LIM);

  pcnt_counter_pause(unit);
  pcnt_counter_clear(unit);
  pcnt_intr_enable(unit);
  pcnt_counter_resume(unit);
}

void EncoderHandler::begin() {
  initPCNT(PCNT_UNIT_0, M3_PIN_A, M3_PIN_B);
  initPCNT(PCNT_UNIT_1, M4_PIN_A, M4_PIN_B);

  static bool isr_installed = false;
  if (!isr_installed) {
    pcnt_isr_register(pcnt_intr_handler, NULL, 0, NULL);
    isr_installed = true;
  }
  Serial.println("[EncoderHandler] PCNT Hardware 4X Encoders initialized.");
}

long EncoderHandler::getM3Ticks() {
  int16_t count = 0;
  pcnt_get_counter_value(PCNT_UNIT_0, &count);
  return (long)m3HighCount * 20000 + count;
}

long EncoderHandler::getM4Ticks() {
  int16_t count = 0;
  pcnt_get_counter_value(PCNT_UNIT_1, &count);
  return (long)m4HighCount * 20000 + count;
}

void EncoderHandler::reset() {
  resetM3();
  resetM4();
}

void EncoderHandler::resetM3() {
  pcnt_counter_clear(PCNT_UNIT_0);
  m3HighCount = 0;
}

void EncoderHandler::resetM4() {
  pcnt_counter_clear(PCNT_UNIT_1);
  m4HighCount = 0;
}
