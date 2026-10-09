#ifndef STACKCHAN_LEDS_H
#define STACKCHAN_LEDS_H

// The effect engine is base-agnostic: it only ever calls scSetBaseLedColor() /
// scRefreshBaseLeds() and reads SC_BASE_LED_COUNT, so it serves the stack-chan
// ring, the Faces strips and the 1.69 interior ring alike. Only the backend
// differs.
#include "config.h"
#ifdef HAS_BASE_LEDS

#include <Arduino.h>
#include <FastLED.h>   // screen mode: CRGBPalette16, rgb2hsv_approximate
#if defined(BOARD_HAS_STACKCHAN_BASE)
#include "stackchan_base.h"
#elif defined(BOARD_HAS_FACES_BASE)
#include "faces_base.h"
#else
#include "ring_leds.h"
#endif

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
//   9 = Audio (spectrum-reactive; idle glow on boards without a mic)
//  10 = Screen (follows the LCD's ambient background: its colours, and a motion
//       picked per effect; mood colour when the background isn't ambient)

#define SC_LED_MODE_OFF        0
#define SC_LED_MODE_BREATHING  1
#define SC_LED_MODE_RAINBOW    2
#define SC_LED_MODE_CHASE      3
#define SC_LED_MODE_FIRE       4
#define SC_LED_MODE_TWINKLE    5
#define SC_LED_MODE_PULSE      6
#define SC_LED_MODE_AURORA     7
#define SC_LED_MODE_MOOD       8
#define SC_LED_MODE_AUDIO      9
#define SC_LED_MODE_SCREEN     10
#define SC_LED_MODE_COUNT      11

static const char* const SC_LED_MODE_NAMES[] = {
  "off", "breathing", "rainbow", "chase", "fire",
  "twinkle", "pulse", "aurora", "mood", "audio", "screen"
};

// Screen mode: a 2D effect can't map onto a ring pixel for pixel, so each
// ambient effect (effects_ambient.h order) gets the ring motion closest to it.
enum ScScreenMotion : uint8_t {
  SC_MOTION_FLOW,    // palette gradient drifts slowly
  SC_MOTION_SPIN,    // comet rotates around the ring
  SC_MOTION_RIPPLE,  // brightness waves spread from one point
  SC_MOTION_MARCH,   // bands travel around the ring
  SC_MOTION_SWEEP,   // one bright band sweeps back and forth
  SC_MOTION_BLOCKS,  // two-tone segments swap in steps
  SC_MOTION_DOTS,    // a couple of dots chase with tails
};

