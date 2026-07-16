/*
 * Basic DC Motor Control Example
 * 
 * This example demonstrates basic control of a DC motor using the AFMotor library
 * modified for ESP32. The motor will run forward, backward, and stop in a loop.
 * 
 * Hardware connections:
 * - Connect your motor shield to the ESP32 according to the pin definitions in AFMotor.h
 * - Connect a DC motor to motor port 1 on the shield
 * 
 * Created: 2025
 * Modified for ESP32
 */

#include <AFMotor.h>

// Create motor object for motor port 1
// Parameters: motor number, PWM frequency (default 1000Hz), PWM resolution (default 8-bit)
AF_DCMotor motor1(1);

void setup() {
  Serial.begin(115200);
  Serial.println("AFMotor ESP32 Basic DC Motor Test");
  
  // Set initial motor speed (0-255)
  motor1.setSpeed(150);
  
  // Release motor to ensure it's stopped
  motor1.run(RELEASE);
}

void loop() {
  Serial.println("Motor running forward");
  motor1.run(FORWARD);
  delay(2000);
  
  Serial.println("Motor stopped");
  motor1.run(RELEASE);
  delay(1000);
  
  Serial.println("Motor running backward");
  motor1.run(BACKWARD);
  delay(2000);
  
  Serial.println("Motor stopped");
  motor1.run(RELEASE);
  delay(1000);
  
  // Change speed demonstration
  Serial.println("Speed test - slow to fast");
  motor1.run(FORWARD);
  for (int speed = 50; speed <= 255; speed += 50) {
    motor1.setSpeed(speed);
    Serial.print("Speed: ");
    Serial.println(speed);
    delay(1000);
  }
  
  motor1.run(RELEASE);
  delay(2000);
}
