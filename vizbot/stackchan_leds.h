#ifndef STACKCHAN_LEDS_H
#define STACKCHAN_LEDS_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include "config.h"
#include "stackchan_base.h"

// ============================================================================
// Base LED Glow Library — animated effects for the 12-LED WS2812C ring
// ============================================================================
// Call scLeds.update() once per frame (~30fps). Effects are self-contained
// and use millis() for timing so they stay smooth regardless of frame rate.
//
// LED modes:
//   0 = Off
//   1 = Breathing (slow pulse)
//   2 = Rainbow cycle
//   3 = Chase (dot chasing around ring)
//   4 = Fire (flickering warm tones)
//   5 = Twinkle (random sparkles)
//   6 = Pulse wave (expanding ring pulse)
//   7 = Aurora (slow shifting greens/blues/purples)
//   8 = Mood (solid color, set externally — used by mood ring)

#define SC_LED_MODE_OFF        0
#define SC_LED_MODE_BREATHING  1
#define SC_LED_MODE_RAINBOW    2
#define SC_LED_MODE_CHASE      3
#define SC_LED_MODE_FIRE       4
#define SC_LED_MODE_TWINKLE    5
#define SC_LED_MODE_PULSE      6
#define SC_LED_MODE_AURORA     7
#define SC_LED_MODE_MOOD       8
#define SC_LED_MODE_COUNT      9

static const char* const SC_LED_MODE_NAMES[] = {
  "off", "breathing", "rainbow", "chase", "fire",
  "twinkle", "pulse", "aurora", "mood"
};

// ============================================================================
// HSV to RGB helper (avoid pulling in full FastLED for 12 LEDs)
// ============================================================================
struct ScRgb { uint8_t r, g, b; };

inline ScRgb scHsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
  if (s == 0) return {v, v, v};
  uint8_t region = h / 43;
  uint8_t remainder = (h - region * 43) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
  switch (region) {
    case 0:  return {v, t, p};
    case 1:  return {q, v, p};
    case 2:  return {p, v, t};
    case 3:  return {p, q, v};
    case 4:  return {t, p, v};
    default: return {v, p, q};
  }
}

// ============================================================================
// LED Effect Engine
// ============================================================================
struct ScBaseLeds {
  uint8_t mode = SC_LED_MODE_RAINBOW;  // default: rainbow
  uint8_t brightness = 80;             // 0-255 global brightness
  uint8_t speed = 128;                 // 0-255 effect speed multiplier

  // Mood mode color (set externally by mood ring)
  uint8_t moodR = 0, moodG = 0, moodB = 100;

  // Flash overlay (for touch reactions, etc.)
  bool flashing = false;
  uint8_t flashR, flashG, flashB;
  unsigned long flashStart = 0;
  uint16_t flashDurationMs = 200;

  // Internal state
  unsigned long lastUpdate = 0;
  float phase = 0;        // general animation phase (0-1 wrapping)
  uint8_t chasePos = 0;   // chase dot position
  uint8_t twinkleMap = 0; // bitmask-ish for active twinkles
  unsigned long lastTwinkle = 0;

  // Per-LED color buffer
  uint8_t ledR[SC_BASE_LED_COUNT];
  uint8_t ledG[SC_BASE_LED_COUNT];
  uint8_t ledB[SC_BASE_LED_COUNT];

  // Fire heat buffer
  uint8_t heat[SC_BASE_LED_COUNT];

  void init() {
    mode = SC_LED_MODE_RAINBOW;
    brightness = 80;
    speed = 128;
    phase = 0;
    chasePos = 0;
    lastUpdate = millis();
    memset(heat, 0, sizeof(heat));
    memset(ledR, 0, sizeof(ledR));
    memset(ledG, 0, sizeof(ledG));
    memset(ledB, 0, sizeof(ledB));
  }

  // Start a brief flash overlay (touch reaction, notification, etc.)
  void flash(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs = 200) {
    flashing = true;
    flashR = r; flashG = g; flashB = b;
    flashStart = millis();
    flashDurationMs = durationMs;
  }

  // Set mood color (used by mood ring mode)
  void setMoodColor(uint8_t r, uint8_t g, uint8_t b) {
    moodR = r; moodG = g; moodB = b;
  }

