#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Touch input — TTP223 digital pad on GPIO7
// ============================================================================
// The pad is mounted inside the yeti body, so light/fast taps don't always
// cross the capacitive threshold — only a deliberate touch reliably drives the
// output high. So instead of release-based tap/double-tap timing, we fire the
// primary event the instant the pad goes HIGH (press edge), which feels
// responsive and can't be "missed". A sustained hold yields a second event.
// ============================================================================

enum TouchEvent : uint8_t {
  TOUCH_NONE = 0,
  TOUCH_PRESS,       // fired immediately when the pad is first touched
  TOUCH_LONG_PRESS   // fired once when held past LONG_MS
};

class TouchInput {
public:
  void begin() {
    pinMode(TOUCH_PIN, INPUT_PULLUP);   // button to GND; pull-up idles the pin HIGH
    _score = 0;
    _stable = false;
    _longFired = false;
    _lastMs = millis();
    _pressStart = 0;
  }

  // Call every frame. Leaky integrator: high time adds to a score, lows subtract
  // (faster), with hysteresis. A mostly-high real touch — even with fast chatter
  // dropouts — accumulates past PRESS_ON; isolated phantom blips decay before
  // they count. (Sustained false-latches still leak; those need the hardware
  // decoupling cap / sensitivity fix on the TTP223.)
  TouchEvent update() {
    unsigned long now = millis();
    int dt = (int)(now - _lastMs);
    _lastMs = now;
    if (dt < 0) dt = 0; else if (dt > 50) dt = 50;   // clamp big gaps

    if (pressedNow()) _score += dt;
    else              _score -= dt + dt / 2;          // decay 1.5x on lows
    if (_score < 0) _score = 0; else if (_score > SCORE_MAX) _score = SCORE_MAX;

    TouchEvent out = TOUCH_NONE;
    if (!_stable && _score >= PRESS_ON) {             // press
      _stable = true; _pressStart = now; _longFired = false;
      out = TOUCH_PRESS;
    } else if (_stable && _score <= PRESS_OFF) {      // release
      _stable = false;
    }
    if (_stable && !_longFired && (now - _pressStart) >= LONG_MS) {
      _longFired = true;
      out = TOUCH_LONG_PRESS;
    }
    return out;
  }

  bool isHeld() const { return _stable; }

private:
  static const int      SCORE_MAX = 70;
  static const int      PRESS_ON  = 30;   // ~30 ms net high to register
  static const int      PRESS_OFF = 8;
  static const uint16_t LONG_MS   = 1200; // deliberate hold -> info mode

  bool pressedNow() {
    int v = digitalRead(TOUCH_PIN);
#if TOUCH_ACTIVE_HIGH
    return v == HIGH;
#else
    return v == LOW;
#endif
  }

  int  _score;
  bool _stable, _longFired;
  unsigned long _lastMs, _pressStart;
};

#endif // TOUCH_INPUT_H
