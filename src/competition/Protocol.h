#pragma once
#include "Core.h"
#include <string.h>
namespace Competition {
enum class Message : uint8_t { Ping = 1, Pong, Sync, Prepare, Ready, Commit, Renew, Disable, Status, View, Release };
constexpr size_t HeaderBytes = 64, MaxPayload = 128, MaxFrame = HeaderBytes + MaxPayload;
inline void put(uint8_t* p, uint64_t v, size_t n) { for (size_t i=0; i<n; ++i) p[i]=uint8_t(v >> (8*i)); }
inline uint64_t get(const uint8_t* p, size_t n) { uint64_t v=0; for(size_t i=0;i<n;++i) v |= uint64_t(p[i]) << (8*i); return v; }
struct Packet {
  Message type = Message::Ping;
  uint16_t length = 0;
  uint64_t controllerBoot = 0, match = 0, deviceBoot = 0;
  uint32_t generation = 0, sequence = 0;
  Time sent = 0, effective = 0, until = 0;
  uint8_t payload[MaxPayload] = {};
};
inline uint16_t payloadSize(Message t) {
  switch(t) {
    case Message::Ping: return 0;
    case Message::Pong: return 24; // echoed t1, t2(received), t3(sent)
    case Message::Sync: return 12; // signed controller-local offset, uncertainty
    case Message::Prepare: return 2; // phase mode, needs stored autonomous program
    case Message::Ready: return 9; // ready, program hash, error
    case Message::Commit: return 8; // phase end
    case Message::Renew: return 0;
    case Message::Disable: return 1; // terminal flag
    case Message::Status: return 16; // previous flags + referee ownership
    case Message::View: return 6; // state, phase mode, remaining milliseconds (display only)
    case Message::Release: return 0; // terminal match: explicitly return to teaching
  }
  return 65535;
}
inline size_t encode(const Packet& p, uint8_t* out) {
  if (p.length != payloadSize(p.type) || p.length > MaxPayload || !p.sequence) return 0;
  memset(out,0,HeaderBytes); out[0]='S'; out[1]='M'; out[2]=2; out[3]=uint8_t(p.type);
  put(out+4,p.length,2); put(out+8,p.controllerBoot,8); put(out+16,p.match,8); put(out+24,p.deviceBoot,8);
  put(out+32,p.generation,4); put(out+36,p.sequence,4); put(out+40,p.sent,8); put(out+48,p.effective,8); put(out+56,p.until,8);
  memcpy(out+64,p.payload,p.length); return 64+p.length;
}
inline bool decode(const uint8_t* in, size_t n, Packet& p) {
  if(n<64 || n>MaxFrame || in[0]!='S' || in[1]!='M' || in[2]!=2 || in[6] || in[7]) return false;
  p.type=Message(in[3]); p.length=uint16_t(get(in+4,2));
  if(p.length!=payloadSize(p.type) || p.length>MaxPayload || n!=HeaderBytes+p.length) return false;
  p.controllerBoot=get(in+8,8); p.match=get(in+16,8); p.deviceBoot=get(in+24,8);
  p.generation=uint32_t(get(in+32,4)); p.sequence=uint32_t(get(in+36,4));
  p.sent=get(in+40,8); p.effective=get(in+48,8); p.until=get(in+56,8);
  if(!p.sequence) return false;
  memcpy(p.payload,in+64,p.length); return true;
}
}
