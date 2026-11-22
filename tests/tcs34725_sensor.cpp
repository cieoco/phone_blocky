#include <cassert>
#include "Tcs34725Sensor.h"
#include "OledDisplay.h"
uint32_t testMillis = 0;
TwoWire Wire;

int main() {
  PeripheralI2cBus bus;
  Tcs34725Sensor sensor(bus);
  OledDisplay oled(bus);
  assert(oled.write(0x3c, 0, "RGB", false));
  assert(sensor.readChannel(0) == -1 && !sensor.failed());
  assert(Wire.begins == 1); // sensor cannot reinitialize OLED's Wire
  assert(Wire.registers[1] == 0xf6 && Wire.registers[15] == 0);
  testMillis = 2; assert(sensor.readChannel(0) == -1);
  testMillis = 3; assert(sensor.readChannel(0) == -1);
  assert(Wire.registers[0] == 3);
  testMillis = 26; assert(sensor.readChannel(0) == -1);
  testMillis = 27;
  assert(sensor.readChannel(0) == 0x5678);
  assert(sensor.readChannel(1) == 0x9abc);
  assert(sensor.readChannel(2) == 0xdef0);
  assert(sensor.readChannel(3) == 0x1234);
  assert(Wire.bursts == 1);
  testMillis = 50; sensor.readChannel(0); assert(Wire.bursts == 1);
  testMillis = 51; sensor.readChannel(0); assert(Wire.bursts == 2);
  Wire.connected = false; testMillis = 75;
  assert(sensor.readChannel(0) == -1 && sensor.failed());
  assert(oled.write(0x3c, 1, "still works", false));
  assert(Wire.begins == 1 && Wire.ends == 0);
  Wire.connected = true; testMillis = 574;
  assert(sensor.readChannel(0) == -1 && sensor.failed());
  testMillis = 575; assert(sensor.readChannel(0) == -1 && !sensor.failed());
  testMillis = 578; sensor.readChannel(0);
  testMillis = 602; assert(sensor.readChannel(3) == 0x1234);
  Wire.shortRead = true; testMillis += 24;
  assert(sensor.readChannel(3) == -1 && sensor.failed());
  Wire.shortRead = false;
  sensor.reset(); Wire.nackReg = 0x01;
  assert(sensor.readChannel(0) == -1 && sensor.failed());
  Wire.nackReg = -1; Wire.id = 0xff; sensor.reset();
  assert(sensor.readChannel(0) == -1 && sensor.failed());
  Wire.id = 0x4d; Wire.ready = false; sensor.reset();
  sensor.readChannel(0); testMillis += 3; sensor.readChannel(0); testMillis += 124;
  assert(sensor.readChannel(0) == -1 && sensor.failed());
  Wire.ready = true;
  // Ownership changes release the bus once; reentering initializes both drivers.
  sensor.reset(); oled.end(); assert(Wire.ends == 0);
  bus.end(); assert(Wire.ends == 1);
  testMillis = UINT32_MAX - 2;
  sensor.readChannel(0); testMillis += 3; sensor.readChannel(0); testMillis += 24;
  assert(sensor.readChannel(0) == 0x5678); // millis wraparound
  assert(oled.write(0x3c, 0, "again", false));
  assert(Wire.begins == 2);
  assert(sensor.setGain(4) && sensor.setCycles(256));
  assert(!sensor.setGain(2) && !sensor.setCycles(0));
  assert(sensor.readChannel(0) == -1);
  assert(Wire.registers[1] == 0 && Wire.registers[15] == 1);
  testMillis += 3; sensor.readChannel(0);
  testMillis += 614; assert(sensor.readChannel(0) == -1);
  ++testMillis; assert(sensor.readChannel(0) == 0x5678);
  sensor.setGain(4); sensor.setCycles(256);
  assert(sensor.readChannel(0) == 0x5678);
  sensor.defaults(); assert(sensor.readChannel(0) == -1);
  assert(Wire.registers[1] == 0xf6 && Wire.registers[15] == 0);
  uint16_t red[] = {550,250,200,1000}, scaled[] = {2200,1000,800,4000};
  assert(Tcs34725Math::color(red,10) == 1);
  assert(Tcs34725Math::estimate(red,10,1,false) == 3048);
  assert(Tcs34725Math::estimate(red,10,1,true) == 2776);
  assert(Tcs34725Math::estimate(scaled,10,4,false) == Tcs34725Math::estimate(red,10,1,false));
  uint16_t dark[] = {0,0,0,0}, clipped[] = {550,250,200,7680};
  assert(Tcs34725Math::estimate(dark,10,1,false) == 0);
  assert(Tcs34725Math::estimate(dark,10,1,true) == -1);
  assert(Tcs34725Math::color(dark,10) == 0);
  assert(Tcs34725Math::color(clipped,10) == -1);
  Wire.sample = {100,0,50,0,25,0,101,0};
  testMillis += 3; assert(sensor.rgb(0) == -1);
  testMillis += 24; assert(sensor.rgb(0) == 128 && sensor.rgb(1) == 64 && sensor.rgb(2) == 255);
  Wire.sample[0] = 0; testMillis += 24; assert(sensor.rgb(0) == 0);
  assert(sensor.rgb(3) == -1);
  assert(sensor.readChannel(-1) == -1 && sensor.readChannel(4) == -1);
}