  // Main update — call once per frame
  void update() {
    if (!sysStatus.scBaseLedsReady) return;

    unsigned long now = millis();
    float dt = (now - lastUpdate) / 1000.0f;
    lastUpdate = now;

    // Speed factor: 0.25x at speed=0, 1x at speed=128, 2x at speed=255
    float speedFactor = 0.25f + (speed / 128.0f) * 0.875f;

    // Check flash overlay
    if (flashing) {
      if (now - flashStart < flashDurationMs) {
        // Flash overrides everything
        float t = (float)(now - flashStart) / flashDurationMs;
        // Fade out over duration
        uint8_t fR = flashR * (1.0f - t);
        uint8_t fG = flashG * (1.0f - t);
        uint8_t fB = flashB * (1.0f - t);
        for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
          scSetBaseLedColor(i, fR, fG, fB);
        }
        scRefreshBaseLeds();
        return;
      }
      flashing = false;
    }

    // Run the active effect
    switch (mode) {
      case SC_LED_MODE_OFF:       effectOff(); break;
      case SC_LED_MODE_BREATHING: effectBreathing(dt, speedFactor); break;
      case SC_LED_MODE_RAINBOW:   effectRainbow(dt, speedFactor); break;
      case SC_LED_MODE_CHASE:     effectChase(dt, speedFactor); break;
      case SC_LED_MODE_FIRE:      effectFire(dt, speedFactor); break;
      case SC_LED_MODE_TWINKLE:   effectTwinkle(dt, speedFactor); break;
      case SC_LED_MODE_PULSE:     effectPulse(dt, speedFactor); break;
      case SC_LED_MODE_AURORA:    effectAurora(dt, speedFactor); break;
      case SC_LED_MODE_MOOD:      effectMood(dt, speedFactor); break;
      default:                    effectOff(); break;
    }

