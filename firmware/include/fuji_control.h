#pragma once
#include <stdint.h>

namespace fuji {
constexpr uint16_t FULL = 512; // Timer5 period counts; explicit GPIO for 0/512.
constexpr uint16_t ROTARY_MASK = (1u<<9)-1;
constexpr uint32_t SETTLE_MS = 500, DWELL_MS = 3000, BLANK_MS = 2;
constexpr uint32_t TRAVEL_TIMEOUT_MS = 120000, BOOT_STABLE_MS = 500;
enum class State : uint8_t { Off, BiasWait, Idle, Run, Settle, Dwell, Deadtime, BiasReverse, Brake };
enum class Fault : uint8_t { None, Hardware, Driver, Power, Timeout, Watchdog, Startup };
enum class BoardRevision : uint8_t { R1, R2 };
struct Params {
  uint16_t minimum = 0, speed = 0, rampMs = 1000, dwellMs = DWELL_MS, brakeMs = 0;
};
inline uint16_t normalized(uint16_t adc) {
  if (adc <= 8) return 0;
  if (adc >= 1015) return FULL;
  return uint32_t(adc - 8) * FULL / 1007;
}
inline uint16_t target(const Params &p) {
  return p.minimum + uint32_t(FULL - p.minimum) * p.speed / FULL;
}

struct Tuning {
  uint16_t lightPermille=300, dwellMin=200, dwellMax=60000, rampMin=200, rampMax=3000;
  uint16_t runoutMm=0, brakeMaxMs=0, reverseRotaries=0;
  uint16_t rotaryValue(uint8_t channel, uint16_t calibrated) const {
    return (reverseRotaries & (1u<<channel)) ? FULL-calibrated : calibrated;
  }
  bool valid() const {
    return lightPermille<=1000 && dwellMin<=dwellMax && dwellMax<=60000 &&
           rampMin>=1 && rampMin<=rampMax && rampMax<=60000 &&
           runoutMm<=9999 && brakeMaxMs<=60000 && reverseRotaries<=ROTARY_MASK;
  }
};

// Inputs have already been mapped to 0..FULL by the rotary calibration.
inline void applyR2Rotaries(Params &p, uint16_t speed, uint16_t time, uint16_t lighting,
                            const Tuning &tuning = Tuning()) {
  const uint16_t lightingLimit = uint32_t(FULL) * tuning.lightPermille / 1000;
  p.speed = speed;
  p.minimum = uint32_t(lighting) * lightingLimit / FULL;
  p.rampMs = tuning.rampMin + uint32_t(time) * (tuning.rampMax - tuning.rampMin) / FULL;
  p.dwellMs = tuning.dwellMin + uint32_t(time) * (tuning.dwellMax - tuning.dwellMin) / FULL;
  p.brakeMs = uint32_t(time) * tuning.brakeMaxMs / FULL;
}

// Press is accepted only after a stable release while output is permitted.
class Button {
  bool raw_ = false, stable_ = false, armed_ = false;
  uint32_t since_ = 0;
public:
  bool update(bool pressed, bool permitted, uint32_t now) {
    if (pressed != raw_) { raw_ = pressed; since_ = now; }
    if (!permitted) armed_ = false;
    if (uint32_t(now - since_) < 25) return false;
    stable_ = raw_;
    if (!stable_ && permitted) armed_ = true;
    if (stable_ && permitted && armed_) { armed_ = false; return true; }
    return false;
  }
};

class Route {
  uint32_t at_ = 0, tick_ = 0, travel_ = 0, fraction_ = 0;
  uint16_t stationDwellMs_ = 0;
  uint16_t brakeFrom_ = 0, brakeDurationMs_ = 0;
  bool timing_ = false, departureDirectionSet_ = false;
public:
  State state = State::Off;
  bool right = true;
  uint16_t duty = 0;
  bool timeout = false;
  void off(uint32_t now) {
    // If stopped at the endpoint, next manual start must depart from it.
    const bool atEndpoint = state == State::Brake || state == State::Settle ||
                            state == State::Dwell || state == State::Deadtime;
    if (atEndpoint && !departureDirectionSet_) right = !right;
    state = State::Off; duty = 0; at_ = tick_ = now; timing_ = false; fraction_ = 0;
    departureDirectionSet_ = false;
  }
  bool automatic() const { return state >= State::Run; }
  void update(uint32_t now, bool permit, bool pressed, bool leftEvent, bool rightEvent,
              const Params &p, BoardRevision revision) {
    const bool r2 = revision == BoardRevision::R2;
    uint32_t dt = now - tick_; tick_ = now;
    if (!permit) { off(now); return; }
    if (state == State::Off) { state = State::BiasWait; at_ = now; duty = 0; }
    if (state == State::BiasWait) {
      if (uint32_t(now-at_) >= BLANK_MS) { state = State::Idle; duty = p.minimum; }
      return;
    }
    const bool arrived = right ? rightEvent : leftEvent;
    if (r2 && pressed && automatic()) {
      if (state == State::Run && arrived) state = State::Settle;
      off(now);
      state = State::BiasWait;
      return;
    }
    if (state == State::Idle) {
      duty = p.minimum;
      if (pressed) { state = State::Run; timing_ = false; fraction_ = 0; }
    }
    if (state == State::Run) {
      if (arrived) {
        departureDirectionSet_ = false;
        at_ = now;
        timing_ = false;
        if (r2 && p.brakeMs && duty > p.minimum) {
          brakeFrom_ = duty;
          brakeDurationMs_ = p.brakeMs;
          state = State::Brake;
        } else {
          duty = r2 ? p.minimum : 0;
          state = State::Settle;
        }
        return;
      }
      const uint16_t goal = target(p);
      if (goal > p.minimum) {
        if (!timing_) { timing_ = true; travel_ = now; }
        if (uint32_t(now-travel_) >= TRAVEL_TIMEOUT_MS) { timeout = true; duty = 0; return; }
      } else timing_ = false;
      // Ramp rate is full scale per rampMs; retain fractional steps at 1 ms ticks.
      if (dt > 100) dt = 100;
      fraction_ += dt * FULL;
      const uint16_t ramp = p.rampMs ? p.rampMs : 1;
      uint32_t step = fraction_ / ramp; fraction_ %= ramp;
      if (duty < goal) duty += step < uint16_t(goal-duty) ? step : goal-duty;
      else if (duty > goal) duty -= step < uint16_t(duty-goal) ? step : duty-goal;
    } else if (state == State::Brake) {
      const uint32_t elapsed = now - at_;
      if (elapsed >= brakeDurationMs_) {
        duty = p.minimum;
        state = State::Settle;
        at_ = now;
      } else {
        const uint16_t span = brakeFrom_ > p.minimum ? brakeFrom_ - p.minimum : 0;
        duty = p.minimum + span - uint32_t(span) * elapsed / brakeDurationMs_;
      }
    } else if (state == State::Settle) {
      duty = r2 ? p.minimum : 0;
      if (uint32_t(now-at_) >= SETTLE_MS) {
        stationDwellMs_ = p.dwellMs;
        state = State::Dwell; at_ = now; duty = p.minimum;
      }
    } else if (state == State::Dwell) {
      duty = p.minimum;
      const uint32_t elapsed = now-at_;
      if (r2) {
        if (!departureDirectionSet_ && elapsed >= (uint32_t(stationDwellMs_) + 1) / 2) {
          right = !right;
          departureDirectionSet_ = true;
        }
        if (elapsed >= stationDwellMs_) {
          state = State::Run;
          timing_ = false;
          fraction_ = 0;
          departureDirectionSet_ = false;
        }
      } else if (elapsed >= p.dwellMs) {
        state = State::Deadtime; at_ = now; duty = 0;
      }
    } else if (state == State::Deadtime) {
      duty = 0;
      if (uint32_t(now-at_) >= BLANK_MS) { right = !right; state = State::BiasReverse; at_ = now; }
    } else if (state == State::BiasReverse) {
      duty = 0;
      if (uint32_t(now-at_) >= BLANK_MS) {
        state = State::Run; duty = p.minimum; timing_ = false; fraction_ = 0;
      }
    }
  }
};

class Controller {
  const BoardRevision revision_;
  bool booted_ = false, stable_ = false, totalStable_ = false;
  uint32_t goodAt_ = 0, totalAt_ = 0, bootAt_ = 0;
  bool panelOff_ = false, resetStable_ = false, releaseFaultSink_ = false;
  uint32_t panelOffAt_ = 0, resetGoodAt_ = 0;
  uint8_t restartRoutes_ = 0;
  Button buttons_[3];
public:
  explicit Controller(BoardRevision revision = BoardRevision::R1) : revision_(revision) {}
  Route routes[3];
  Fault fault = Fault::None;
  uint8_t faultRoutes = 0;
  uint8_t buttonEvents = 0;
  bool ready = false, permit = false;
  void begin(uint32_t now, bool watchdogReset = false) { bootAt_ = now; if (watchdogReset) fail(Fault::Watchdog); }
  void fail(Fault why, uint8_t mask = 0) {
    if (fault == Fault::None) { fault = why; faultRoutes = mask; }
    ready = permit = false;
  }
  bool faultRequest() const { return fault != Fault::None && !releaseFaultSink_; }
  void servicePanelReset(uint32_t now, bool physicalOff, bool inputsHealthy) {
    if (physicalOff && !panelOff_) panelOffAt_ = now;
    panelOff_ = physicalOff;
    releaseFaultSink_ = panelOff_ && uint32_t(now-panelOffAt_) >= BOOT_STABLE_MS;
    if (!releaseFaultSink_ || !inputsHealthy || fault == Fault::None) {
      resetStable_ = false;
      return;
    }
    if (!resetStable_) {
      resetStable_ = true;
      resetGoodAt_ = now;
    }
    if (uint32_t(now-resetGoodAt_) < 100) return;
    fault = Fault::None;
    faultRoutes = 0;
    booted_ = stable_ = totalStable_ = false;
    ready = permit = false;
    bootAt_ = now;
    restartRoutes_ = 0b111;
    for (auto &route : routes) route.timeout = false;
  }
  void update(uint32_t now, bool powerGood, bool hardwareFault, uint8_t driverFaults,
              bool totalOn, bool adcReady, uint8_t presses, uint8_t events, const Params p[3],
              bool commonDriverFault = false) {
    const bool driverTrip = driverFaults || commonDriverFault;
    if (hardwareFault) fail(Fault::Hardware, driverFaults);
    if (booted_ && !powerGood) fail(Fault::Power);
    if (booted_ && driverTrip) fail(Fault::Driver, driverFaults);
    if (!booted_ && fault == Fault::None) {
      if (powerGood && !driverTrip && adcReady) {
        if (!stable_) { stable_ = true; goodAt_ = now; }
        if (uint32_t(now-goodAt_) >= BOOT_STABLE_MS) booted_ = true;
      } else stable_ = false;
      if (!booted_ && uint32_t(now-bootAt_) >= 5000) fail(Fault::Startup, driverFaults);
    }
    ready = booted_ && fault == Fault::None;
    if (!totalOn || !ready) totalStable_ = false;
    else if (!totalStable_) { totalStable_ = true; totalAt_ = now; }
    permit = ready && totalOn && totalStable_ && uint32_t(now-totalAt_) >= 25;
    buttonEvents = 0;
    for (uint8_t i=0; i<3; ++i) {
      const uint8_t mask = 1u<<i;
      const bool pressed = buttons_[i].update(presses & (1<<i), permit, now);
      if (pressed) {
        buttonEvents |= mask;
        restartRoutes_ &= ~mask;
      }
      Params routeParams = p[i];
      if (restartRoutes_ & mask) routeParams.minimum = 0;
      routes[i].update(now, permit, pressed, events & (1<<(2*i)), events & (2<<(2*i)), routeParams, revision_);
      if (routes[i].timeout) fail(Fault::Timeout, 1<<i);
    }
    if (fault != Fault::None) for (auto &r : routes) r.off(now);
  }
};
}
