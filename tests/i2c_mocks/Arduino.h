#pragma once
#include <cstdint>
#include <string>
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
class String : public std::string {
public:
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  String substring(unsigned start, unsigned end) const { return substr(start, end - start); }
};
