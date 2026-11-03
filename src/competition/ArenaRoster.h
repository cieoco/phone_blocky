#pragma once
#include "Core.h"
#include <string.h>
namespace Competition {
constexpr size_t ArenaCount=2, MaxConcurrentPlayers=4;
// Reservations last through terminal states until acknowledged Release.
struct ArenaRoster {
  uint8_t macs[ArenaCount][MaxPlayers][6]={};
  size_t counts[ArenaCount]={};
  int owner(const uint8_t* mac)const {
    for(size_t a=0;a<ArenaCount;++a)for(size_t p=0;p<counts[a];++p)
      if(!memcmp(macs[a][p],mac,6))return int(a);
    return -1;
  }
  const char* check(size_t arena,const uint8_t selected[][6],size_t count)const {
    if(arena>=ArenaCount || counts[arena] || !count || count>MaxPlayers)return "invalid_config";
    size_t total=count;for(size_t a=0;a<ArenaCount;++a)total+=counts[a];
    for(size_t p=0;p<count;++p) {
      if(owner(selected[p])>=0)return "team_in_other_arena";
      for(size_t q=0;q<p;++q)if(!memcmp(selected[p],selected[q],6))return "duplicate_team";
    }
    return total>MaxConcurrentPlayers?"participant_capacity":"ok";
  }
  bool reserve(size_t arena,const uint8_t selected[][6],size_t count) {
    if(strcmp(check(arena,selected,count),"ok"))return false;
    memcpy(macs[arena],selected,count*6);counts[arena]=count;return true;
  }
  void release(size_t arena){if(arena<ArenaCount)counts[arena]=0;}
};
}
