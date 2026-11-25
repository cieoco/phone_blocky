#pragma once
#include <stdint.h>

// Owned by the interpreter task. STOP arms only the match advertised in that word.
struct KitPhaseLease {
  enum Result { Ignore, Renew, Change };
  uint16_t word = 0, lastMatch = 0;
  uint32_t lastBeat = 0;
  bool armed = false, seenMatch = false;

  uint8_t phase() const { return word >> 14; }
  bool expire(uint32_t now) {
    if (!phase() || now - lastBeat < 300) return false;
    word = 0;
    armed = false;
    return true;
  }
  Result accept(uint16_t incoming, uint32_t received, uint32_t now) {
    if (now - received >= 300) return Ignore;
    const unsigned next = incoming >> 14;
    const uint16_t id = incoming & 0x3fff;
    if (incoming != word && next) {
      const bool start = next == 1 && armed && id == (word & 0x3fff) &&
                         (!seenMatch || id != lastMatch);
      const bool advance = phase() != 0 && next == unsigned(phase()) + 1 && id == lastMatch;
      if (!start && !advance) return Ignore;
      if (start) { seenMatch = true; lastMatch = id; }
    }
    const Result result = incoming == word ? Renew : Change;
    word = incoming;
    armed = next == 0;
    lastBeat = received;
    return result;
  }
};
