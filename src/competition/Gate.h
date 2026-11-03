#pragma once
#ifndef COMPETITION_COMPETITOR
#define COMPETITION_COMPETITOR 0
#endif
#if COMPETITION_COMPETITOR && defined(I2C_SLAVE_MODE) && I2C_SLAVE_MODE
#error Competition student role cannot enable I2C slave control
#endif
namespace CompetitionGate {
  using Check=bool(*)();
  inline Check& checker() { static Check fn=nullptr; return fn; }
  inline bool allowed() {
#if COMPETITION_COMPETITOR
    return checker() && checker()();
#else
    return true;
#endif
  }
}