static const uint8_t SC_SCREEN_MOTION[16] = {
  SC_MOTION_FLOW,    //  0 Plasma
  SC_MOTION_SPIN,    //  1 Galaxy
  SC_MOTION_RIPPLE,  //  2 Ripple
  SC_MOTION_MARCH,   //  3 Chevrons
  SC_MOTION_MARCH,   //  4 Stripes
  SC_MOTION_BLOCKS,  //  5 Checker
  SC_MOTION_SWEEP,   //  6 Scanline
  SC_MOTION_FLOW,    //  7 Perlin
  SC_MOTION_FLOW,    //  8 Distorsion
  SC_MOTION_SPIN,    //  9 ZVortex
  SC_MOTION_DOTS,    // 10 Snakes
  SC_MOTION_RIPPLE,  // 11 Sinusoid
  SC_MOTION_BLOCKS,  // 12 Puzzle
  SC_MOTION_FLOW,    // 13 Bumpmap
  SC_MOTION_BLOCKS,  // 14 Xorcery
  SC_MOTION_FLOW,    // 15 Hiphotic
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

  // Cost of update() incl. the push to hardware; worst case since /state last read
  uint32_t updateMaxUs = 0;

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

#ifdef HIRES_ENABLED
  // Screen mode: colours lifted from the last ambient frame, eased over time
  CRGBPalette16 screenPal = CRGBPalette16(CRGB::Black);
  float screenT = 0;   // motion clock, seconds x speed factor
#endif

  void init() {
#ifdef BOARD_HAS_RING_LEDS
    mode = SC_LED_MODE_SCREEN;   // the 1.69 ring exists to echo the screen
#else
    mode = SC_LED_MODE_RAINBOW;
#endif
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
      case SC_LED_MODE_AUDIO:     effectAudio(dt, speedFactor); break;
      case SC_LED_MODE_SCREEN:    effectScreen(dt, speedFactor); break;
      default:                    effectOff(); break;
    }

    // Audio overlay — when AudioFX is enabled (and we're not on OFF or the
    // dedicated AUDIO mode which already does its own thing), modulate the
    // ring brightness with rms and flash on beats so every base LED pattern
    // visibly reacts to music. Honors the global audioDrama setting.
    #ifdef TARGET_CORES3
    if (mode != SC_LED_MODE_OFF && mode != SC_LED_MODE_AUDIO) {
      extern struct AudioSpectrum audioSpectrum;
      extern uint8_t audioDrama;
      if (audioSpectrum.alive && audioDrama > 0) {
        const float dramaF = (float)audioDrama / 100.0f;            // 0..2
        const float rms     = constrain(audioSpectrum.rms     * dramaF, 0.0f, 1.5f);
        const float beatEnv = constrain(audioSpectrum.beatEnv * dramaF, 0.0f, 1.5f);
        // Brightness scale: 0.35 + 1.0 * rms → quiet dims to ~35%, loud boosts to ~185%
        const float brightScale = 0.35f + 1.0f * rms;
        // Beat flash: additive white-ish punch on each beat (saturating add)
        const uint8_t beatPunch = (uint8_t)(beatEnv * 90.0f);
        for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
          int r = (int)((float)ledR[i] * brightScale) + beatPunch;
          int g = (int)((float)ledG[i] * brightScale) + beatPunch;
          int b = (int)((float)ledB[i] * brightScale) + beatPunch;
          ledR[i] = (uint8_t)(r > 255 ? 255 : (r < 0 ? 0 : r));
          ledG[i] = (uint8_t)(g > 255 ? 255 : (g < 0 ? 0 : g));
          ledB[i] = (uint8_t)(b > 255 ? 255 : (b < 0 ? 0 : b));
        }
      }
    }
    #endif

    // Flash overlay (touch reactions etc.): starts as the flash colour and
    // crossfades back into the running effect. Blended at output only — never
    // into ledR/G/B, which effects like twinkle and fire carry between frames.
    // Pops at twice the effect brightness but still follows the setting, and
    // mode off stays dark.
    uint8_t flashMix = 0;   // 255 = all flash
    if (flashing) {
      if (now - flashStart < flashDurationMs && mode != SC_LED_MODE_OFF) {
        flashMix = 255 - (uint8_t)((now - flashStart) * 255 / flashDurationMs);
      } else {
        flashing = false;
      }
    }
    const uint8_t flashLevel = qadd8(brightness, brightness);

    // Apply brightness and push to hardware
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      uint8_t r = (ledR[i] * brightness) >> 8;
      uint8_t g = (ledG[i] * brightness) >> 8;
      uint8_t b = (ledB[i] * brightness) >> 8;
      if (flashMix) {
        r = lerp8by8(r, scale8(flashR, flashLevel), flashMix);
        g = lerp8by8(g, scale8(flashG, flashLevel), flashMix);
        b = lerp8by8(b, scale8(flashB, flashLevel), flashMix);
      }
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
    // Secondary ignition point (opposite side of ring; wraps on short rings)
    if (random(0, 5) == 0) {
      int pos = (SC_BASE_LED_COUNT / 2 + random(0, 3)) % SC_BASE_LED_COUNT;
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

  // Audio-reactive: bass→brightness, hue rotates with mid, beat flashes
  void effectAudio(float dt, float spd) {
    #ifndef TARGET_CORES3
    // No mic on the 1.69: hold the idle glow
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      ledR[i] = 10; ledG[i] = 0; ledB[i] = 15;
    }
    #else
    extern struct AudioSpectrum audioSpectrum;
    if (!audioSpectrum.alive) {
      // Dim idle glow when audio isn't flowing
      for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
        ledR[i] = 10; ledG[i] = 0; ledB[i] = 15;
      }
      return;
    }
    float b = audioSpectrum.bass;
    float m = audioSpectrum.mid;
    float t = audioSpectrum.treble;
    float r = audioSpectrum.rms;
    float beat = audioSpectrum.beatEnv;

    // Base hue rotates slowly, mid shifts it faster
    phase += dt * (0.1f + m * 0.5f) * spd;
    if (phase > 1.0f) phase -= 1.0f;
    uint8_t baseHue = (uint8_t)(phase * 255);

    // Overall brightness from RMS + bass boost
    float vol = constrain(r + b * 0.5f, 0.0f, 1.0f);
    uint8_t intensity = (uint8_t)(40 + vol * 215);

    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      // Spread hue around ring, treble widens the spread
      uint8_t hue = baseHue + (uint8_t)(i * (15 + t * 10));
      // Beat pulse: flash white-hot on beats
      uint8_t sat = (beat > 0.3f) ? (uint8_t)(240 - beat * 180) : 240;
      uint8_t val = (beat > 0.3f) ? min(255, (int)(intensity + beat * 100)) : intensity;
      ScRgb c = scHsvToRgb(hue, sat, val);
      ledR[i] = c.r; ledG[i] = c.g; ledB[i] = c.b;
    }
    #endif
  }

  // Follow the LCD background. Colours come from the frame actually drawn — the
  // ambient effects build theirs from a rotating hue, not the palette — and the
  // motion comes from SC_SCREEN_MOTION. Any other background (black, gradient,
  // starfield) shows the expression's mood colour instead.
  void effectScreen(float dt, float spd) {
#ifdef HIRES_ENABLED
    extern uint8_t botBackgroundStyle;
    extern uint8_t effectIndex;
    if (botBackgroundStyle != 4) { effectMood(dt, spd); return; }

    sampleScreenPalette();
    screenT += dt * spd;
    if (screenT > 3600.0f) screenT -= 3600.0f;   // keep float precision
    const float t = screenT;
    const int n = SC_BASE_LED_COUNT;

    switch (SC_SCREEN_MOTION[effectIndex % 16]) {
      case SC_MOTION_FLOW:
        for (int i = 0; i < n; i++) {
          uint8_t idx = i * 128 / n + (uint8_t)(t * 16);
          uint8_t v = 150 + scale8(sin8(i * 256 / n + (uint8_t)(t * 40)), 105);
          setLed(i, screenColor(idx, v));
        }
        break;

      case SC_MOTION_SPIN: {
        float head = fmodf(t * 0.6f, 1.0f) * n;
        for (int i = 0; i < n; i++) {
          float d = head - i;                 // distance behind the head
          if (d < 0) d += n;
          float k = 1.0f - d / n;
          uint8_t v = 50 + (uint8_t)(205.0f * k * k);
          setLed(i, screenColor(i * 256 / n + (uint8_t)(t * 30), v));
        }
        break;
      }

      case SC_MOTION_RIPPLE:
        for (int i = 0; i < n; i++) {
          uint8_t dn = ringDist(i) * 256 / n;  // 0..128 away from LED 0
          uint8_t wave = sin8(dn * 2 - (uint8_t)(t * 160));
          setLed(i, screenColor(dn + (uint8_t)(t * 10), 50 + scale8(wave, 205)));
        }
        break;

      case SC_MOTION_MARCH:
        for (int i = 0; i < n; i++) {
          uint8_t wave = sin8(i * 512 / n - (uint8_t)(t * 150));   // two bands
          uint8_t v = scale8(wave, wave);
          if (v < 25) v = 25;
          setLed(i, screenColor(i * 128 / n + (uint8_t)(t * 12), v));
        }
        break;

      case SC_MOTION_SWEEP: {
        // The ring's two sides read as screen rows: LED 0 = top, n/2 = bottom
        float ph = fmodf(t / 2.4f, 1.0f);
        float row = (ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f) * (n / 2);
        for (int i = 0; i < n; i++) {
          float diff = fabsf(ringDist(i) - row);
          int v = 255 - (int)(diff * 150.0f);
          setLed(i, screenColor(ringDist(i) * 256 / n + (uint8_t)(t * 8),
                                (uint8_t)(v < 20 ? 20 : v)));
        }
        break;
      }

      case SC_MOTION_BLOCKS: {
        uint32_t step = (uint32_t)(t * 2.5f);
        uint8_t base = (uint8_t)(t * 8);
        for (int i = 0; i < n; i++) {
          bool on = ((i / 2 + step) & 1);
          setLed(i, screenColor(on ? base : base + 128, on ? 255 : 170));
        }
        break;
      }

      case SC_MOTION_DOTS: {
        static const float DOT_SPEED[2] = {0.45f, 0.3f};   // revolutions/s
        for (int i = 0; i < n; i++) {
          CRGB c = screenColor(i * 256 / n, 18);
          for (int k = 0; k < 2; k++) {
            float d = fmodf(t * DOT_SPEED[k] + k * 0.5f, 1.0f) * n - i;
            if (d < 0) d += n;
            if (d < 2.5f) c += screenColor(k * 128 + (uint8_t)(t * 10),
                                           (uint8_t)(255.0f * (1.0f - d / 2.5f)));
          }
          setLed(i, c);
        }
        break;
      }
    }
#else
    effectMood(dt, spd);
#endif
  }

