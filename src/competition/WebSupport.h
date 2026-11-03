#pragma once
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <functional>
#include "Runtime.h"

namespace Competition {
// Per-request bounded body, including a terminator. AsyncWebServer frees _tempObject.
class JsonEndpoint : public AsyncWebHandler {
  struct Body { size_t size, received; bool bad; };
  String path_; size_t max_;
  std::function<void(AsyncWebServerRequest*,const char*,size_t)> handler_;
public:
  JsonEndpoint(const char* path,size_t limit,std::function<void(AsyncWebServerRequest*,const char*,size_t)> fn):path_(path),max_(limit),handler_(fn) {}
  bool canHandle(AsyncWebServerRequest* r) override {
    if(r->url()!=path_ || !(r->method()&(HTTP_POST|HTTP_PUT))) return false;
    r->addInterestingHeader("ANY"); return true;
  }
  void handleBody(AsyncWebServerRequest* r,uint8_t* data,size_t len,size_t index,size_t total) override {
    if(total==0 || total>max_ || index>total || len>total-index) return;
    if(index==0 && !r->_tempObject) {
      r->_tempObject=calloc(1,sizeof(Body)+total+1);
      if(r->_tempObject) static_cast<Body*>(r->_tempObject)->size=total;
    }
    Body* b=static_cast<Body*>(r->_tempObject); if(!b) return;
    if(b->size!=total || b->received!=index) {b->bad=true;return;}
    memcpy(reinterpret_cast<char*>(b+1)+index,data,len); b->received+=len;
  }
  void handleRequest(AsyncWebServerRequest* r) override {
    Body* b=static_cast<Body*>(r->_tempObject);
    if(!b || b->bad || b->received!=b->size) {r->send(413,"application/json","{\"error\":\"invalid_or_oversize_body\"}");return;}
    handler_(r,reinterpret_cast<char*>(b+1),b->size);
  }
};
inline void errorResponse(AsyncWebServerRequest* r,int code,const char* error) {
  StaticJsonDocument<192> d; d["ok"]=false; d["error"]=error; String s; serializeJson(d,s); r->send(code,"application/json",s);
}
inline bool sameOrigin(AsyncWebServerRequest* r) {
  if(!r->hasHeader("Origin")) return true; // USB/CLI clients do not send Origin.
  return r->header("Origin")==String("http://")+r->host();
}
inline bool authenticate(AsyncWebServerRequest* r,const char* token) {
  bool valid=r->hasHeader("X-Competition-Key") && r->header("X-Competition-Key")==token;
  String cookies=r->header("Cookie");int from=0;
  while(from<int(cookies.length())) {
    int end=cookies.indexOf(';',from);if(end<0)end=cookies.length();
    String part=cookies.substring(from,end);part.trim();
    if(part==String("competition=")+token)valid=true;
    from=end+1;
  }
  if(!sameOrigin(r) || !valid) {
    errorResponse(r,403,"authentication_required"); return false;
  } return true;
}
class Snapshot {
  SemaphoreHandle_t mutex_=xSemaphoreCreateMutex(); String value_="{}";
public:
  void set(const String& s) {xSemaphoreTake(mutex_,portMAX_DELAY);value_=s;xSemaphoreGive(mutex_);}
  String get() {xSemaphoreTake(mutex_,portMAX_DELAY);String s=value_;xSemaphoreGive(mutex_);return s;}
};
struct WebJob { char json[1536]; };
class Jobs {
  QueueHandle_t q_=xQueueCreate(8,sizeof(WebJob));
public:
  bool push(const char* s,size_t n) {if(n>=sizeof(WebJob::json)) return false; WebJob j={};memcpy(j.json,s,n);return xQueueSend(q_,&j,0)==pdTRUE;}
  bool pop(WebJob& j) {return xQueueReceive(q_,&j,0)==pdTRUE;}
};
inline void staticFiles(AsyncWebServer& server,const char* root) {
  server.on("/",HTTP_GET,[root](AsyncWebServerRequest* r){r->send(LittleFS,root,"text/html");});
  server.serveStatic("/",LittleFS,"/").setCacheControl("no-cache");
}
}
