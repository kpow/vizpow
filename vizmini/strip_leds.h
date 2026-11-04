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
//   LEDS_MOOD    — colour + motion follow the face's expression; blinks dim it.
//                  vizMini rarely changes face on his own, so every
//                  LED_MOOD_CYCLE_MS the mood swaps to a companion motion and
//                  its hue drifts, keeping the mood's colours but never still
//   LEDS_AMBIENT — independent light show: 8 patterns x 16 palettes (+ a
//                  custom one from three picked colours). Pattern and palette
//                  each either cycle on their own clocks (pace: chill / normal /
//                  fast) or stay locked on one; palettes cross-fade; speed 1-10
//   LEDS_SOLID   — one steady picked colour
//   LEDS_OFF
// Everything the web page sets is saved in NVS.
// A "spark" is a short rainbow burst laid over either mode.
//
// Each tick renders a target frame, then the shown frame eases toward it, so
// mood changes cross-fade instead of snapping.
// ============================================================================

// Off stays 2 so modes saved by older firmware keep their meaning
enum LedMode : uint8_t { LEDS_MOOD = 0, LEDS_AMBIENT, LEDS_OFF, LEDS_SOLID, LEDS_NUM_MODES };

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

static const char *const ledModeNames[LEDS_NUM_MODES] = { "mood", "ambient", "off", "solid" };

