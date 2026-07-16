/*
 * Serial Motor Control Example
 * 
 * This example demonstrates control of DC motors via serial commands.
 * Commands format: M<motor_number>,<direction>,<speed>
 * Example: M1,F,200 (Motor 1, Forward, Speed 200)
 * 
 * Commands:
 * - M1,F,<speed> - Motor 1 forward with specified speed (0-255)
 * - M1,B,<speed> - Motor 1 backward with specified speed (0-255)
 * - M1,S,0 - Motor 1 stop
 * - Similar for M2, M3, M4
 * 
 * Hardware connections:
 * - Connect your motor shield to the ESP32 according to the pin definitions in AFMotor.h
 * - Connect DC motors to motor ports on the shield
 * 
 * Created: 2025
 * Modified for ESP32
 */

#include <AFMotor.h>

// Create motor objects
AF_DCMotor motor1(1);
AF_DCMotor motor2(2);
AF_DCMotor motor3(3);
AF_DCMotor motor4(4);

void setup() {
  Serial.begin(115200);
  Serial.println("AFMotor ESP32 Serial Control");
  Serial.println("Commands: M<motor>,<direction>,<speed>");
  Serial.println("Example: M1,F,200 (Motor 1, Forward, Speed 200)");
  Serial.println("Directions: F=Forward, B=Backward, S=Stop");
  Serial.println("Motors: 1-4, Speed: 0-255");
  Serial.println("Ready for commands...");
  
  // Initialize all motors to stopped state
  motor1.run(RELEASE);
  motor2.run(RELEASE);
  motor3.run(RELEASE);
  motor4.run(RELEASE);
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      processCommand(command);
    }
  }
}

void processCommand(String cmd) {
  // Parse command format: M<motor>,<direction>,<speed>
  if (cmd.startsWith("M") && cmd.length() >= 5) {
    int motor = cmd.substring(1, 2).toInt();
    char direction = cmd.charAt(3);
    int speed = cmd.substring(5).toInt();
    
    if (motor >= 1 && motor <= 4) {
      AF_DCMotor* currentMotor = getMotor(motor);
      
      if (currentMotor != nullptr) {
        // Set speed first
        if (speed >= 0 && speed <= 255) {
          currentMotor->setSpeed(speed);
        }
        
        // Set direction
        switch (direction) {
          case 'F':
          case 'f':
            currentMotor->run(FORWARD);
            Serial.print("Motor ");
            Serial.print(motor);
            Serial.print(" forward, speed ");
            Serial.println(speed);
            break;
            
          case 'B':
          case 'b':
            currentMotor->run(BACKWARD);
            Serial.print("Motor ");
            Serial.print(motor);
            Serial.print(" backward, speed ");
            Serial.println(speed);
            break;
            
          case 'S':
          case 's':
            currentMotor->run(RELEASE);
            Serial.print("Motor ");
            Serial.print(motor);
            Serial.println(" stopped");
            break;
            
          default:
            Serial.println("Invalid direction. Use F, B, or S");
            break;
        }
      }
    } else {
      Serial.println("Invalid motor number. Use 1-4");
    }
  } else {
    Serial.println("Invalid command format. Use: M<motor>,<direction>,<speed>");
    Serial.println("Example: M1,F,200");
  }
}

AF_DCMotor* getMotor(int motorNum) {
  switch (motorNum) {
    case 1: return &motor1;
    case 2: return &motor2;
    case 3: return &motor3;
    case 4: return &motor4;
    default: return nullptr;
  }
}
