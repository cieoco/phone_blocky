#pragma once
#include <stdint.h>
#include <stdio.h>
namespace Competition {
inline void refereeAPName(char* out,size_t capacity,const char* suffix,const char* staIp) {
  // Four MAC hex digits + IPv4 stay well within the 32-byte Wi-Fi SSID limit.
  snprintf(out,capacity,"%.4s-%.15s",suffix,staIp && *staIp?staIp:"STA-offline");
}
// A match binds to one STA connection epoch and one channel. Reconnecting on
// the same channel is still a new connection and must not resume the match.
struct StationTicket {
  uint32_t epoch=0;
  uint8_t channel=0;
  static bool usable(bool connected,uint8_t current) { return connected && current>=1 && current<=11; }
  bool matches(bool connected,uint8_t current,uint32_t connectionEpoch) const {
    return usable(connected,current) && current==channel && connectionEpoch==epoch;
  }
};
}