    // Apply brightness and push to hardware
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      uint8_t r = (ledR[i] * brightness) >> 8;
      uint8_t g = (ledG[i] * brightness) >> 8;
      uint8_t b = (ledB[i] * brightness) >> 8;
      scSetBaseLedColor(i, r, g, b);
    }
    scRefreshBaseLeds();
  }

  // --- Effects ---

  void effectOff() {
    memset(ledR, 0, sizeof(ledR));
    memset(ledG, 0, sizeof(ledG));
    memset(ledB, 0, sizeof(ledB));
  }

  // Slow sine-wave pulse, all LEDs same color cycling through hue
  void effectBreathing(float dt, float spd) {
    phase += dt * 0.4f * spd;
    if (phase > 1.0f) phase -= 1.0f;
    // Sine breath: 0→1→0
    float breath = (sin(phase * 2.0f * PI) + 1.0f) * 0.5f;
    uint8_t v = 40 + (uint8_t)(breath * 215);
    // Slowly shift hue
    uint8_t hue = (uint8_t)(phase * 255 * 3) % 256;  // 3 hue cycles per breath cycle
    ScRgb c = scHsvToRgb(hue, 220, v);
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      ledR[i] = c.r; ledG[i] = c.g; ledB[i] = c.b;
    }
  }

  // Classic rainbow rotating around the ring
  void effectRainbow(float dt, float spd) {
    phase += dt * 0.3f * spd;
    if (phase > 1.0f) phase -= 1.0f;
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      uint8_t hue = (uint8_t)((phase * 255) + (i * 255 / SC_BASE_LED_COUNT)) % 256;
      ScRgb c = scHsvToRgb(hue, 255, 255);
      ledR[i] = c.r; ledG[i] = c.g; ledB[i] = c.b;
    }
  }

  // Bright dot chasing around the ring with fading tail
  void effectChase(float dt, float spd) {
    phase += dt * 3.0f * spd;
    if (phase >= SC_BASE_LED_COUNT) {
      phase -= SC_BASE_LED_COUNT;
    }
    // Slowly rotating hue for the chase dot
    uint8_t hue = (uint8_t)(millis() / 50) % 256;
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      // Distance from chase head (wrapping around ring)
      float dist = phase - i;
      if (dist < 0) dist += SC_BASE_LED_COUNT;
      // Tail fade: 3 LEDs behind the head
      float intensity = 0;
      if (dist < 0.5f) intensity = 1.0f;
      else if (dist < 3.5f) intensity = 1.0f - (dist - 0.5f) / 3.0f;
      if (intensity < 0) intensity = 0;
      ScRgb c = scHsvToRgb(hue, 255, (uint8_t)(intensity * 255));
      ledR[i] = c.r; ledG[i] = c.g; ledB[i] = c.b;
    }
  }

  // Flickering warm fire tones
  void effectFire(float dt, float spd) {
    // Cool down every cell a little
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      uint8_t cooldown = random(0, 20);
      heat[i] = (heat[i] > cooldown) ? heat[i] - cooldown : 0;
    }
    // Heat drifts "up" (around the ring)
    for (int i = SC_BASE_LED_COUNT - 1; i >= 2; i--) {
      heat[i] = (heat[i - 1] + heat[i - 2]) / 2;
    }
    // Random ignition at base positions
    if (random(0, 4) == 0) {
      int pos = random(0, 3);
      heat[pos] = min(255L, (long)heat[pos] + random(120, 200));
    }
    // Secondary ignition point (opposite side of ring)
    if (random(0, 5) == 0) {
      int pos = 6 + random(0, 3);
      heat[pos] = min(255L, (long)heat[pos] + random(100, 180));
    }
    // Map heat to fire palette (black → red → orange → yellow → white)
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      uint8_t h = heat[i];
      if (h < 85) {
        ledR[i] = h * 3; ledG[i] = 0; ledB[i] = 0;
      } else if (h < 170) {
        ledR[i] = 255; ledG[i] = (h - 85) * 3; ledB[i] = 0;
      } else {
        ledR[i] = 255; ledG[i] = 255; ledB[i] = (h - 170) * 3;
      }
    }
  }

  // Random sparkles popping in and fading
  void effectTwinkle(float dt, float spd) {
    unsigned long now = millis();
    // Fade all LEDs toward black
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      if (ledR[i] > 8) ledR[i] -= 8; else ledR[i] = 0;
      if (ledG[i] > 8) ledG[i] -= 8; else ledG[i] = 0;
      if (ledB[i] > 8) ledB[i] -= 8; else ledB[i] = 0;
    }
    // Spawn new twinkles
    uint16_t spawnInterval = 200 - (spd * 80);
    if (spawnInterval < 40) spawnInterval = 40;
    if (now - lastTwinkle > spawnInterval) {
      lastTwinkle = now;
      int pos = random(0, SC_BASE_LED_COUNT);
      // Random bright pastel color
      uint8_t hue = random(0, 256);
      ScRgb c = scHsvToRgb(hue, 180, 255);
      ledR[pos] = c.r; ledG[pos] = c.g; ledB[pos] = c.b;
      // Sometimes spawn a neighbor too for cluster effect
      if (random(100) < 40) {
        int neighbor = (pos + 1) % SC_BASE_LED_COUNT;
        ScRgb c2 = scHsvToRgb(hue + 20, 200, 200);
        ledR[neighbor] = c2.r; ledG[neighbor] = c2.g; ledB[neighbor] = c2.b;
      }
    }
  }

  // Expanding pulse wave from a point, radiating outward around ring
  void effectPulse(float dt, float spd) {
    phase += dt * 1.5f * spd;
    if (phase > 1.0f) phase -= 1.0f;
    // Pulse origin moves slowly
    float origin = fmod(millis() / 8000.0f, 1.0f) * SC_BASE_LED_COUNT;
    uint8_t hue = (uint8_t)(millis() / 30) % 256;
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      // Distance from origin (ring-wrapped)
      float dist = fabs(i - origin);
      if (dist > SC_BASE_LED_COUNT / 2.0f) dist = SC_BASE_LED_COUNT - dist;
      // Expanding wave front
      float wave = fmod(phase * SC_BASE_LED_COUNT, (float)SC_BASE_LED_COUNT);
      float diff = fabs(dist - wave);
      if (diff > SC_BASE_LED_COUNT / 2.0f) diff = SC_BASE_LED_COUNT - diff;
      float intensity = max(0.0f, 1.0f - diff / 2.0f);
      ScRgb c = scHsvToRgb(hue + (uint8_t)(i * 10), 240, (uint8_t)(intensity * 255));
      ledR[i] = c.r; ledG[i] = c.g; ledB[i] = c.b;
    }
  }

  // Slow-shifting aurora (greens, blues, purples)
  void effectAurora(float dt, float spd) {
    phase += dt * 0.15f * spd;
    if (phase > 1.0f) phase -= 1.0f;
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      // Each LED has its own sine wave with offset
      float pos = (float)i / SC_BASE_LED_COUNT;
      float wave1 = sin((phase + pos) * 2.0f * PI) * 0.5f + 0.5f;
      float wave2 = sin((phase * 1.7f + pos * 2.3f) * 2.0f * PI) * 0.5f + 0.5f;
      float wave3 = sin((phase * 0.6f + pos * 3.1f) * 2.0f * PI) * 0.5f + 0.5f;
      // Aurora palette: green (85) → cyan (128) → blue (170) → purple (200)
      uint8_t hue = 85 + (uint8_t)(wave1 * 115);
      uint8_t sat = 200 + (uint8_t)(wave2 * 55);
      uint8_t val = 60 + (uint8_t)(wave3 * 195);
      ScRgb c = scHsvToRgb(hue, sat, val);
      ledR[i] = c.r; ledG[i] = c.g; ledB[i] = c.b;
    }
  }

  // Solid mood color with subtle breathing
  void effectMood(float dt, float spd) {
    phase += dt * 0.3f * spd;
    if (phase > 1.0f) phase -= 1.0f;
    float breath = (sin(phase * 2.0f * PI) + 1.0f) * 0.5f;
    float scale = 0.5f + breath * 0.5f;  // 50%-100% brightness modulation
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      ledR[i] = (uint8_t)(moodR * scale);
      ledG[i] = (uint8_t)(moodG * scale);
      ledB[i] = (uint8_t)(moodB * scale);
    }
  }
};