#ifdef HIRES_ENABLED
  // Point-sample a 4x4 grid of the last ambient frame (post-kaleidoscope, pre-
  // face), drop near-black samples (their hue is noise), sort by hue starting
  // after the widest gap so the palette reads as a gradient, force full value
  // (the motion owns brightness), and ease toward it so re-sorts don't pop.
  void sampleScreenPalette() {
    extern bool hiResMode;
    const CRGB* buf = hiResMode ? hiResCrgbBuf : pixelModeBuf;
    const uint8_t w = hiResMode ? HIRES_COLS : PIXEL_MODE_COLS;
    const uint8_t h = hiResMode ? HIRES_ROWS : PIXEL_MODE_ROWS;

    CHSV s[16];
    uint8_t cnt = 0;
    for (uint8_t gy = 0; gy < 4; gy++) {
      for (uint8_t gx = 0; gx < 4; gx++) {
        CRGB p = buf[((2 * gy + 1) * h / 8) * w + (2 * gx + 1) * w / 8];
        if (p.getAverageLight() < 12) continue;
        s[cnt++] = rgb2hsv_approximate(p);
      }
    }
    if (cnt == 0) return;   // all dark: keep the previous colours

    for (uint8_t i = 1; i < cnt; i++) {   // insertion sort by hue
      CHSV x = s[i];
      int8_t j = i - 1;
      while (j >= 0 && s[j].h > x.h) { s[j + 1] = s[j]; j--; }
      s[j + 1] = x;
    }
    uint8_t start = 0, widest = 0;
    for (uint8_t i = 0; i < cnt; i++) {
      uint8_t gap = s[(i + 1) % cnt].h - s[i].h;   // wraps mod 256
      if (cnt == 1 || gap > widest) { widest = gap; start = (i + 1) % cnt; }
    }

    for (uint8_t e = 0; e < 16; e++) {
      CHSV c = s[(start + e * cnt / 16) % cnt];
      c.v = 255;
      nblend(screenPal[e], CRGB(c), 40);
    }
  }

  CRGB screenColor(uint8_t idx, uint8_t v) {
    return ColorFromPalette(screenPal, idx, v, LINEARBLEND);
  }
