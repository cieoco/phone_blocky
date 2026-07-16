/*
 * Multiple Motors Control Example
 * 
 * This example demonstrates control of multiple DC motors using the AFMotor library
 * modified for ESP32. It shows how to control 4 motors independently.
 * 
 * Hardware connections:
 * - Connect your motor shield to the ESP32 according to the pin definitions in AFMotor.h
 * - Connect DC motors to motor ports 1, 2, 3, and 4 on the shield
 * 
 * Created: 2025
 * Modified for ESP32
 */

#include <AFMotor.h>

// Create motor objects for all 4 motor ports
AF_DCMotor motor1(1);
AF_DCMotor motor2(2);
AF_DCMotor motor3(3);
AF_DCMotor motor4(4);

void setup() {
  Serial.begin(115200);
  Serial.println("AFMotor ESP32 Multiple Motors Test");
  
  // Set initial motor speeds
  motor1.setSpeed(200);
  motor2.setSpeed(180);
  motor3.setSpeed(160);
  motor4.setSpeed(140);
  
  // Release all motors to ensure they're stopped
  motor1.run(RELEASE);
  motor2.run(RELEASE);
  motor3.run(RELEASE);
  motor4.run(RELEASE);
}

void loop() {
  // Test 1: All motors forward
  Serial.println("All motors forward");
  motor1.run(FORWARD);
  motor2.run(FORWARD);
  motor3.run(FORWARD);
  motor4.run(FORWARD);
  delay(2000);
  
  // Stop all motors
  Serial.println("All motors stopped");
  motor1.run(RELEASE);
  motor2.run(RELEASE);
  motor3.run(RELEASE);
  motor4.run(RELEASE);
  delay(1000);
  
  // Test 2: All motors backward
  Serial.println("All motors backward");
  motor1.run(BACKWARD);
  motor2.run(BACKWARD);
  motor3.run(BACKWARD);
  motor4.run(BACKWARD);
  delay(2000);
  
  // Stop all motors
  Serial.println("All motors stopped");
  motor1.run(RELEASE);
  motor2.run(RELEASE);
  motor3.run(RELEASE);
  motor4.run(RELEASE);
  delay(1000);
  
  // Test 3: Alternating pattern
  Serial.println("Alternating pattern");
  motor1.run(FORWARD);
  motor2.run(BACKWARD);
  motor3.run(FORWARD);
  motor4.run(BACKWARD);
  delay(2000);
  
  // Reverse pattern
  Serial.println("Reverse pattern");
  motor1.run(BACKWARD);
  motor2.run(FORWARD);
  motor3.run(BACKWARD);
  motor4.run(FORWARD);
  delay(2000);
  
  // Stop all motors
  Serial.println("All motors stopped");
  motor1.run(RELEASE);
  motor2.run(RELEASE);
  motor3.run(RELEASE);
  motor4.run(RELEASE);
  delay(2000);
}
