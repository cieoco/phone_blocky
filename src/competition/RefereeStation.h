#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>
#include "StationPolicy.h"
#if __has_include("referee_wifi.local.h")
#include "referee_wifi.local.h"
#else
#include "referee_wifi.example.h"
#endif

namespace Competition {
class RefereeStation {
  std::atomic<uint32_t> disconnectEpoch_{0};
  uint32_t retryAt_=0;
  bool wasConnected_=false;
  uint8_t reportedChannel_=0;
  IPAddress reportedIp_;
  String apName_,suffix_;
  const char* apPassword_=nullptr;
  uint8_t initialChannel_=1;
  void updateAccessPoint(const char* staIp) {
    char name[33];refereeAPName(name,sizeof(name),suffix_.c_str(),staIp);
    if(apName_==name)return;
    uint8_t ch=connected()?channel():reportedChannel_?reportedChannel_:initialChannel_;
    if(WiFi.softAP(name,apPassword_,ch,0,4)) {
      apName_=name;
      Serial.printf("[referee] AP name=%s url=http://%s/\n",name,WiFi.softAPIP().toString().c_str());
    } else Serial.println("[referee] AP start/update failed.");
  }
public:
  bool configured() const {return strlen(REFEREE_STA_SSID)>0;}
  bool connected() const {return WiFi.status()==WL_CONNECTED;}
  uint8_t channel() const {return connected()?WiFi.channel():0;}
  bool ready() const {return StationTicket::usable(connected(),channel());}
  StationTicket ticket() const {StationTicket t;t.epoch=disconnectEpoch_.load();t.channel=channel();return t;}
  bool matches(const StationTicket& t) const {return t.matches(connected(),channel(),disconnectEpoch_.load());}
  const char* ssid() const {return REFEREE_STA_SSID;}
  const String& apName() const {return apName_;}
  const char* status() const {
    return !configured()?"wifi_not_configured":!connected()?"wifi_disconnected":!ready()?"unsupported_channel":"connected";
  }
  void begin(const char* apPassword,uint8_t initialChannel) {
    apPassword_=apPassword;initialChannel_=initialChannel;
    WiFi.persistent(false);WiFi.mode(WIFI_AP_STA);WiFi.setAutoReconnect(false);
    suffix_=WiFi.macAddress();suffix_.replace(":","");suffix_=suffix_.substring(suffix_.length()-4);
    updateAccessPoint("");
    WiFi.onEvent([this](WiFiEvent_t event,WiFiEventInfo_t){
      if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)++disconnectEpoch_;
    });
    if(configured()){WiFi.begin(REFEREE_STA_SSID,REFEREE_STA_PASSWORD);retryAt_=millis()+15000;}
    else Serial.println("[referee] STA credentials missing: create include/referee_wifi.local.h and rebuild.");
    esp_wifi_set_ps(WIFI_PS_NONE);
  }
  void tick() {
    if(connected()) {
      if(!wasConnected_ || reportedChannel_!=channel() || reportedIp_!=WiFi.localIP()) {
        reportedChannel_=channel();
        reportedIp_=WiFi.localIP();updateAccessPoint(reportedIp_.toString().c_str());
        Serial.printf("[referee] STA connected: http://%s/ channel=%u mac=%s\n",WiFi.localIP().toString().c_str(),reportedChannel_,WiFi.macAddress().c_str());
      }
      wasConnected_=true;retryAt_=millis()+15000;return;
    }
    if(wasConnected_){Serial.println("[referee] STA disconnected; active match cannot resume automatically.");wasConnected_=false;updateAccessPoint("");}
    if(configured() && int32_t(millis()-retryAt_)>=0) {
      retryAt_=millis()+15000;WiFi.reconnect();
    }
  }
};
}
