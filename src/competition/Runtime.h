#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <atomic>
#include "Protocol.h"

namespace Competition {
inline Time nowMs() { return uint64_t(esp_timer_get_time()) / 1000; }
inline uint64_t randomId() { return (uint64_t(esp_random()) << 32) | esp_random() | 1; }
inline String hexId(uint64_t n) { char b[17]; snprintf(b,sizeof(b),"%016llx",(unsigned long long)n); return String(b); }
inline bool parseId(const String& s, uint64_t& v) {
  if(s.length()!=16) return false; v=0;
  for(unsigned i=0;i<16;++i) { char c=s[i]; int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1; if(n<0) return false; v=(v<<4)|n; } return true;
}
inline String macString(const uint8_t* mac) { char s[18]; snprintf(s,sizeof(s),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]); return String(s); }
inline bool parseMac(const String& s, uint8_t* mac) {
  if(s.length()!=17) return false;
  unsigned a[6]; int used=0;
  if(sscanf(s.c_str(),"%2x:%2x:%2x:%2x:%2x:%2x%n",a,a+1,a+2,a+3,a+4,a+5,&used)!=6 || used!=17) return false;
  for(int i=0;i<6;++i) mac[i]=uint8_t(a[i]);
  return !(mac[0]&1) && memcmp(mac,"\0\0\0\0\0\0",6)!=0;
}
inline bool parseKey(const String& s, uint8_t* key) {
  if(s.length()!=32) return false;
  for(int i=0;i<16;++i) { String part=s.substring(i*2,i*2+2); char* end=nullptr; long n=strtol(part.c_str(),&end,16); if(!end || *end || end==part.c_str() || n<0 || n>255) return false; key[i]=uint8_t(n); }
  return true;
}
struct Team { uint8_t mac[6]={}, key[16]={}; char name[49]={}; };
struct Settings {
  static constexpr size_t MaxTeams=64;
  Team teams[MaxTeams]; uint8_t count=0, channel=1;
  char secret[33]={}; uint8_t pmk[16]={};
  bool load() {
    Preferences p;
    if(!p.begin("competition",false)) return false;
    channel=p.getUChar("channel",1); if(channel<1||channel>11) channel=1;
    count=p.getUChar("count",0);
    if(count>MaxTeams || p.getBytesLength("teams")!=sizeof(Team)*count) count=0;
    if(count) p.getBytes("teams",teams,sizeof(Team)*count);
    String token=p.getString("secret","");
    if(token.length()!=32) { token=hexId(randomId())+hexId(randomId()); if(p.putString("secret",token)!=32) {p.end();return false;} }
    token.toCharArray(secret,sizeof(secret));
    if(p.getBytesLength("pmk")==16) p.getBytes("pmk",pmk,16);
    else { esp_fill_random(pmk,16); if(p.putBytes("pmk",pmk,16)!=16) {p.end();return false;} }
    p.end(); return true;
  }
  int index(const uint8_t* mac) const { for(int i=0;i<count;++i) if(!memcmp(mac,teams[i].mac,6)) return i; return -1; }
  bool saveTeam(const Team& team, bool student=false) {
    int i=student?0:index(team.mac); if(i<0) i=count;
    if(i>=int(MaxTeams)) return false;
    Team old=teams[i]; uint8_t oldCount=count; teams[i]=team; count=student?1:std::max<int>(count,i+1);
    Preferences p; bool ok=p.begin("competition",false);
    if(ok) ok=p.putBytes("teams",teams,sizeof(Team)*count)==sizeof(Team)*count && p.putUChar("count",count)==1;
    p.end(); if(!ok) {teams[i]=old;count=oldCount;} return ok;
  }
  bool setChannel(uint8_t ch) { if(ch<1||ch>11) return false; Preferences p; if(!p.begin("competition",false)) return false; bool ok=p.putUChar("channel",ch)==1; p.end(); if(ok) channel=ch; return ok; }
  bool forgetTeams() {
    Preferences p;if(!p.begin("competition",false))return false;
    bool ok=p.putUChar("count",0)==1 && p.getUChar("count",255)==0;p.end();
    if(ok)count=0;return ok;
  }
};

struct Received { uint8_t mac[6]; uint16_t length; Time received; uint8_t bytes[MaxFrame]; };
class Radio {
  QueueHandle_t queue_=nullptr;
  std::atomic<bool> busy_{false}, overflow_{false};
  Time sentAt_=0;
  static Radio*& instance() { static Radio* r=nullptr; return r; }
  static void receive(const uint8_t* mac,const uint8_t* data,int len) {
    auto* r=instance(); if(!r || len<64 || len>int(MaxFrame)) return;
    Received m; memcpy(m.mac,mac,6); memcpy(m.bytes,data,len); m.length=len; m.received=nowMs();
    if(xQueueSend(r->queue_,&m,0)!=pdTRUE) r->overflow_=true;
  }
  static void sent(const uint8_t*,esp_now_send_status_t) { if(instance()) instance()->busy_=false; }
public:
  static const uint8_t* broadcastMac(){static const uint8_t mac[6]={255,255,255,255,255,255};return mac;}
  bool addBroadcast() {
    esp_now_peer_info_t peer={};memcpy(peer.peer_addr,broadcastMac(),6);peer.ifidx=WIFI_IF_STA;
    return esp_now_is_peer_exist(peer.peer_addr) || esp_now_add_peer(&peer)==ESP_OK;
  }
  bool begin(const uint8_t* pmk) {
    queue_=xQueueCreate(20,sizeof(Received)); if(!queue_) return false; instance()=this;
    return esp_now_init()==ESP_OK && esp_now_set_pmk(pmk)==ESP_OK && esp_now_register_recv_cb(receive)==ESP_OK && esp_now_register_send_cb(sent)==ESP_OK;
  }
  bool add(const Team& t,uint8_t channel) {
    esp_now_peer_info_t peer={}; memcpy(peer.peer_addr,t.mac,6); memcpy(peer.lmk,t.key,16);
    peer.channel=channel; peer.ifidx=WIFI_IF_STA; peer.encrypt=true;
    return esp_now_is_peer_exist(t.mac) || esp_now_add_peer(&peer)==ESP_OK;
  }
  void remove(const uint8_t* mac) { esp_now_del_peer(mac); }
  bool send(const uint8_t* mac,const Packet& p) {
    if(busy_.load()) return false;
    uint8_t bytes[MaxFrame]; size_t len=encode(p,bytes); if(!len) return false;
    busy_=true; sentAt_=nowMs();
    if(esp_now_send(mac,bytes,len)!=ESP_OK) {busy_=false;return false;} return true;
  }
  bool receive(Received& r,Packet& p) {
    while(xQueueReceive(queue_,&r,0)==pdTRUE) if(decode(r.bytes,r.length,p)) return true;
    return false;
  }
  bool failed() { return overflow_.load() || (busy_.load() && nowMs()-sentAt_>250); }
};
inline void beginWifi(const char* prefix,Settings& settings) {
  WiFi.persistent(false); WiFi.mode(WIFI_AP_STA); WiFi.setAutoReconnect(false); WiFi.disconnect(false,false);
  String name=String(prefix)+WiFi.macAddress().substring(12); name.replace(":","");
  // A unique generated password; revealed only by the explicit local USB info command.
  WiFi.softAP(name.c_str(),settings.secret,settings.channel,0,4);
  esp_wifi_set_ps(WIFI_PS_NONE);
}
}
