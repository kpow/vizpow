#ifndef TWEEN_H
#define TWEEN_H

#include <Arduino.h>
#include <math.h>

// ============================================================================
// Tween System — Smooth Animation Primitives
// ============================================================================
// Lightweight tween engine for ESP32 animation. Drives float values from
// start to end over a duration using pluggable easing functions.
//
// Usage:
//   float myValue = 0.0f;
//   tweenManager.start(&myValue, 0.0f, 1.0f, 300, EASE_OUT_QUAD);
//   // ... in loop:
//   tweenManager.update();  // advances all active tweens
//
// TweenManager holds 16 slots. If all slots are full, the oldest tween
// is evicted to make room for the new one.
// ============================================================================

// ============================================================================
// Easing Functions
// ============================================================================
// Each takes t in [0..1] and returns eased value in [0..1].
// Named after common easing conventions (Robert Penner style).

enum EaseType : uint8_t {
  EASE_LINEAR = 0,
  EASE_IN_QUAD,
  EASE_OUT_QUAD,
  EASE_IN_OUT_QUAD,
  EASE_OUT_CUBIC,
  EASE_OUT_BOUNCE,
  EASE_OUT_ELASTIC,
  EASE_OUT_BACK,
  EASE_COUNT
};

inline float easeLinear(float t)     { return t; }
inline float easeInQuad(float t)     { return t * t; }
inline float easeOutQuad(float t)    { return t * (2.0f - t); }
inline float easeInOutQuad(float t)  { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }
inline float easeOutCubic(float t)   { float u = t - 1.0f; return u * u * u + 1.0f; }

inline float easeOutBounce(float t) {
  if (t < 1.0f / 2.75f) {
    return 7.5625f * t * t;
  } else if (t < 2.0f / 2.75f) {
    t -= 1.5f / 2.75f;
    return 7.5625f * t * t + 0.75f;
  } else if (t < 2.5f / 2.75f) {
    t -= 2.25f / 2.75f;
    return 7.5625f * t * t + 0.9375f;
  } else {
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + 0.984375f;
  }
}

inline float easeOutElastic(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return powf(2.0f, -10.0f * t) * sinf((t - 0.075f) * (2.0f * PI) / 0.3f) + 1.0f;
}

inline float easeOutBack(float t) {
  const float c1 = 1.70158f;
  const float c3 = c1 + 1.0f;
  float u = t - 1.0f;
  return 1.0f + c3 * u * u * u + c1 * u * u;
}

// Easing function lookup table
typedef float (*EaseFunc)(float);
static const EaseFunc easeFuncs[EASE_COUNT] = {
  easeLinear,
  easeInQuad,
  easeOutQuad,
  easeInOutQuad,
  easeOutCubic,
  easeOutBounce,
  easeOutElastic,
  easeOutBack
};

// ============================================================================
// Tween Slot
// ============================================================================

struct Tween {
  float* target;          // Pointer to the value being animated
  float from;             // Start value
  float to;               // End value
  unsigned long startMs;  // millis() when tween started
  uint16_t durationMs;    // Total duration in ms
  EaseType easing;        // Easing function to use
  bool active;            // Whether this slot is in use

  void init() {
    target = nullptr;
    active = false;
  }

  // Returns true if tween completed this update
  bool update(unsigned long now) {
    if (!active || target == nullptr) return false;

    unsigned long elapsed = now - startMs;
    if (elapsed >= durationMs) {
      *target = to;
      active = false;
      return true;  // Completed
    }

    float t = (float)elapsed / (float)durationMs;
    float eased = easeFuncs[easing](t);
    *target = from + (to - from) * eased;
    return false;
  }
};

// ============================================================================
// TweenManager — Manages up to 16 concurrent tweens
// ============================================================================

#define TWEEN_MAX_SLOTS 16

struct TweenManager {
  Tween slots[TWEEN_MAX_SLOTS];

  void init() {
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      slots[i].init();
    }
  }

  // Start a new tween. If the target is already being tweened, that slot is
  // reused (prevents competing tweens on the same value). If no free slot,
  // the oldest active tween is evicted.
  void start(float* target, float from, float to, uint16_t durationMs,
             EaseType easing = EASE_OUT_QUAD) {
    // First: check if this target already has an active tween — reuse that slot
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      if (slots[i].active && slots[i].target == target) {
        slots[i].from = from;
        slots[i].to = to;
        slots[i].startMs = millis();
        slots[i].durationMs = durationMs;
        slots[i].easing = easing;
        return;
      }
    }

    // Second: find an inactive slot
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      if (!slots[i].active) {
        _fillSlot(i, target, from, to, durationMs, easing);
        return;
      }
    }

    // All slots full — evict the oldest
    uint8_t oldest = 0;
    unsigned long oldestTime = ULONG_MAX;
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      if (slots[i].startMs < oldestTime) {
        oldestTime = slots[i].startMs;
        oldest = i;
      }
    }
    // Snap evicted tween to its end value before overwriting
    if (slots[oldest].target != nullptr) {
      *slots[oldest].target = slots[oldest].to;
    }
    _fillSlot(oldest, target, from, to, durationMs, easing);
  }

  // Convenience: tween from current value to target
  void startTo(float* target, float to, uint16_t durationMs,
               EaseType easing = EASE_OUT_QUAD) {
    start(target, *target, to, durationMs, easing);
  }

  // Update all active tweens. Call once per frame.
  void update() {
    unsigned long now = millis();
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      slots[i].update(now);
    }
  }

  // Cancel all tweens on a specific target
  void cancel(float* target) {
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      if (slots[i].active && slots[i].target == target) {
        slots[i].active = false;
      }
    }
  }

  // Cancel all active tweens
  void cancelAll() {
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      slots[i].active = false;
    }
  }

  // Check if a specific target has an active tween
  bool isActive(float* target) const {
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      if (slots[i].active && slots[i].target == target) return true;
    }
    return false;
  }

  // Count of active tweens
  uint8_t activeCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < TWEEN_MAX_SLOTS; i++) {
      if (slots[i].active) count++;
    }
    return count;
  }

private:
  void _fillSlot(uint8_t i, float* target, float from, float to,
                 uint16_t durationMs, EaseType easing) {
    slots[i].target = target;
    slots[i].from = from;
    slots[i].to = to;
    slots[i].startMs = millis();
    slots[i].durationMs = durationMs;
    slots[i].easing = easing;
    slots[i].active = true;
  }
};

// Global tween manager instance
TweenManager tweenManager;

#endif // TWEEN_H
