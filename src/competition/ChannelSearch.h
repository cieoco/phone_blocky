#pragma once
#include "Core.h"
namespace Competition {
constexpr Time ChannelDwellMs=900, RefereeBeaconMs=500, RefereeSeenMs=3000;
// Only radio discovery state: never grants authority or clears a vehicle lease.
struct ChannelSearch {
  Time next=0,lastSeen=0;
  bool seen=false;
  void heard(Time now){seen=true;lastSeen=now;next=now+RefereeSeenMs;}
  bool fresh(Time now)const{return seen && now-lastSeen<RefereeSeenMs;}
  uint8_t advance(Time now,uint8_t current,bool permitted) {
    if(!permitted || fresh(now) || now<next)return 0;
    next=now+ChannelDwellMs;
    return current>=1 && current<11?current+1:1;
  }
};
}
