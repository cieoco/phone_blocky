#include "../src/ControlTiming.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  uint32_t last = 0;
  float dt = 0, integral = 0;
  unsigned steps = 0;
  // Inner and outer calls on every millisecond: integral/ramp time must still
  // account for exactly one second, not two or an invented 10 ms per call.
  for (uint32_t now = 0; now <= 1000; ++now) {
    for (int caller = 0; caller < 2; ++caller) {
      if (ControlTiming::takeStep(now, last, dt)) { integral += dt; ++steps; }
    }
  }
  assert(steps == 100 && std::fabs(integral - 1.0f) < 0.00001f);
  assert(!ControlTiming::takeStep(1009, last, dt));
  assert(ControlTiming::takeStep(1037, last, dt));
  assert(std::fabs(dt - 0.037f) < 0.000001f); // actual time after blocking work
  last = UINT32_MAX - 4;
  assert(!ControlTiming::takeStep(4, last, dt));
  assert(ControlTiming::takeStep(5, last, dt));
  assert(std::fabs(dt - 0.010f) < 0.000001f);

  ControlTiming::LoopCadence cadence;
  unsigned iterations = 0, serviceCalls = 0;
  for (uint32_t now = 0; now < 1000; ++now) {
    ++serviceCalls;
    if (cadence.ready(now)) { ++iterations; cadence.complete(now); }
  }
  assert(serviceCalls == 1000 && iterations == 100);
  cadence.complete(1500); // long Blockly body finished here
  for (uint32_t now = 1500; now < 1510; ++now) assert(!cadence.ready(now));
  assert(cadence.ready(1510));
  cadence.complete(UINT32_MAX - 4);
  assert(!cadence.ready(4) && cadence.ready(5));
  std::cout << "control timing: duplicate callers, 10ms cadence, elapsed time, long bodies and rollover PASS\n";
}
