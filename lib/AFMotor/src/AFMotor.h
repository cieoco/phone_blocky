// Adafruit Motor shield library
// for ESP8266 Wemos D1 R2

#ifndef _AFMotor_h_
#define _AFMotor_h_

#include <Arduino.h>

#include <inttypes.h>
#define MOTORDEBUG 1
#include "stdlib_noniso.h"
#define MICROSTEPS 8
#define DC_MOTOR_PWM_RATE 1000
#define STEPPER1_PWM_RATE 5
#define STEPPER2_PWM_RATE 5
#define PWMRESOLUTION 8

//變量用於選擇通道號 
#define PWM1_CHANNEL 5
#define PWM2_CHANNEL 2
#define PWM3_CHANNEL 3
#define PWM4_CHANNEL 4
// logic pins for "PWM"
#define MOTOR1_EN GPIO_NUM_23
#define MOTOR2_EN GPIO_NUM_25
#define MOTOR3_EN GPIO_NUM_27
#define MOTOR4_EN GPIO_NUM_16

// Bit positions in the 74HCT595 shift register output
#define MOTOR1_A 2
#define MOTOR1_B 3
#define MOTOR2_A 1
#define MOTOR2_B 4
#define MOTOR3_A 5
#define MOTOR3_B 7
#define MOTOR4_A 0
#define MOTOR4_B 6

// Constants that the user passes in to the motor calls
#define FORWARD 1
#define BACKWARD 2
#define BRAKE 3
#define RELEASE 4

// Constants that the user passes in to the stepper calls
#define SINGLE 1
#define DOUBLE 2
#define INTERLEAVE 3
#define MICROSTEP 4

// Arduino pin names for interface to 74HCT595 latch
#define MOTORLATCH GPIO_NUM_19
#define MOTORCLK GPIO_NUM_17
#define MOTORENABLE GPIO_NUM_14 // used to be 7
#define MOTORDATA GPIO_NUM_12   // used to be 8

class AFMotorController
{
public:
  AFMotorController(void);
  void enable(void);
  friend class AF_DCMotor;
  void latch_tx(void);
  uint8_t TimerInitalized;
};

class AF_DCMotor
{
public:
  AF_DCMotor(uint8_t motornum,uint16_t freq = DC_MOTOR_PWM_RATE,uint8_t resol=PWMRESOLUTION);
  void run(uint8_t);
  void setSpeed(uint8_t);

private:
  uint8_t motornum,  pwmresol;
  uint16_t  pwmfreq;
};

#endif