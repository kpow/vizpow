#ifndef STRIP_LEDS_H
#define STRIP_LEDS_H

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "config.h"
#include "bot_faces.h"   // EXPR_* ids

// ============================================================================
// Inside lights — 8× WS2812B strip on GPIO4
// ============================================================================
// Only the strip is driven. The C3 has two RMT TX channels and FastLED gives
// each strip two memory blocks there (FastLED #1846: one block flickers when
// WiFi interrupts delay the refill), so one strip uses up the RMT. The
// onboard NeoPixel on GPIO8 stays dark; the strip replaces it.
// The body is translucent, so these are a primary feature, not a status LED.
// Modes (web-switchable, saved in NVS):
//   LEDS_MOOD    — colour + motion follow the face's expression; blinks dim it
//   LEDS_AMBIENT — independent light show, cycles patterns on its own
//   LEDS_OFF
// A "spark" is a short rainbow burst laid over either mode.
//
// Each tick renders a target frame, then the shown frame eases toward it, so
// mood changes cross-fade instead of snapping.
// ============================================================================

enum LedMode : uint8_t { LEDS_MOOD = 0, LEDS_AMBIENT, LEDS_OFF, LEDS_NUM_MODES };

enum LedMotion : uint8_t {
  LM_STEADY, LM_BREATHE, LM_PULSE, LM_HEART, LM_WAVE, LM_CHASE,
  LM_SPARKLE, LM_RAINBOW, LM_FLICKER, LM_STROBE, LM_GLITCH, LM_DUAL
};

struct LedMood {
  uint8_t hue, sat, val;   // base colour (FastLED hue: 0 red, 64 yellow, 96 green, 160 blue, 224 pink)
  uint8_t motion;          // LedMotion
  uint8_t speed;           // 1 = slow .. 10 = fast
  uint8_t hue2;            // second colour for LM_DUAL / LM_SPARKLE accents
};

// One row per expression, in EXPR_* order
static const LedMood ledMoods[BOT_NUM_EXPRESSIONS] = {
  /* NEUTRAL   */ { 130, 220, 150, LM_BREATHE, 2, 130 },
  /* HAPPY     */ {  42, 240, 230, LM_WAVE,    4,  64 },
  /* SAD       */ { 160, 255,  90, LM_BREATHE, 1, 170 },
  /* SURPRISED */ {   0,   0, 255, LM_PULSE,   8,   0 },
  /* CHILL     */ { 118, 230, 140, LM_WAVE,    1, 150 },
  /* ANGRY     */ {   0, 255, 255, LM_FLICKER, 8,  10 },
  /* LOVE      */ { 236, 230, 230, LM_HEART,   5, 250 },
  /* DIZZY     */ {   0, 255, 220, LM_RAINBOW, 9,   0 },
  /* THINKING  */ { 190, 230, 170, LM_CHASE,   3, 160 },
  /* EXCITED   */ {  20, 255, 255, LM_SPARKLE, 9,  64 },
  /* MISCHIEF  */ {  90, 255, 200, LM_SPARKLE, 5, 200 },
  /* SKEPTICAL */ {  30, 230, 120, LM_STEADY,  1,  30 },
  /* WORRIED   */ { 180, 160, 140, LM_FLICKER, 3, 160 },
  /* CONFUSED  */ { 210, 220, 180, LM_DUAL,    3, 120 },
  /* PROUD     */ {  48, 220, 240, LM_SPARKLE, 3,  40 },
  /* SHY       */ { 232, 150, 100, LM_BREATHE, 2, 232 },
  /* ANNOYED   */ {  12, 255, 210, LM_PULSE,   5,  12 },
  /* FOCUSED   */ { 150,  90, 200, LM_STEADY,  1, 150 },
  /* WINKING   */ {  56, 240, 220, LM_DUAL,    5, 240 },
  /* DEVIOUS   */ { 200, 255, 190, LM_FLICKER, 4,  96 },
  /* SHOCKED   */ {   0,   0, 255, LM_STROBE,  9,   0 },
  /* KISSING   */ { 242, 220, 230, LM_HEART,   6, 224 },
  /* NERVOUS   */ {  72, 230, 180, LM_FLICKER, 7,  72 },
  /* GLITCHING */ { 128, 255, 230, LM_GLITCH,  9, 224 },
  /* SASSY     */ { 214, 255, 230, LM_CHASE,   7, 236 },
};

static const char *const ledModeNames[LEDS_NUM_MODES] = { "mood", "ambient", "off" };

