#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

struct MotorCap {
    bool has_encoder;
};

// M1=index 0, M2=index 1, M3=index 2, M4=index 3
static const MotorCap MOTOR_CAPS[4] = {
    {false},  // M1: no encoder
    {false},  // M2: no encoder
    {true},   // M3: encoder pins 35/34
    {true},   // M4: encoder pins 36/39
};

static const int NUM_MOTORS = 4;
static const int NUM_SERVOS = 2;

#endif
