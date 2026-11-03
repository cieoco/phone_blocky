#pragma once
#include "Gate.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// The competitor build uses the original main, web server and CommandProcessor.
// All command execution and radio state transitions belong to jsonTask.
namespace Student {
#if COMPETITION_COMPETITOR
void boot();
void attach(AsyncWebServer& server, AsyncWebSocket& socket);
void begin();
void step(bool serialMode);
void commandError();
bool managed();
bool paired();
uint8_t channel();
bool submit(const String& json, uint8_t source, uint32_t client = 0);
void disconnected(uint32_t client);
class MutationGuard {
  bool held_;
public:
  MutationGuard();
  ~MutationGuard();
  bool allowed() const;
};
#else
inline bool managed() { return false; }
inline bool paired() { return false; }
inline uint8_t channel() { return 1; }
class MutationGuard {
public:
  bool allowed() const { return true; }
};
#endif
}