class StripLeds {
public:
  void begin() {
    FastLED.addLeds<WS2812B, LED_STRIP_PIN, GRB>(strip, LED_STRIP_COUNT);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_MAX_MA);
    FastLED.setCorrection(TypicalLEDStrip);

    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    mode = p.getUChar("ledMode", LEDS_MOOD);
    bri  = p.getUChar("ledBri", LED_DEFAULT_BRI);
    p.end();
    if (mode >= LEDS_NUM_MODES) mode = LEDS_MOOD;

    FastLED.setBrightness(bri);
    fill_solid(strip, LED_STRIP_COUNT, CRGB::Black);
    FastLED.show();
    ambientStart = millis();
  }

  void setMode(uint8_t m) { if (m < LEDS_NUM_MODES) { mode = m; save(); } }
  void setBrightness(uint8_t b) { bri = b; FastLED.setBrightness(b); save(); }
  void setMood(uint8_t expr) { if (expr < BOT_NUM_EXPRESSIONS && expr != moodExpr) { moodExpr = expr; moodStart = millis(); } }
  void setBlink(float b) { blinkAmt = b; }
  void spark() { sparkStart = millis(); sparking = true; }

  uint8_t getMode() const { return mode; }
  uint8_t getBrightness() const { return bri; }
  const char *modeName() const { return ledModeNames[mode]; }

  // Call every loop pass; renders at most every LED_FRAME_MS
  void update() {
    unsigned long now = millis();
    if (now - lastTick < LED_FRAME_MS) return;
    lastTick = now;

    CRGB target[LED_STRIP_COUNT];
    if (mode == LEDS_MOOD)         renderMood(target, now);
    else if (mode == LEDS_AMBIENT) renderAmbient(target, now);
    else                           fill_solid(target, LED_STRIP_COUNT, CRGB::Black);

    if (sparking) renderSpark(target, now);

    // Ease toward the target: ~120 ms cross-fade, fast enough to keep strobes crisp
    for (uint8_t i = 0; i < LED_STRIP_COUNT; i++) nblend(strip[i], target[i], 96);
    FastLED.show();
  }