#endif

  // Steps from LED 0 the short way round: 0..n/2
  static int ringDist(int i) {
    return i <= SC_BASE_LED_COUNT / 2 ? i : SC_BASE_LED_COUNT - i;
  }

  void setLed(int i, const CRGB& c) {
    ledR[i] = c.r; ledG[i] = c.g; ledB[i] = c.b;
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

// Flash the base LEDs in an expression's mood colour (defined below)
inline void scFlashMood(uint8_t exprIndex, uint16_t durationMs);

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

inline void scFlashMood(uint8_t exprIndex, uint16_t durationMs) {
  if (exprIndex >= 25) exprIndex = 0;
  ScMoodColor c;
  memcpy_P(&c, &SC_MOOD_COLORS[exprIndex], sizeof(ScMoodColor));
  scLeds.flash(c.r, c.g, c.b, durationMs);
}

inline void scUpdateMoodFromExpression(uint8_t exprIndex) {
  if (exprIndex >= 25) exprIndex = 0;
  if (exprIndex == scLastMoodExpr) return;  // no change
  scLastMoodExpr = exprIndex;

  ScMoodColor c;
  memcpy_P(&c, &SC_MOOD_COLORS[exprIndex], sizeof(ScMoodColor));
  scLeds.setMoodColor(c.r, c.g, c.b);
}

#endif // HAS_BASE_LEDS
#endif // STACKCHAN_LEDS_H
