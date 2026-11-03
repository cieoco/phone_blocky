#pragma once
#include <stdint.h>

namespace ControlTiming {
constexpr uint32_t PeriodMs = 10;

// Shared by the outer loop and in-program control calls. Never invent elapsed
// time for duplicate calls; unsigned subtraction also handles millis rollover.
inline bool takeStep(uint32_t now, uint32_t& last, float& seconds) {
  const uint32_t elapsed = now - last;
  if (elapsed < PeriodMs) return false;
  last = now;
  seconds = elapsed / 1000.0f;
  return true;
}

// Like the teaching task's delay after each complete Blockly loop. A long body
// does not create a backlog of iterations to execute in a burst afterwards.
class LoopCadence {
  uint32_t completedAt_ = 0;
  bool completed_ = false;
public:
  bool ready(uint32_t now) const {
    return !completed_ || uint32_t(now - completedAt_) >= PeriodMs;
  }
  void complete(uint32_t now) { completedAt_ = now; completed_ = true; }
};
}