private:
  CRGB strip[LED_STRIP_COUNT];
  uint8_t mode = LEDS_MOOD, bri = LED_DEFAULT_BRI;
  uint8_t moodExpr = EXPR_NEUTRAL;
  float blinkAmt = 0;
  bool sparking = false;
  unsigned long lastTick = 0, moodStart = 0, sparkStart = 0, ambientStart = 0;

  void save() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar("ledMode", mode);
    p.putUChar("ledBri", bri);
    p.end();
  }

  // 0..255 phase that advances at `speed` (1..10) — about 0.2 to 2 cycles a second
  static uint8_t phase(unsigned long t, uint8_t speed) { return (uint8_t)((t * (uint32_t)speed * 13UL) >> 8); }

  void renderMood(CRGB *out, unsigned long now) {
    const LedMood &m = ledMoods[moodExpr];
    unsigned long t = now - moodStart;
    uint8_t ph = phase(t, m.speed);
    const uint8_t N = LED_STRIP_COUNT;

    switch (m.motion) {
      case LM_STEADY:
        fill_solid(out, N, CHSV(m.hue, m.sat, m.val));
        break;
      case LM_BREATHE: {
        uint8_t v = scale8(m.val, 90 + scale8(sin8(ph), 165));
        fill_solid(out, N, CHSV(m.hue, m.sat, v));
        break;
      }
      case LM_PULSE: {   // sharp attack, slow decay
        uint8_t d = 255 - ph;
        uint8_t v = scale8(m.val, 50 + scale8(scale8(d, d), 205));
        fill_solid(out, N, CHSV(m.hue, m.sat, v));
        break;
      }
      case LM_HEART: {   // lub-dub, rest
        uint8_t a = (ph < 40) ? sin8(ph * 3 + 192) : (ph >= 60 && ph < 100) ? sin8((ph - 60) * 3 + 192) : 0;
        uint8_t v = scale8(m.val, 50 + scale8(a, 205));
        fill_solid(out, N, CHSV(m.hue, m.sat, v));
        break;
      }
      case LM_WAVE:
        for (uint8_t i = 0; i < N; i++) {
          uint8_t s = sin8(ph + i * (256 / N));
          out[i] = CHSV(m.hue + scale8(s, 16), m.sat, scale8(m.val, 80 + scale8(s, 175)));
        }
        break;
      case LM_CHASE: {   // a bright head runs the strip with a fading tail
        uint16_t pos = ((uint16_t)ph * N) >> 8;
        for (uint8_t i = 0; i < N; i++) {
          uint8_t dist = (pos - i + N) % N;
          uint8_t v = dist == 0 ? 255 : dist == 1 ? 120 : dist == 2 ? 50 : 18;
          out[i] = CHSV(dist == 0 ? m.hue2 : m.hue, m.sat, scale8(m.val, v));
        }
        break;
      }
      case LM_SPARKLE:
        fill_solid(out, N, CHSV(m.hue, m.sat, scale8(m.val, 70)));
        for (uint8_t i = 0; i < N; i++)
          if (random8() < 6 + m.speed * 5) out[i] = CHSV(m.hue2, m.sat / 2, m.val);
        break;
      case LM_RAINBOW:
        for (uint8_t i = 0; i < N; i++) out[i] = CHSV(ph * 2 + i * (256 / N), m.sat, m.val);
        break;
      case LM_FLICKER:
        for (uint8_t i = 0; i < N; i++) {
          uint8_t v = scale8(m.val, 120 + random8(136));
          out[i] = CHSV(m.hue + random8(10), m.sat, random8() < 10 ? v / 4 : v);
        }
        break;
      case LM_STROBE:
        fill_solid(out, N, (ph & 0x40) ? CRGB::Black : CRGB(CHSV(m.hue, m.sat, m.val)));
        break;
      case LM_GLITCH:
        for (uint8_t i = 0; i < N; i++) {
          uint8_t r = random8();
          out[i] = r < 30 ? CRGB(CHSV(m.hue2, 255, m.val)) : r < 50 ? CRGB::Black
                          : CRGB(CHSV(m.hue, m.sat, scale8(m.val, 160)));
        }
        break;
      case LM_DUAL:      // two colours swap sides
        for (uint8_t i = 0; i < N; i++) {
          bool first = (i < N / 2) ^ (ph >= 128);
          out[i] = CHSV(first ? m.hue : m.hue2, m.sat, m.val);
        }
        break;
    }

    // Blinks dim the glow, so the body "blinks" with the eyes
    if (blinkAmt > 0.02f) {
      uint8_t keep = 255 - (uint8_t)(blinkAmt * 170.0f);
      for (uint8_t i = 0; i < N; i++) out[i].nscale8_video(keep);
    }
  }

  // Ambient show: a few patterns, each held LED_AMBIENT_HOLD_MS
  void renderAmbient(CRGB *out, unsigned long now) {
    const uint8_t N = LED_STRIP_COUNT;
    unsigned long t = now - ambientStart;
    uint8_t which = (t / LED_AMBIENT_HOLD_MS) % 4;
    uint8_t ph = phase(t, 2);

    switch (which) {
      case 0:   // rainbow drift
        for (uint8_t i = 0; i < N; i++) out[i] = CHSV(ph + i * 12, 240, 220);
        break;
      case 1: { // lava: two slow sine blobs of warm colour
        for (uint8_t i = 0; i < N; i++) {
          uint8_t a = sin8(ph + i * 40), b = sin8(ph * 2 - i * 25);
          out[i] = CHSV(250 + scale8(a, 40), 255, 60 + scale8(qadd8(a, b) / 2 + 64, 195));
        }
        break;
      }
      case 2: { // ocean: blue/teal swell
        for (uint8_t i = 0; i < N; i++) {
          uint8_t s = sin8(ph * 3 + i * 32);
          out[i] = CHSV(140 + scale8(s, 30), 230, 60 + scale8(s, 180));
        }
        break;
      }
      default: { // twinkle stars over a dim violet sky
        static CRGB stars[LED_STRIP_COUNT];
        for (uint8_t i = 0; i < N; i++) {
          stars[i].fadeToBlackBy(12);
          if (random8() < 4) stars[i] = CHSV(random8(), 120, 255);
          out[i] = CRGB(CHSV(200, 200, 30)) + stars[i];
        }
        break;
      }
    }
  }

  void renderSpark(CRGB *out, unsigned long now) {
    unsigned long el = now - sparkStart;
    if (el >= LED_SPARK_MS) { sparking = false; return; }
    uint8_t env = el < 200 ? el * 255 / 200 : (LED_SPARK_MS - el) * 255 / (LED_SPARK_MS - 200);
    uint8_t ph = (uint8_t)(el / 4);
    for (uint8_t i = 0; i < LED_STRIP_COUNT; i++)
      nblend(out[i], CRGB(CHSV(ph + i * 32, 255, 255)), env);
  }
};

StripLeds leds;

#endif // STRIP_LEDS_H
