#pragma once
#include "Core.h"
namespace Competition {
// Sound ownership is independent of match/operator ownership and never drives
// the match state machine. Expiry stays silent until an explicit local takeover.
struct AudioRoute {
  static constexpr Time TimeoutMs=5000;
  uint64_t holder=0;
  Time until=0;
  bool local=false;
  bool central(Time now)const{return holder && now<until;}
  bool heartbeat(uint64_t id,Time now) {
    if(!id || (central(now) && holder!=id))return false;
    holder=id;until=now+TimeoutMs;local=false;return true;
  }
  bool release(uint64_t id) {
    if(!id || holder!=id)return false;
    holder=0;until=0;return true;
  }
  bool takeLocal(Time now) {
    if(central(now))return false;
    local=true;return true;
  }
  const char* name(Time now)const{return central(now)?"CENTRAL":local?"LOCAL":"WAITING";}
};
}
