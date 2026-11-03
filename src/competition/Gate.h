#pragma once
#include <stdint.h>
#ifndef COMPETITION_COMPETITOR
#define COMPETITION_COMPETITOR 0
#endif
#if COMPETITION_COMPETITOR && defined(I2C_SLAVE_MODE) && I2C_SLAVE_MODE
#error Competition student role cannot enable I2C slave control
#endif
namespace CompetitionGate {
  inline uint64_t& digitalOutputs() { static uint64_t pins=0; return pins; }
  inline void noteDigitalOutput(int pin) {
#if COMPETITION_COMPETITOR
    if(pin>=0 && pin<34)digitalOutputs()|=uint64_t(1)<<pin;
#endif
  }
  using Check=bool(*)();
  inline Check& checker() { static Check fn=nullptr; return fn; }
  inline Check& managedChecker() { static Check fn=nullptr; return fn; }
  inline bool managed() {
#if COMPETITION_COMPETITOR
    return !managedChecker() || managedChecker()();
#else
    return false;
#endif
  }
  inline bool allowed() {
#if COMPETITION_COMPETITOR
    return checker() && checker()();
#else
    return true;
#endif
  }
}
