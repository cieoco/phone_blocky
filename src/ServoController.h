#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "competition/Gate.h"

class ServoController
{
private:
  Servo servo1;
  Servo servo2;
  int servo1Pin;
  int servo2Pin;

public:
  ServoController(int pin1, int pin2) : servo1Pin(pin1), servo2Pin(pin2) {}

  void initServo()
  {
    // Timer 3 (channels 6/7) is isolated from AFMotor timers 1/2
    // and general-purpose PWM timer 0. ESP32Servo allocates within this timer.
    ESP32PWM::allocateTimer(3);

#if COMPETITION_COMPETITOR
    return; // Competition boot must not attach or command a 90-degree movement.
#endif

    servo1.setPeriodHertz(50);
    servo1.attach(servo1Pin, 500, 2500);
    servo1.write(90);

    servo2.setPeriodHertz(50);
    servo2.attach(servo2Pin, 500, 2500);
    servo2.write(90);
  }

  void controlServo(int servoNumber, int angle)
  {
    if (!CompetitionGate::allowed()) return;
#if COMPETITION_COMPETITOR
    Servo* target = servoNumber == 1 ? &servo1 : servoNumber == 2 ? &servo2 : nullptr;
    if (!target) return;
    if (!target->attached()) {
      target->setPeriodHertz(50);
      target->attach(servoNumber == 1 ? servo1Pin : servo2Pin, 500, 2500);
    }
#endif
    if (servoNumber == 1)
      servo1.write(angle);
    else if (servoNumber == 2)
      servo2.write(angle);
    else
      Serial.println("Invalid servo number");
  }

  void disableOutputs() {
    if (servo1.attached()) servo1.detach();
    if (servo2.attached()) servo2.detach();
  }
};

#endif
