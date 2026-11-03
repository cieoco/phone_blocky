#pragma once
#include "Core.h"
namespace Competition {
constexpr Time PairingWindowMs=60000, DiscoveryFreshMs=5000;
struct PairingAdmission {
  Time deadline=0;
  uint64_t ticket=0;
  bool open(Time now,uint64_t nonce,bool committed) {
    if(committed || !nonce)return false;
    ticket=nonce;deadline=now+PairingWindowMs;return true;
  }
  bool window(Time now)const{return ticket && now<deadline;}
  bool accepts(Time now,bool paired,bool managed,bool committed,uint64_t offered)const {
    return !committed && ticket && offered==ticket && ((!paired && !managed) || window(now));
  }
  void consume(){ticket=0;deadline=0;}
  bool claim(Time now,bool paired,bool managed,bool committed,uint64_t offered) {
    if(!accepts(now,paired,managed,committed,offered))return false;
    consume();return true;
  }
};
// A partial NVS/peer failure must never execute the final durable unlock.
template<class Save,class Install,class Unlock>
const char* commitPairing(Save save,Install install,Unlock unlock) {
  if(!save())return "pairing_write_failed";
  if(!install())return "peer_add_failed";
  if(!unlock())return "ownership_write_failed";
  return nullptr;
}
}
