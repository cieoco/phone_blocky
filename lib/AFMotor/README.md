# AFMotor ESP32 Library

A modified version of the Adafruit Motor Shield library specifically designed for ESP32 microcontrollers.

## Features

- Control up to 4 DC motors
- PWM speed control (0-255)
- Forward, backward, and stop control
- Configurable PWM frequency and resolution
- ESP32 optimized pin assignments

## Hardware Requirements

- ESP32 development board
- Adafruit Motor Shield (or compatible)
- DC motors

## Pin Connections

The library uses the following ESP32 pins:

### Motor Enable Pins (PWM):
- Motor 1: GPIO 23
- Motor 2: GPIO 25
- Motor 3: GPIO 27
- Motor 4: GPIO 16

### Shift Register Control:
- MOTORLATCH: GPIO 19
- MOTORCLK: GPIO 17
- MOTORENABLE: GPIO 14
- MOTORDATA: GPIO 12

## Installation

1. Copy the `AFMotor` folder to your Arduino libraries directory
2. Restart Arduino IDE
3. Include the library in your sketch: `#include <AFMotor.h>`

## Usage

### Basic Example

```cpp
#include <AFMotor.h>

AF_DCMotor motor1(1);  // Create motor on port 1

void setup() {
  motor1.setSpeed(150);  // Set speed (0-255)
}

void loop() {
  motor1.run(FORWARD);   // Run forward
  delay(2000);
  motor1.run(BACKWARD);  // Run backward
  delay(2000);
  motor1.run(RELEASE);   // Stop
  delay(1000);
}
```

### Advanced Configuration

```cpp
// Motor with custom PWM frequency and resolution
AF_DCMotor motor1(1, 5000, 10);  // Motor 1, 5kHz, 10-bit resolution
```

## API Reference

### AF_DCMotor Class

#### Constructor
- `AF_DCMotor(uint8_t motornum)` - Create motor object with default settings
- `AF_DCMotor(uint8_t motornum, uint16_t freq, uint8_t resol)` - Create motor with custom PWM settings

#### Methods
- `void run(uint8_t cmd)` - Control motor direction
  - `FORWARD` - Run forward
  - `BACKWARD` - Run backward
  - `RELEASE` - Stop motor
- `void setSpeed(uint8_t speed)` - Set motor speed (0-255)

## Examples

The library includes several example sketches:

1. **BasicDCMotor** - Simple forward/backward motor control
2. **MultipleMotors** - Control multiple motors simultaneously
3. **SerialControl** - Control motors via serial commands

## Serial Control Commands

When using the SerialControl example, use these command formats:

- `M1,F,200` - Motor 1 forward at speed 200
- `M2,B,150` - Motor 2 backward at speed 150
- `M3,S,0` - Motor 3 stop

## Troubleshooting

### Motor doesn't run
- Check power supply to motor shield
- Verify pin connections
- Ensure motor is properly connected to shield

### Erratic motor behavior
- Check for loose connections
- Verify adequate power supply
- Ensure proper grounding

## License

This library is based on the original Adafruit Motor Shield library and is modified for ESP32 compatibility.

## Contributing

Feel free to submit issues and pull requests to improve this library.

## Changelog

### v1.0.0
- Initial ESP32 port
- Added PWM frequency and resolution configuration
- Updated pin assignments for ESP32
- Added comprehensive examples
