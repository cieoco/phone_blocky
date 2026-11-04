#pragma once
#include <stdint.h>
#include <stddef.h>
#include <algorithm>

namespace Competition {
using Time = uint64_t;
constexpr uint32_t LeaseMs = 300, RenewMs = 80, HealthMs = 250;
constexpr uint32_t GuardMs = 500, StartDelayMs = 1200, SyncMaxAgeMs = 3000;
constexpr size_t MaxPlayers = 4;
enum class Mode : uint8_t { Manual = 1, Auto = 2, Mixed = 3 };
enum class State : uint8_t { Idle, Preparing, Ready, Starting, Manual, Auto, Gap, Paused, Finished, Aborted, Fault };
inline const char* stateName(State s) {
  static const char* names[] = {"IDLE","PREPARING","READY","STARTING","RUN_MANUAL","RUN_AUTO","GAP","PAUSED","FINISHED","ABORTED","FAULT"};
  return unsigned(s) < 11 ? names[unsigned(s)] : "FAULT";
}
inline const char* modeName(Mode m) { return m == Mode::Manual ? "MANUAL" : m == Mode::Auto ? "AUTO" : "MIXED"; }
struct Config {
  Mode mode = Mode::Manual, first = Mode::Manual;
  uint32_t manualMs = 30000, autoMs = 30000, gapMs = 5000;
  bool valid() const {
    return (mode == Mode::Manual || mode == Mode::Auto || mode == Mode::Mixed) &&
      (first == Mode::Manual || first == Mode::Auto) && manualMs >= 1000 && manualMs <= 600000 &&
      autoMs >= 1000 && autoMs <= 600000 && gapMs >= 1000 && gapMs <= 60000;
  }
  Mode at(uint8_t index) const {
    if (mode != Mode::Mixed) return mode;
    return index == 0 ? first : (first == Mode::Manual ? Mode::Auto : Mode::Manual);
  }
  uint32_t duration(Mode m) const { return m == Mode::Manual ? manualMs : autoMs; }
};

// No Arduino dependencies: the firmware and host fault-injection tests use this exact engine.
class MatchEngine {
public:
  Config config;
  State state = State::Idle, pausedState = State::Idle;
  uint64_t match = 0;
  uint32_t generation = 0, version = 0, event = 0;
  uint8_t phase = 0;
  Time start = 0, end = 0, prepareDeadline = 0, remainingPaused = 0;
  bool guardPassed = false, resumed = false, timerOnly = false;
  const char* reason = "";
  const char* sound = "";
  Mode mode() const { return config.at(phase); }
  bool running() const { return state == State::Manual || state == State::Auto; }
  bool scheduling() const { return state == State::Starting || state == State::Gap; }
  bool terminal() const { return state == State::Finished || state == State::Aborted || state == State::Fault; }
  bool active() const { return state != State::Idle && !terminal(); }
  Time remaining(Time now) const {
    if (state == State::Paused) return remainingPaused;
    if (state == State::Gap || state == State::Starting) return start > now ? start - now : 0;
    return running() && end > now ? end - now : 0;
  }
  bool prepare(const Config& c, uint64_t id, Time now, bool localTimer = false) {
    if (state != State::Idle || !c.valid() || !id) return false;
    config = c; match = id; phase = 0; ++generation; reason = ""; sound = "";
    start = end = 0; guardPassed = resumed = false; prepareDeadline = now + 6000;
    timerOnly = localTimer;
    change(timerOnly ? State::Ready : State::Preparing); return true;
  }
  bool ready() { if (state != State::Preparing) return false; change(State::Ready); return true; }
  bool begin(Time now) {
    if (state != State::Ready) return false;
    schedule(now + StartDelayMs, config.duration(mode()), State::Starting); return true;
  }
  void tick(Time now, bool healthy, bool committed) {
    if (timerOnly) healthy = committed = true;
    if (state == State::Preparing && now >= prepareDeadline) { fault("prepare_timeout"); return; }
    if ((running() || scheduling() || state == State::Ready || state == State::Paused) && !healthy) {
      fault("participant_unhealthy"); return;
    }
    if (scheduling()) {
      if (!guardPassed && now + GuardMs >= start) {
        if (!committed) { fault("commit_timeout"); return; }
        guardPassed = true;
      }
      if (now >= start) {
        if (!committed || !guardPassed || now >= end) { fault("start_missed"); return; }
        change(mode() == Mode::Manual ? State::Manual : State::Auto);
        if (!resumed) cue(phase == 0 || mode() == Mode::Auto ? "start" : "resume");
        resumed = false;
      }
    }
    if (running() && now >= end) {
      ++generation;
      if (config.mode == Mode::Mixed && phase == 0) {
        ++phase; schedule(end + config.gapMs, config.duration(mode()), State::Gap); cue("end");
      } else { change(State::Finished); cue("end"); }
    }
  }
  bool pause(Time now) {
    if (!running() && state != State::Gap) return false;
    pausedState = state; remainingPaused = remaining(now);
    if (!remainingPaused) return false;
    ++generation; guardPassed = false; change(State::Paused); return true;
  }
  bool resume(Time now) {
    if (state != State::Paused) return false;
    ++generation; resumed = pausedState != State::Gap;
    if (pausedState == State::Gap) {
      // A separate prepare window preserves even a sub-second remaining GAP.
      schedule(now + StartDelayMs + remainingPaused, config.duration(mode()), State::Gap);
    } else schedule(now + StartDelayMs, remainingPaused, State::Starting);
    return true;
  }
  bool stop() {
    if (state == State::Idle) return false;
    if (terminal()) return true;
    ++generation; guardPassed = false; change(State::Aborted); cue("abort"); return true;
  }
  void fault(const char* why) {
    if (terminal() || state == State::Idle) return;
    reason = why; ++generation; guardPassed = false; change(State::Fault); cue("abort");
  }
  bool reset() {
    if (!terminal() && state != State::Ready && state != State::Preparing) return false;
    ++generation; match = 0; start = end = 0; guardPassed = false; reason = ""; change(State::Idle); return true;
  }
private:
  void change(State s) { state = s; ++version; }
  void cue(const char* s) { sound = s; ++event; }
  void schedule(Time at, Time duration, State s) { start = at; end = at + duration; guardPassed = false; change(s); }
};

// Local lease state. Authentication, address checks and decoding happen before these methods.
class Lease {
public:
  uint64_t controllerBoot = 0, match = 0;
  uint32_t generation = 0, sequence = 0;
  Mode mode = Mode::Manual;
  Time start = 0, end = 0, expiry = 0;
  bool prepared = false, committed = false, faulted = false, terminal = false, everLeased = false;
  bool prepare(uint64_t cb, uint64_t id, uint32_t gen, Mode m, uint32_t seq) {
    if (!cb || !id || !gen || (m != Mode::Manual && m != Mode::Auto)) return false;
    if (controllerBoot == cb && match == id) {
      if (terminal || faulted || gen < generation || seq <= sequence) return false;
      if (gen == generation) { sequence = seq; return prepared && mode == m; }
    }
    controllerBoot = cb; match = id; generation = gen; mode = m; sequence = seq;
    start = end = expiry = 0; prepared = true; committed = faulted = terminal = everLeased = false;
    return true;
  }
  bool commit(uint32_t gen, uint32_t seq, Time at, Time until, Time now) {
    if (!prepared || faulted || terminal || gen != generation || seq <= sequence || at <= now || until <= at || until - at > 600000) return false;
    if (committed && (start != at || end != until)) return false;
    sequence = seq; start = at; end = until; committed = true; return true;
  }
  bool renew(uint32_t gen, uint32_t seq, Time sent, Time until, Time now) {
    tick(now);
    if (!committed || faulted || terminal || gen != generation || seq <= sequence ||
        sent > now + 10 || until <= now || until < sent || until - sent > LeaseMs || until > end) return false;
    sequence = seq; expiry = std::max(expiry, until); everLeased = true; return true;
  }
  bool disable(uint32_t gen, uint32_t seq, bool final) {
    if (gen < generation || seq <= sequence) return false;
    generation = gen; sequence = seq; committed = prepared = everLeased = false; expiry = 0;
    terminal = terminal || final; return true;
  }
  void tick(Time now) {
    if (!committed) return;
    if (now >= end) { committed = false; expiry = 0; return; }
    if ((everLeased && now >= expiry) || (!everLeased && now >= start)) fail();
  }
  bool allowed(Time now) const { return committed && !faulted && !terminal && everLeased && now >= start && now < end && now < expiry; }
  void fail() { faulted = true; committed = false; expiry = 0; }
};
}
