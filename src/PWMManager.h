#ifndef PWM_MANAGER_H
#define PWM_MANAGER_H

#include <Arduino.h>

class PWMManager
{
public:
  static const int MAX_CHANNELS = 16;
  int pinChannel[MAX_CHANNELS];
  bool channelInitialized[MAX_CHANNELS];

  PWMManager()
  {
    initChannels();
  }

  void initChannels()
  {
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
      pinChannel[i] = -1;
      channelInitialized[i] = false;
    }
  }

  int getChannelForPin(int pin)
  {
    // Board-owned pins must never be detached/reconfigured by generic PWM.
    if (pin != 0 && pin != 2 && pin != 4 && pin != 15 && pin != 18 &&
        pin != 21 && pin != 22 && pin != 26 && pin != 32 && pin != 33)
      return -1;
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
    {
      if (pinChannel[ch] == pin)
      {
        return ch;
      }
    }
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
    {
      // Timer 0 only: motors use timers 1/2, servos use timer 3.
      if ((ch == 0 || ch == 1 || ch == 8 || ch == 9) && pinChannel[ch] == -1)
      {
        pinChannel[ch] = pin;
        return ch;
      }
    }
    return -1; // no available channel
  }

  void dynamicAnalogWrite(int pin, int value)
  {
    int ch = getChannelForPin(pin);
    if (ch < 0)
    {
      Serial.println("Error: 沒有可用的 PWM channel");
      return;
    }
    if (!channelInitialized[ch])
    {
      ledcSetup(ch, 5000, 8);
      ledcAttachPin(pin, ch);
      channelInitialized[ch] = true;
    }
    ledcWrite(ch, value);
  }
};

#endif
