#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <Arduino.h>
#include <ESP32Servo.h>

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
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    servo1.setPeriodHertz(50);
    servo1.attach(servo1Pin, 500, 2500);
    servo1.write(90);

    servo2.setPeriodHertz(50);
    servo2.attach(servo2Pin, 500, 2500);
    servo2.write(90);
  }

  void controlServo(int servoNumber, int angle)
  {
    if (servoNumber == 1)
      servo1.write(angle);
    else if (servoNumber == 2)
      servo2.write(angle);
    else
      Serial.println("Invalid servo number");
  }
};

#endif