#define LED_PATTERNS   8
#define LED_PALETTES   17              // 16 built in + custom (c1..c3)
#define LED_PAL_CUSTOM 16
#define LED_AUTO       255             // pattern / palette selection: cycle
static const char *const ledPatternNames[LED_PATTERNS] = {
  "drift", "comet", "twinkle", "swell", "bounce", "mirror", "lava", "confetti"
};
static const char *const ledPaletteNames[LED_PALETTES] = {
  "rainbow", "lava", "ocean", "forest", "party", "heat", "sunset", "neon",
  "ice", "candy", "matrix", "fire", "aurora", "cyber", "pastel", "mono", "custom"
};
static const char *const ledPaceNames[3] = { "chill", "normal", "fast" };

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
    patSel = p.getUChar("ledPat", LED_AUTO);
    palSel = p.getUChar("ledPal", LED_AUTO);
    speed  = p.getUChar("ledSpd", 3);
    pace   = p.getUChar("ledPace", 1);
    c[0] = CRGB(p.getULong("ledC1", 0xFF2080));
    c[1] = CRGB(p.getULong("ledC2", 0x20C0FF));
    c[2] = CRGB(p.getULong("ledC3", 0xFFC000));
    p.end();
    if (mode >= LEDS_NUM_MODES) mode = LEDS_MOOD;
    if (patSel >= LED_PATTERNS) patSel = LED_AUTO;
    if (palSel >= LED_PALETTES) palSel = LED_AUTO;
    speed = constrain(speed, 1, 10);
    if (pace > 2) pace = 1;
    if (patSel != LED_AUTO) ambPat = patSel;
    if (palSel != LED_AUTO) palIdx = palSel;

    FastLED.setBrightness(bri);
    fill_solid(strip, LED_STRIP_COUNT, CRGB::Black);
    FastLED.show();
    ambientStart = millis();
    pal = palTarget = paletteN(palIdx);
  }

  void setMode(uint8_t m) { if (m < LEDS_NUM_MODES) { mode = m; save(); } }
  void setBrightness(uint8_t b) { bri = b; FastLED.setBrightness(b); save(); }
  void setMood(uint8_t expr) { if (expr < BOT_NUM_EXPRESSIONS && expr != moodExpr) { moodExpr = expr; moodStart = millis(); } }
  void setBlink(float b) { blinkAmt = b; }
  void spark() { sparkStart = millis(); sparking = true; }

  uint8_t getMode() const { return mode; }
  uint8_t getBrightness() const { return bri; }
  const char *modeName() const { return ledModeNames[mode]; }
  const char *patternName() const { return ledPatternNames[ambPat]; }
  const char *paletteName() const { return ledPaletteNames[palIdx]; }
  const char *patternSel() const { return patSel == LED_AUTO ? "auto" : ledPatternNames[patSel]; }
  const char *paletteSel() const { return palSel == LED_AUTO ? "auto" : ledPaletteNames[palSel]; }
  const char *paceName() const { return ledPaceNames[pace]; }
  uint8_t getSpeed() const { return speed; }
  String colorHex(int i) const { char b[7]; snprintf(b, sizeof(b), "%02X%02X%02X", c[i].r, c[i].g, c[i].b); return String(b); }

  // Web settings: a name ("auto" or one from the tables), and save
  void setPattern(const String &n) {
    if (n == "auto") patSel = LED_AUTO;
    else for (uint8_t i = 0; i < LED_PATTERNS; i++) if (n == ledPatternNames[i]) { patSel = i; ambPat = i; }
    nextPat = millis() + patternMs();
    save();
  }
  void setPalette(const String &n) {
    if (n == "auto") palSel = LED_AUTO;
    else for (uint8_t i = 0; i < LED_PALETTES; i++) if (n == ledPaletteNames[i]) { palSel = i; palIdx = i; palTarget = paletteN(i); }
    nextPal = millis() + paletteMs();
    save();
  }
  void setSpeed(int v) { speed = constrain(v, 1, 10); save(); }
  void setPace(const String &n) { for (uint8_t i = 0; i < 3; i++) if (n == ledPaceNames[i]) pace = i; nextPat = nextPal = 0; save(); }
  // i = 0..2, hex RRGGBB. c1 is the solid colour; c1..c3 build the custom palette
  void setColor(int i, const String &hex) {
    if (i < 0 || i > 2 || hex.length() < 6) return;
    c[i] = CRGB((uint32_t)strtoul(hex.c_str() + (hex[0] == '#'), nullptr, 16));
    if (palIdx == LED_PAL_CUSTOM) palTarget = paletteN(LED_PAL_CUSTOM);
    save();
  }

  // Call every loop pass; renders at most every LED_FRAME_MS
  void update() {
    unsigned long now = millis();
    if (now - lastTick < LED_FRAME_MS) return;
    lastTick = now;

    CRGB target[LED_STRIP_COUNT];
    if (mode == LEDS_MOOD)         renderMood(target, now);
    else if (mode == LEDS_AMBIENT) renderAmbient(target, now);
    else if (mode == LEDS_SOLID)   fill_solid(target, LED_STRIP_COUNT, c[0]);
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
  uint8_t patSel = LED_AUTO, palSel = LED_AUTO, speed = 3, pace = 1;
  CRGB c[3];
  float blinkAmt = 0;
  bool sparking = false;
  unsigned long lastTick = 0, moodStart = 0, sparkStart = 0, ambientStart = 0;

  void save() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar("ledMode", mode);
    p.putUChar("ledBri", bri);
    p.putUChar("ledPat", patSel);
    p.putUChar("ledPal", palSel);
    p.putUChar("ledSpd", speed);
    p.putUChar("ledPace", pace);
    for (int i = 0; i < 3; i++) {
      static const char *const k[3] = { "ledC1", "ledC2", "ledC3" };
      p.putULong(k[i], ((uint32_t)c[i].r << 16) | ((uint32_t)c[i].g << 8) | c[i].b);
    }
    p.end();
  }

  // 0..255 phase that advances at `speed` (1..10) — about 0.2 to 2 cycles a second
  static uint8_t phase(unsigned long t, uint8_t speed) { return (uint8_t)((t * (uint32_t)speed * 13UL) >> 8); }

  // The mood's own motion, then companions that suit its energy, swapped every LED_MOOD_CYCLE_MS
  void renderMood(CRGB *out, unsigned long now) {
    LedMood m = ledMoods[moodExpr];
    unsigned long t = now - moodStart;
    uint32_t step = t / LED_MOOD_CYCLE_MS;
    if (step % 3) {
      static const uint8_t calm[3] = { LM_WAVE, LM_CHASE, LM_BREATHE };
      static const uint8_t lively[4] = { LM_SPARKLE, LM_CHASE, LM_WAVE, LM_DUAL };
      bool hot = m.speed >= 5 || m.motion == LM_STROBE || m.motion == LM_GLITCH || m.motion == LM_RAINBOW;
      uint8_t pick = hot ? lively[(step + moodExpr) % 4] : calm[(step + moodExpr) % 3];
      if (pick == m.motion) pick = hot ? LM_WAVE : LM_CHASE;
      m.motion = pick;
      if (step & 1) { uint8_t h = m.hue; m.hue = m.hue2; m.hue2 = h; }   // the accent leads
      if (m.hue2 == m.hue) m.hue2 = m.hue + 24;
      if (m.speed < 3) m.speed = 3;
    }
    // A slow +-20 hue drift so even a steady mood moves through its colour family
    int16_t drift = ((int16_t)sin8((uint8_t)(now / 120)) - 128) * 20 / 128;
    m.hue += drift; m.hue2 += drift;
    renderMotion(m, out, t);

    // Blinks dim the glow, so the body "blinks" with the eyes
    if (blinkAmt > 0.02f) {
      uint8_t keep = 255 - (uint8_t)(blinkAmt * 170.0f);
      for (uint8_t i = 0; i < LED_STRIP_COUNT; i++) out[i].nscale8_video(keep);
    }
  }

  void renderMotion(const LedMood &m, CRGB *out, unsigned long t) {
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
  }

  // Ambient show: pattern and palette change on their own clocks, so the
  // combinations keep turning over; palettes blend into each other
  CRGBPalette16 pal, palTarget;
  uint8_t ambPat = 0, palIdx = 0;
  unsigned long nextPat = 0, nextPal = 0;

  uint32_t patternMs() const { static const uint16_t ms[3] = { 20000, 10000, 5000 }; return ms[pace]; }
  uint32_t paletteMs() const { static const uint16_t ms[3] = { 12000, 7000, 3500 }; return ms[pace]; }

  CRGBPalette16 paletteN(uint8_t i) const {
    switch (i) {
      case 0:  return RainbowColors_p;
      case 1:  return LavaColors_p;
      case 2:  return OceanColors_p;
      case 3:  return ForestColors_p;
      case 4:  return PartyColors_p;
      case 5:  return HeatColors_p;
      case 6:  return CRGBPalette16(CRGB(0xFF2800), CRGB(0xFF005A), CRGB(0x7800FF), CRGB(0xFF7800));   // sunset
      case 7:  return CRGBPalette16(CRGB(0x00FFDC), CRGB(0xFF00C8), CRGB(0x0050FF), CRGB(0xB400FF));   // neon
      case 8:  return CRGBPalette16(CRGB(0x001040), CRGB(0x0080FF), CRGB(0x80E0FF), CRGB(0xFFFFFF));   // ice
      case 9:  return CRGBPalette16(CRGB(0xFF40A0), CRGB(0xFFA0D0), CRGB(0x80C0FF), CRGB(0xC080FF));   // candy
      case 10: return CRGBPalette16(CRGB(0x001000), CRGB(0x00FF40), CRGB(0x004010), CRGB(0x80FF80));   // matrix
      case 11: return CRGBPalette16(CRGB(0x200000), CRGB(0xFF2000), CRGB(0xFF8000), CRGB(0xFFD000));   // fire
      case 12: return CRGBPalette16(CRGB(0x00FF80), CRGB(0x00A0FF), CRGB(0x8000FF), CRGB(0x00FFC0));   // aurora
      case 13: return CRGBPalette16(CRGB(0xFF00FF), CRGB(0x00FFFF), CRGB(0xFFFF00), CRGB(0xFF0080));   // cyber
      case 14: return CRGBPalette16(CRGB(0xFFB0B0), CRGB(0xB0FFB0), CRGB(0xB0B0FF), CRGB(0xFFFFB0));   // pastel
      case 15: return CRGBPalette16(CRGB(0x202020), CRGB(0xFFFFFF), CRGB(0x606060), CRGB(0xFFFFFF));   // mono
      default: return CRGBPalette16(c[0], c[1], c[2], c[0]);                                           // custom
    }
  }

  void renderAmbient(CRGB *out, unsigned long now) {
    const uint8_t N = LED_STRIP_COUNT;
    if (patSel == LED_AUTO && (long)(now - nextPat) >= 0) {
      uint8_t p = random8(LED_PATTERNS);
      ambPat = p == ambPat ? (p + 1) % LED_PATTERNS : p;
      nextPat = now + patternMs() + random16(patternMs() / 3);
    }
    if (palSel == LED_AUTO && (long)(now - nextPal) >= 0) {   // auto skips custom and mono
      uint8_t q = random8(LED_PALETTES - 2);
      palIdx = q == palIdx ? (q + 1) % (LED_PALETTES - 2) : q;
      palTarget = paletteN(palIdx);
      nextPal = now + paletteMs() + random16(paletteMs() / 3);
    }
    nblendPaletteTowardPalette(pal, palTarget, 24);

    unsigned long t = now - ambientStart;
    uint8_t ph = phase(t, speed);
    switch (ambPat) {
      case 0:   // drift: the palette slides along the strip
        for (uint8_t i = 0; i < N; i++) out[i] = ColorFromPalette(pal, ph * 2 + i * 22, 230);
        break;
      case 1: { // comet: a bright head with a palette tail
        uint16_t pos = ((uint16_t)(ph * 2) * N) >> 8;
        for (uint8_t i = 0; i < N; i++) {
          uint8_t d = (pos - i + N) % N;
          out[i] = ColorFromPalette(pal, ph + d * 18, d == 0 ? 255 : d == 1 ? 140 : d == 2 ? 60 : 12);
        }
        break;
      }
      case 2: { // twinkle: palette stars over a dim palette sky
        static CRGB stars[LED_STRIP_COUNT];
        for (uint8_t i = 0; i < N; i++) {
          stars[i].fadeToBlackBy(18);
          if (random8() < 10) stars[i] = ColorFromPalette(pal, random8(), 255);
          out[i] = ColorFromPalette(pal, ph + i * 8, 28) + stars[i];
        }
        break;
      }
      case 3: { // swell: the whole body breathes through the palette
        uint8_t v = 50 + scale8(sin8(ph * 3), 205);
        fill_solid(out, N, ColorFromPalette(pal, ph, v));
        break;
      }
      case 4: { // bounce: a ping-pong dot with a short trail
        uint8_t tri = triwave8(ph * 3);
        uint8_t pos = scale8(tri, N - 1);
        for (uint8_t i = 0; i < N; i++) {
          uint8_t d = abs((int)i - (int)pos);
          out[i] = ColorFromPalette(pal, ph * 2 + i * 10, d == 0 ? 255 : d == 1 ? 90 : 10);
        }
        break;
      }
      case 5: { // mirror: waves ripple out from the middle
        for (uint8_t i = 0; i < N; i++) {
          uint8_t d = i < N / 2 ? N / 2 - 1 - i : i - N / 2;
          uint8_t sw = sin8(ph * 4 - d * 48);
          out[i] = ColorFromPalette(pal, ph + d * 30, 40 + scale8(sw, 215));
        }
        break;
      }
      case 6: { // lava: two slow blobs
        for (uint8_t i = 0; i < N; i++) {
          uint8_t a = sin8(ph * 2 + i * 40), b = sin8(ph * 3 - i * 25);
          out[i] = ColorFromPalette(pal, qadd8(a / 2, b / 2), 60 + scale8(qadd8(a / 2, b / 2), 195));
        }
        break;
      }
      default: { // confetti: fast palette sparks
        static CRGB c[LED_STRIP_COUNT];
        for (uint8_t i = 0; i < N; i++) {
          c[i].fadeToBlackBy(40);
          if (random8() < 40) c[i] = ColorFromPalette(pal, random8(), 255);
          out[i] = c[i];
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
