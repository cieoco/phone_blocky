#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <Arduino.h>
#include "AFMotor.h"

class MotorController
{
private:
  AF_DCMotor motor1;
  AF_DCMotor motor2;
  AF_DCMotor motor3;
  AF_DCMotor motor4;

public:
  MotorController() : motor1(1), motor2(2), motor3(3), motor4(4) {
    // 建構子保持最小化，避免在全域初始化時崩潰
  }

  void begin() {
    motor1.run(RELEASE);
    motor2.run(RELEASE);
    motor3.run(RELEASE);
    motor4.run(RELEASE);
    Serial.println("[MotorController] 馬達控制器已初始化 (M1-M4)");
  }

  // PWM ±100 interface aligned with motorControl's "cmd":"pwm" verb
  void pwmSigned(int motorNumber, int duty) {
    duty = constrain(duty, -100, 100);
    if (duty == 0) {
      controlMotor(motorNumber, 'R', 0);
    } else if (duty > 0) {
      controlMotor(motorNumber, 'F', (int)(duty * 255L / 100));
    } else {
      controlMotor(motorNumber, 'B', (int)((-duty) * 255L / 100));
    }
  }

  void stopMotor(int motorNumber) {
    controlMotor(motorNumber, 'R', 0);
  }

  void stopAll() {
    for (int i = 1; i <= 4; i++) controlMotor(i, 'R', 0);
  }

  void controlMotor(int motorNumber, char direction, int speed)
  {
    // AFMotor control (motors 1-4)
    if (motorNumber >= 1 && motorNumber <= 4) {
      AF_DCMotor *motor;
      switch (motorNumber)
      {
      case 1:
        motor = &motor1;
        break;
      case 2:
        motor = &motor2;
        break;
      case 3:
        motor = &motor3;
        break;
      case 4:
        motor = &motor4;
        break;
      default:
        return;
      }

      if (direction == 'F' || direction == 'f')
        motor->run(FORWARD);
      else if (direction == 'B' || direction == 'b')
        motor->run(BACKWARD);
      else if (direction == 'R' || direction == 'r')
        motor->run(RELEASE);
      else if (direction == 'S' || direction == 's')
        motor->run(BRAKE);
      else
      {
        Serial.println("Invalid direction");
        return;
      }
      motor->setSpeed(speed);
    }
    else {
      Serial.printf("Invalid motor number: %d (Supported range: 1-4)\n", motorNumber);
      return;
    }
  }
};

#endif
