#pragma once
#include <stdint.h>

namespace dashboard_lease {
enum State : uint8_t { Standalone, Stopped, Ready, Running };
enum Operation : uint8_t { Stop, Heartbeat, Release, Start };
constexpr uint32_t timeoutMs = 1200;

// Rows: requested state. Columns: Stop, Heartbeat, Release, Start.
// A delayed Release/Start is accepted only after the previous state was applied.
constexpr State transitions[4][4] = {
    {Stopped, Running, Standalone, Standalone},
    {Stopped, Stopped, Ready, Stopped},
    {Stopped, Ready, Ready, Running},
    {Stopped, Running, Running, Running},
};

inline State afterTimeout(State state, uint32_t elapsed) {
  return (state == Running || state == Ready) && elapsed >= timeoutMs ? Stopped : state;
}
inline State afterOperation(State requested, State applied, uint8_t op) {
  if (op > Start || (op >= Release && requested != applied)) return requested;
  return transitions[requested][op];
}
} // namespace dashboard_lease