// Global instance
static ScBaseLeds scLeds;

// ============================================================================
// Mood Ring — expression-to-color mapping
// ============================================================================
// Each expression maps to an RGB mood color. When the bot's expression changes,
// call scUpdateMoodFromExpression() to push the new color to the LED ring.
// Only takes effect when LED mode is SC_LED_MODE_MOOD.

struct ScMoodColor { uint8_t r, g, b; };

// Color palette: warm = positive emotions, cool = negative, vivid = intense
static const ScMoodColor SC_MOOD_COLORS[] PROGMEM = {
  // 0  NEUTRAL    — soft white
  { 180, 180, 200 },
  // 1  HAPPY      — warm yellow
  { 255, 220,  50 },
  // 2  SAD        — deep blue
  {  30,  60, 200 },
  // 3  SURPRISED  — bright cyan flash
  {   0, 255, 255 },
  // 4  CHILL      — mellow teal
  {  60, 200, 180 },
  // 5  ANGRY      — hot red
  { 255,  20,  10 },
  // 6  LOVE       — pink
  { 255,  60, 120 },
  // 7  DIZZY      — spinning purple
  { 180,  50, 255 },
  // 8  THINKING   — amber
  { 255, 180,  30 },
  // 9  EXCITED    — electric orange
  { 255, 120,   0 },
  // 10 MISCHIEF   — lime green
  { 120, 255,  30 },
  // 11 SKEPTICAL  — dusty orange
  { 200, 140,  60 },
  // 12 WORRIED    — pale blue
  { 100, 140, 220 },
  // 13 CONFUSED   — lavender
  { 160, 120, 255 },
  // 14 PROUD      — gold
  { 255, 200,  30 },
  // 15 SHY        — soft pink
  { 255, 150, 180 },
  // 16 ANNOYED    — burnt orange
  { 220, 100,  20 },
  // 17 FOCUSED    — cool white-blue
  { 140, 180, 255 },
  // 18 WINKING    — playful magenta
  { 255,  50, 200 },
  // 19 DEVIOUS    — dark green
  {  40, 180,  60 },
  // 20 SHOCKED    — white flash
  { 255, 255, 255 },
  // 21 KISSING    — rose
  { 255,  80, 140 },
  // 22 NERVOUS    — flickering yellow-green
  { 200, 220,  50 },
  // 23 GLITCHING  — neon green
  {   0, 255,  60 },
  // 24 SASSY      — hot magenta
  { 255,  20, 180 },
};

static uint8_t scLastMoodExpr = 255;  // track changes

inline void scUpdateMoodFromExpression(uint8_t exprIndex) {
  if (exprIndex >= 25) exprIndex = 0;
  if (exprIndex == scLastMoodExpr) return;  // no change
  scLastMoodExpr = exprIndex;

  ScMoodColor c;
  memcpy_P(&c, &SC_MOOD_COLORS[exprIndex], sizeof(ScMoodColor));
  scLeds.setMoodColor(c.r, c.g, c.b);
}

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_LEDS_H
