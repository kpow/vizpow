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
    pinMode(TOUCH_PIN, INPUT);
    _prev = pressedNow();
    _pressStart = 0;
    _longFired = false;
  }

  // Call every frame. Fires PRESS on the rising edge (no debounce — the TTP223
  // output is already a clean digital signal, and its touch pulses can be
  // shorter than a debounce window). Returns at most one event per call.
  TouchEvent update() {
    unsigned long now = millis();
    bool r = pressedNow();
    TouchEvent out = TOUCH_NONE;

    if (r && !_prev) {               // ---- rising edge: act immediately ----
      _pressStart = now;
      _longFired = false;
      out = TOUCH_PRESS;
    } else if (r && !_longFired && (now - _pressStart) >= LONG_MS) {
      _longFired = true;             // ---- held long enough ----
      out = TOUCH_LONG_PRESS;
    }

    _prev = r;
    return out;
  }

  bool isHeld() const { return _prev; }

private:
  static const uint16_t LONG_MS = 1200;  // deliberate hold -> sleep

  bool pressedNow() {
    int v = digitalRead(TOUCH_PIN);
#if TOUCH_ACTIVE_HIGH
    return v == HIGH;
#else
    return v == LOW;
#endif
  }

  bool _prev;
  bool _longFired;
  unsigned long _pressStart;
};

#endif // TOUCH_INPUT_H
