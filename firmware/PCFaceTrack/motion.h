#pragma once
#include <stdint.h>
// Compile-time tests evaluate the same controller with a C++14 compiler.
#if __cplusplus >= 201402L
#define MOTION_FN constexpr
#else
#define MOTION_FN
#endif

// Pure controller, shared by firmware and host-side tests.
namespace Motion {
constexpr float PAN_MIN = 15, PAN_MAX = 165, PAN_HOME = 90;
constexpr float TILT_MIN = 65, TILT_MAX = 165, TILT_HOME = 90;
constexpr int PAN_DIRECTION = -1, TILT_DIRECTION = +1;
constexpr float DEAD_ZONE_PX = 10;
constexpr float CORRECTION_DEG_PER_PX = 0.20f;
constexpr float MAX_CORRECTION_DEG = 10;
constexpr float TRACK_SPEED = 100; // degrees/second, independent of inference rate
constexpr float SEARCH_SPEED = 14;
constexpr uint32_t LOST_DELAY_MS = 1200;
constexpr uint32_t FRESH_MS = 1000;

constexpr float bound(float x, float lo, float hi) {return x < lo ? lo : (x > hi ? hi : x);}
constexpr float approach(float x, float target, float step) {return x + bound(target-x, -step, step);}
constexpr float magnitude(float x) {return x < 0 ? -x : x;}
enum class State {Waiting, Paused, Tracking, Lost, Searching, CameraStale};
inline const char *name(State state) {
  switch (state) {
    case State::Paused: return "paused";
    case State::Tracking: return "tracking";
    case State::Lost: return "lost_wait";
    case State::Searching: return "searching";
    case State::CameraStale: return "camera_stale";
    default: return "waiting_for_camera";
  }
}
struct Observation {
  bool valid = false, found = false;
  float dx = 0, dy = 0;
  uint32_t capturedMs = 0;
};
struct Controller {
  float pan = PAN_HOME, tilt = TILT_HOME;
  float panTarget = PAN_HOME, tiltTarget = TILT_HOME;
  bool enabled = true, searchEnabled = true, haveObservation = false;
  Observation latest;
  State state = State::Waiting;
  uint32_t lastFaceMs = 0, lastTickMs = 0;
  int sweepDirection = 1, tiltDirection = 1;

  MOTION_FN void begin(uint32_t now) {lastTickMs = lastFaceMs = now;}
  MOTION_FN void setEnabled(bool value, uint32_t now) {
    if (enabled == value) return;
    enabled = value; haveObservation = false;
    panTarget = pan; tiltTarget = tilt; lastFaceMs = now;
    state = value ? State::Waiting : State::Paused;
  }
  MOTION_FN void setSearch(bool value) {
    searchEnabled = value;
    if (!value && state == State::Searching) {panTarget = pan; tiltTarget = tilt; state = State::Lost;}
  }
  MOTION_FN float correction(float error) {
    if (magnitude(error) <= DEAD_ZONE_PX) return 0;
    float outside = error > 0 ? error-DEAD_ZONE_PX : error+DEAD_ZONE_PX;
    return bound(outside * CORRECTION_DEG_PER_PX, -MAX_CORRECTION_DEG, MAX_CORRECTION_DEG);
  }
  MOTION_FN void observe(const Observation &input, uint32_t now) {
    latest = input; haveObservation = true;
    if (!enabled || !input.valid || now-input.capturedMs > FRESH_MS) {
      panTarget = pan; tiltTarget = tilt; return;
    }
    if (input.found) {
      lastFaceMs = now;
      // One bounded target per new detection; do not repeatedly integrate an old error.
      panTarget = bound(pan + PAN_DIRECTION * correction(input.dx), PAN_MIN, PAN_MAX);
      tiltTarget = bound(tilt + TILT_DIRECTION * correction(input.dy), TILT_MIN, TILT_MAX);
      state = State::Tracking;
    } else if (state != State::Searching) {
      panTarget = pan; tiltTarget = tilt; state = State::Lost;
    }
  }
  MOTION_FN void tick(uint32_t now) {
    float dt = bound((now-lastTickMs)/1000.0f, 0, 0.05f);
    lastTickMs = now;
    if (!enabled) {state = State::Paused; return;}
    if (!haveObservation || !latest.valid || now-latest.capturedMs > FRESH_MS) {
      panTarget = pan; tiltTarget = tilt;
      state = haveObservation ? State::CameraStale : State::Waiting; return;
    }
    float speed = TRACK_SPEED;
    if (!latest.found) {
      if (!searchEnabled || now-lastFaceMs < LOST_DELAY_MS) {state = State::Lost; return;}
      if (state != State::Searching) {
        sweepDirection = pan < (PAN_MIN+PAN_MAX)/2 ? 1 : -1;
        panTarget = sweepDirection > 0 ? PAN_MAX : PAN_MIN;
        tiltTarget = tilt; state = State::Searching;
      }
      speed = SEARCH_SPEED;
      if (magnitude(pan-panTarget) < 0.01f && magnitude(tilt-tiltTarget) < 0.01f) {
        sweepDirection = -sweepDirection;
        panTarget = sweepDirection > 0 ? PAN_MAX : PAN_MIN;
        if (tiltTarget >= TILT_MAX) tiltDirection = -1;
        if (tiltTarget <= TILT_MIN) tiltDirection = 1;
        tiltTarget = bound(tiltTarget + tiltDirection*25.0f, TILT_MIN, TILT_MAX);
      }
      // Move vertically at the edge before scanning the next horizontal row.
      if (magnitude(tilt-tiltTarget) >= 0.01f) {
        tilt = approach(tilt, tiltTarget, speed*dt); return;
      }
    }
    pan = bound(approach(pan, panTarget, speed*dt), PAN_MIN, PAN_MAX);
    tilt = bound(approach(tilt, tiltTarget, speed*dt), TILT_MIN, TILT_MAX);
  }
};
}

#undef MOTION_FN

