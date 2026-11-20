#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ESP32Servo.h>

class ServoController
{
private:
  Servo servo1;
  Servo servo2;
  bool inhibited = false;
  SemaphoreHandle_t ioMutex = nullptr;
  bool detachOnStop[2] = {false, false};
  int servo1Pin;
  int servo2Pin;

public:
  ServoController(int pin1, int pin2) : servo1Pin(pin1), servo2Pin(pin2) {}

  void initServo()
  {
    // Timer 3 (channels 6/7) is isolated from AFMotor timers 1/2
    // and general-purpose PWM timer 0. ESP32Servo allocates within this timer.
    ioMutex = xSemaphoreCreateMutex();
    ESP32PWM::allocateTimer(3);

    servo1.setPeriodHertz(50);
    servo1.attach(servo1Pin, 500, 2500);
    servo1.write(90);

    servo2.setPeriodHertz(50);
    servo2.attach(servo2Pin, 500, 2500);
    servo2.write(90);
  }

  void setStopMode(int index, bool detach) { detachOnStop[index] = detach; }
  void setInhibited(bool stop) {
    if (!ioMutex) return;
    xSemaphoreTake(ioMutex, portMAX_DELAY);
    inhibited = stop;
    if (stop && detachOnStop[0]) servo1.detach();
    if (stop && detachOnStop[1]) servo2.detach();
    xSemaphoreGive(ioMutex);
  }
  void controlServo(int servoNumber, int angle)
  {
    if (!ioMutex) return;
    xSemaphoreTake(ioMutex, portMAX_DELAY);
    if (inhibited) {
      xSemaphoreGive(ioMutex);
      return;
    }
    if (servoNumber == 1) {
      if (!servo1.attached()) servo1.attach(servo1Pin, 500, 2500);
      servo1.write(angle);
    } else if (servoNumber == 2) {
      if (!servo2.attached()) servo2.attach(servo2Pin, 500, 2500);
      servo2.write(angle);
    }
    else
      Serial.println("Invalid servo number");
    xSemaphoreGive(ioMutex);
  }
};

#endif
