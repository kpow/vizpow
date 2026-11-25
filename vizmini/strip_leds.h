#ifndef STRIP_LEDS_H
#define STRIP_LEDS_H

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "config.h"
#include "bot_faces.h"   // EXPR_* ids

// ============================================================================
// Inside lights — 12× WS2812B on GPIO4: a run of 4 down each side, 4 along the bottom
// ============================================================================
// Only the strip is driven. The C3 has two RMT TX channels and FastLED gives
// each strip two memory blocks there (FastLED #1846: one block flickers when
// WiFi interrupts delay the refill), so one strip uses up the RMT. The
// onboard NeoPixel on GPIO8 stays dark; the strip replaces it.
// The body is translucent, so these are a primary feature, not a status LED.
//
// Layout: the runs are wired right side, left side, bottom (as you face him),
// but every pattern draws along one path shaped like a U: down his left side
// (your left), across the pants, up the right side. Path slot u = 0..11; the
// LED_*_FIRST flags in config.h map it onto the wiring. Height (top 0 .. bottom
// row LED_RUN) and distance from the bottom centre let waves rise up both
// sides and ripples mirror left to right.
// Modes (web-switchable, saved in NVS):
//   LEDS_MOOD    — colour + motion follow the face's expression; blinks dim it.
//                  vizMini rarely changes face on his own, so every
//                  LED_MOOD_CYCLE_MS the mood swaps to a companion motion and
//                  its hue drifts, keeping the mood's colours but never still
//   LEDS_AMBIENT — independent light show: 10 patterns x 16 palettes (+ a
//                  custom one from three picked colours). Pattern and palette
//                  each either cycle on their own clocks (pace: chill / normal /
//                  fast) or stay locked on one; palettes cross-fade; speed 1-10
//   LEDS_SOLID   — one steady picked colour
//   LEDS_OFF
// Everything the web page sets is saved in NVS.
// A "spark" is a short rainbow burst laid over either mode.
//
// Body map: the strip is his body, so the newer motions are drawn per body
// part, in wiring order (LED n = strip[n-1]): 1 his left hand, 2-3 that arm,
// 4 + 5 his head, 6-7 the other arm, 8 his right hand, 9 + 12 legs, 10 + 11
// belly. Every LED stays at least LED_BODY_FLOOR lit, so a pattern lights a
// part up over a glowing body instead of leaving the rest dark.
//
// Each tick renders a target frame, then the shown frame eases toward it, so
// mood changes cross-fade instead of snapping.
// ============================================================================

// Off stays 2 so modes saved by older firmware keep their meaning
enum LedMode : uint8_t { LEDS_MOOD = 0, LEDS_AMBIENT, LEDS_OFF, LEDS_SOLID, LEDS_NUM_MODES };

enum LedMotion : uint8_t {
  LM_STEADY, LM_BREATHE, LM_PULSE, LM_HEART, LM_WAVE, LM_CHASE,
  LM_SPARKLE, LM_RAINBOW, LM_FLICKER, LM_STROBE, LM_GLITCH, LM_DUAL,
  // Body motions (see the body map above); these stay put instead of swapping to companions
  LM_BRAIN,    // head glows (thinking)
  LM_HELLO,    // his right hand waves (happy)
  LM_HEARTB,   // lub-dub in the belly, beats climb the arms (love)
  LM_FISTS,    // hot hands, heat up the arms, steam puffs from the head (angry)
  LM_JOLT,     // a flash from the belly out to head, hands and feet (surprised)
  LM_DRAIN,    // glow drips from the head down to the belly (sad)
  LM_JAZZ,     // colour-flashing hands (excited)
  LM_BODYBREATH // breath fills from the feet up to the head and back (neutral)
};
#define LM_BODY_FIRST LM_BRAIN

struct LedMood {
  uint8_t hue, sat, val;   // base colour (FastLED hue: 0 red, 64 yellow, 96 green, 160 blue, 224 pink)
  uint8_t motion;          // LedMotion
  uint8_t speed;           // 1 = slow .. 10 = fast
  uint8_t hue2;            // second colour for LM_DUAL / LM_SPARKLE accents
};

// One row per expression, in EXPR_* order
static const LedMood ledMoods[BOT_NUM_EXPRESSIONS] = {
  /* NEUTRAL   */ { 130, 220, 150, LM_BODYBREATH, 2, 130 },
  /* HAPPY     */ {  42, 240, 230, LM_HELLO,      4,  64 },
  /* SAD       */ { 160, 255,  90, LM_DRAIN,      1, 170 },
  /* SURPRISED */ {   0,   0, 255, LM_JOLT,       8,   0 },
  /* CHILL     */ { 118, 230, 140, LM_WAVE,       1, 150 },
  /* ANGRY     */ {   0, 255, 255, LM_FISTS,      8,  10 },
  /* LOVE      */ { 236, 230, 230, LM_HEARTB,     5, 250 },
  /* DIZZY     */ {   0, 255, 220, LM_RAINBOW,    9,   0 },
  /* THINKING  */ { 190, 230, 170, LM_BRAIN,      3, 160 },
  /* EXCITED   */ {  20, 255, 255, LM_JAZZ,       9,  64 },
  /* MISCHIEF  */ {  90, 255, 200, LM_SPARKLE,    5, 200 },
  /* SKEPTICAL */ {  30, 230, 120, LM_STEADY,     1,  30 },
  /* WORRIED   */ { 180, 160, 140, LM_DRAIN,      3, 160 },
  /* CONFUSED  */ { 210, 220, 180, LM_DUAL,       3, 120 },
  /* PROUD     */ {  48, 220, 240, LM_SPARKLE,    3,  40 },
  /* SHY       */ { 232, 150, 100, LM_BREATHE,    2, 232 },
  /* ANNOYED   */ {  12, 255, 210, LM_FISTS,      5,  12 },
  /* FOCUSED   */ { 150,  90, 200, LM_BRAIN,      1, 150 },
  /* WINKING   */ {  56, 240, 220, LM_HELLO,      5, 240 },
  /* DEVIOUS   */ { 200, 255, 190, LM_FLICKER,    4,  96 },
  /* SHOCKED   */ {   0,   0, 255, LM_JOLT,       9,   0 },
  /* KISSING   */ { 242, 220, 230, LM_HEARTB,     6, 224 },
  /* NERVOUS   */ {  72, 230, 180, LM_FLICKER,    7,  72 },
  /* GLITCHING */ { 128, 255, 230, LM_GLITCH,     9, 224 },
  /* SASSY     */ { 214, 255, 230, LM_CHASE,      7, 236 },
};

// The classic (whole-strip) motion for each mood, in EXPR_* order. A body
// mood shows it now and then: one LED_MOOD_CYCLE_MS turn in LED_CLASSIC_EVERY
static const uint8_t ledMoodClassic[BOT_NUM_EXPRESSIONS] = {
  LM_BREATHE, LM_WAVE, LM_BREATHE, LM_PULSE, LM_WAVE, LM_FLICKER, LM_HEART, LM_RAINBOW,
  LM_CHASE, LM_SPARKLE, LM_SPARKLE, LM_STEADY, LM_FLICKER, LM_DUAL, LM_SPARKLE, LM_BREATHE,
  LM_PULSE, LM_STEADY, LM_DUAL, LM_FLICKER, LM_STROBE, LM_HEART, LM_FLICKER, LM_GLITCH, LM_CHASE
};

static const char *const ledModeNames[LEDS_NUM_MODES] = { "mood", "ambient", "off", "solid" };

#define LED_PATTERNS   15
#define LED_PALETTES   17              // 16 built in + custom (c1..c3)
#define LED_PAL_CUSTOM 16
#define LED_AUTO       255             // pattern / palette selection: cycle
#define LED_BODY_PAT   10              // patterns from here on are the body ones
static const char *const ledPatternNames[LED_PATTERNS] = {
  "drift", "comet", "twinkle", "swell", "bounce", "mirror", "lava", "confetti", "rise", "sides",
  "walk", "juggle", "charge", "clap", "halo"            // body patterns (see the body map)
};
static const char *const ledPaletteNames[LED_PALETTES] = {
  "rainbow", "lava", "ocean", "forest", "party", "heat", "sunset", "neon",
  "ice", "candy", "matrix", "fire", "aurora", "cyber", "pastel", "mono", "custom"
};
static const char *const ledPaceNames[3] = { "chill", "normal", "fast" };

// Body geometry, in wiring order (index = LED number - 1)
static const int8_t  bodyArm[LED_STRIP_COUNT]  = { 0, 1, 2, 3, 3, 2, 1, 0, -1, -1, -1, -1 };  // hand 0 .. head 3
static const uint8_t bodyUp[LED_STRIP_COUNT]   = { 1, 2, 3, 4, 4, 3, 2, 1,  0,  0,  0,  0 };  // feet 0 .. head 4
static const uint8_t bodyOut[LED_STRIP_COUNT]  = { 2, 3, 4, 5, 5, 4, 3, 2,  1,  0,  0,  1 };  // belly 0 .. head 5
static const uint8_t bodyDown[LED_STRIP_COUNT] = { 3, 2, 1, 0, 0, 1, 2, 3,  4,  5,  5,  4 };  // head 0 .. belly 5
enum : uint8_t { B_HAND_L = 0, B_HEAD_A = 3, B_HEAD_B = 4, B_HAND_R = 7, B_LEG_R = 8, B_BELLY_A = 9, B_BELLY_B = 10, B_LEG_L = 11 };

// One body frame: per LED, how lit (v, 0..1 over the floor), how far toward the
// accent colour (k), a hue shift (hs) and how white (w)
struct BodyFrame {
  float v[LED_STRIP_COUNT];
  uint8_t k[LED_STRIP_COUNT], hs[LED_STRIP_COUNT], w[LED_STRIP_COUNT];
  void clear(float v0 = 0) { for (uint8_t i = 0; i < LED_STRIP_COUNT; i++) { v[i] = v0; k[i] = hs[i] = w[i] = 0; } }
  void set(uint8_t i, float val, uint8_t kk) { v[i] = val; k[i] = kk; }
};
static inline float bodyClamp(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static inline float bodyBump(float x, float w) { return expf(-(x * x) / w); }   // 1 at x = 0
static inline float bodyRand() { return random8() / 255.0f; }

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
  // Set by the sleep owner (vizmini.ino): dark without touching the saved mode.
  // identify() still shows, so a sleeping bot can be found.
  bool asleep = false;
  // A single click: the ambient show jumps to a new pattern and palette now.
  // A locked choice moves to the new one for this session; the saved lock stays
  void shuffle() {
    ambPat = pickPattern(ambPat);
    uint8_t q = random8(LED_PALETTES - 2);                   // like auto: skips custom and mono
    palIdx = q == palIdx ? (q + 1) % (LED_PALETTES - 2) : q;
    palTarget = paletteN(palIdx);
    if (patSel != LED_AUTO) patSel = ambPat;
    if (palSel != LED_AUTO) palSel = palIdx;
    unsigned long now = millis();
    nextPat = now + patternMs();
    nextPal = now + paletteMs();
  }
  // Wiring check for a few seconds: "runs" lights each run's first LED white,
  // the rest red (right), green (left), blue (bottom); "path" walks one dot
  // along the U, down your left side, across the bottom, up the right
  void identify(bool path, uint32_t ms) { idPath = path; idUntil = millis() + ms; }

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
    if (asleep)                    fill_solid(target, LED_STRIP_COUNT, CRGB::Black);   // fades out
    else if (mode == LEDS_MOOD)    renderMood(target, now);
    else if (mode == LEDS_AMBIENT) renderAmbient(target, now);
    else if (mode == LEDS_SOLID)   fill_solid(target, LED_STRIP_COUNT, c[0]);
    else                           fill_solid(target, LED_STRIP_COUNT, CRGB::Black);

    if (sparking && !asleep) renderSpark(target, now);
    if ((long)(idUntil - now) > 0) { renderIdentify(now); FastLED.show(); return; }

    // Ease toward the target: ~120 ms cross-fade, fast enough to keep strobes crisp.
    // target is in path order; the strip is in wiring order
    for (uint8_t u = 0; u < LED_STRIP_COUNT; u++) nblend(strip[physOf(u)], target[u], 96);
    FastLED.show();
  }

private:
  CRGB strip[LED_STRIP_COUNT];
  uint8_t mode = LEDS_MOOD, bri = LED_DEFAULT_BRI;
  uint8_t moodExpr = EXPR_NEUTRAL;
  uint8_t patSel = LED_AUTO, palSel = LED_AUTO, speed = 3, pace = 1;
  CRGB c[3];
  float blinkAmt = 0;
  bool sparking = false, idPath = false;
  unsigned long lastTick = 0, moodStart = 0, sparkStart = 0, ambientStart = 0, idUntil = 0;

  // ---- Layout: path slot u -> wiring index, and the U's geometry ----
  static uint8_t physOf(uint8_t u) {
    const uint8_t R = LED_RUN;
    if (u < R) return R + (LED_LEFT_TOP_FIRST ? u : R - 1 - u);                        // left, top -> bottom
    if (u < 2 * R) { uint8_t k = u - R; return 2 * R + (LED_BOTTOM_LEFT_FIRST ? k : R - 1 - k); }   // bottom, left -> right
    uint8_t k = u - 2 * R;                                                               // right, bottom -> top
    return LED_RIGHT_TOP_FIRST ? R - 1 - k : k;
  }
  // 0 at the top of a side .. LED_RUN for the bottom row
  static uint8_t height(uint8_t u) { return u < LED_RUN ? u : u < 2 * LED_RUN ? LED_RUN : 3 * LED_RUN - 1 - u; }
  // Steps from the bottom centre along the U: 0 for the middle two .. 5 at the tops
  static uint8_t fromMid(uint8_t u) { return abs(2 * (int)u - (LED_STRIP_COUNT - 1)) / 2; }

  void renderIdentify(unsigned long now) {
    if (idPath) {
      uint8_t pos = (now / 400) % LED_STRIP_COUNT;
      for (uint8_t u = 0; u < LED_STRIP_COUNT; u++)
        strip[physOf(u)] = u == pos ? CRGB::White : u < pos ? CRGB(0, 0, 24) : CRGB::Black;
      return;
    }
    static const CRGB runCol[3] = { CRGB(90, 0, 0), CRGB(0, 90, 0), CRGB(0, 0, 90) };
    for (uint8_t i = 0; i < LED_STRIP_COUNT; i++)
      strip[i] = i % LED_RUN == 0 ? CRGB::White : runCol[i / LED_RUN];
  }

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
    if (m.motion >= LM_BODY_FIRST && step % LED_CLASSIC_EVERY == LED_CLASSIC_EVERY - 1) {
      m.motion = ledMoodClassic[moodExpr];                 // now and then, the classic look
    } else if (m.motion >= LM_BODY_FIRST) {
      // A body motion is the point of the mood, so it stays; the colours still trade the lead
      if (abs((int)m.hue2 - (int)m.hue) < 16) m.hue2 = m.hue + 32;
      if (step & 1) { uint8_t h = m.hue; m.hue = m.hue2; m.hue2 = h; }
    } else if (step % 3) {
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
    // The whole body stays lit: nothing drops below the floor in the mood's colour
    CRGB floorC = CHSV(m.hue, m.sat, scale8(m.val, LED_BODY_FLOOR));
    for (uint8_t i = 0; i < LED_STRIP_COUNT; i++) out[i] |= floorC;

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
      case LM_PULSE:     // sharp attack, slow decay, rippling out from the bottom centre
        for (uint8_t i = 0; i < N; i++) {
          uint8_t d = 255 - (uint8_t)(ph - fromMid(i) * 6);
          out[i] = CHSV(m.hue, m.sat, scale8(m.val, 50 + scale8(scale8(d, d), 205)));
        }
        break;
      case LM_HEART:     // lub-dub, rest; each beat climbs from the pants up both sides
        for (uint8_t i = 0; i < N; i++) {
          uint8_t p = ph - (LED_RUN - height(i)) * 5;
          uint8_t a = (p < 40) ? sin8(p * 3 + 192) : (p >= 60 && p < 100) ? sin8((p - 60) * 3 + 192) : 0;
          out[i] = CHSV(m.hue, m.sat, scale8(m.val, 50 + scale8(a, 205)));
        }
        break;
      case LM_WAVE:      // swells roll up both sides from the bottom
        for (uint8_t i = 0; i < N; i++) {
          uint8_t s = sin8(ph - fromMid(i) * 40);
          out[i] = CHSV(m.hue + scale8(s, 16), m.sat, scale8(m.val, 80 + scale8(s, 175)));
        }
        break;
      case LM_CHASE: {   // a bright head runs round the U with a fading tail
        uint16_t pos = ((uint16_t)ph * N) >> 8;
        for (uint8_t i = 0; i < N; i++) {
          uint8_t dist = (pos - i + N) % N;
          uint8_t v = dist == 0 ? 255 : dist == 1 ? 140 : dist == 2 ? 70 : dist == 3 ? 30 : 12;
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
      case LM_DUAL:      // two colours swap sides; the bottom splits down the middle
        for (uint8_t i = 0; i < N; i++) {
          bool first = (i < N / 2) ^ (ph >= 128);
          out[i] = CHSV(first ? m.hue : m.hue2, m.sat, m.val);
        }
        break;
      default: {         // body motions
        BodyFrame f;
        bodyMood(m.motion, f, t / 1000.0f);
        for (uint8_t u = 0; u < N; u++) {
          uint8_t i = physOf(u);
          uint8_t bri = scale8(m.val, LED_BODY_FLOOR + scale8((uint8_t)(f.v[i] * 255), 255 - LED_BODY_FLOOR));
          CRGB c = blend(CRGB(CHSV(m.hue + f.hs[i], m.sat, bri)), CRGB(CHSV(m.hue2 + f.hs[i], m.sat, bri)), f.k[i]);
          if (f.w[i]) c = blend(c, CRGB(bri, bri, bri), f.w[i]);
          out[u] = c;
        }
        break;
      }
    }
  }

  // ---- Body motions (mood), drawn in wiring order; ts in seconds ----
  static void bodyMood(uint8_t motion, BodyFrame &f, float ts) {
    const uint8_t N = LED_STRIP_COUNT;
    switch (motion) {
      case LM_BRAIN: {               // head glows, the shoulders catch a little of it
        f.clear();
        float s = sinf(ts * 2.2f);
        f.set(B_HEAD_A, 0.55f + 0.45f * s, 255); f.set(B_HEAD_B, 0.55f + 0.45f * s, 255);
        float sh = 0.18f + 0.12f * sinf(ts * 2.2f - 0.7f);
        f.set(2, sh, 140); f.set(5, sh, 140);
        break;
      }
      case LM_HELLO: {               // his right hand waves: three flicks, a rest
        f.clear(0.15f);
        float u = fmodf(ts, 2.4f);
        float w = u < 1.2f ? 0.3f + 0.7f * fabsf(sinf(u * PI * 2.5f)) : 0.35f;
        f.set(B_HAND_R, w, 255); f.set(6, 0.4f * w, 160); f.set(B_HAND_L, 0.3f, 80);
        break;
      }
      case LM_HEARTB: {              // lub-dub in the belly; each beat climbs the arms
        f.clear();
        auto beat = [](float p) { return max(bodyBump(p - 0.05f, 0.002f), 0.7f * bodyBump(p - 0.24f, 0.002f)); };
        float p = fmodf(ts, 1.2f) / 1.2f;
        float b = 0.25f + 0.75f * beat(p);
        f.set(B_BELLY_A, b, 255); f.set(B_BELLY_B, b, 255);
        f.set(B_LEG_R, 0.6f * b, 200); f.set(B_LEG_L, 0.6f * b, 200);
        for (uint8_t i = 0; i < 8; i++) {
          float v = (0.55f - 0.12f * bodyArm[i]) * beat(p - 0.08f * (bodyArm[i] + 1));
          f.set(i, v, (uint8_t)(v * 255));
        }
        break;
      }
      case LM_FISTS: {               // hot hands, heat up the arms, steam puffs off the head
        f.clear(0.1f);
        f.set(B_HAND_L, 0.7f + 0.3f * bodyRand(), 255); f.set(B_HAND_R, 0.7f + 0.3f * bodyRand(), 255);
        f.hs[B_HAND_L] = random8(8); f.hs[B_HAND_R] = random8(8);
        f.set(1, 0.4f + 0.2f * bodyRand(), 180); f.set(6, 0.4f + 0.2f * bodyRand(), 180);
        f.set(2, 0.22f, 100); f.set(5, 0.22f, 100);
        float puff = expf(-fmodf(ts, 2.2f) / 0.25f);
        for (uint8_t i : { B_HEAD_A, B_HEAD_B }) { f.set(i, 0.1f + 0.9f * puff, 60); f.w[i] = (uint8_t)(puff * 180); }
        break;
      }
      case LM_JOLT: {                // a white flash from the belly out to every tip; the tips hold it
        f.clear();
        float u = fmodf(ts, 2.6f);
        for (uint8_t i = 0; i < N; i++) {
          float d = bodyOut[i];
          float v = bodyBump(u * 7 - d, 0.8f);
          if (d >= 2 && bodyArm[i] == 0 && u * 7 > d) v = max(v, 0.6f * expf(-(u - d / 7) * 2.5f));   // hands
          if (d == 5 && u * 7 > d) v = max(v, 0.8f * expf(-(u - d / 7) * 1.8f));                       // head
          f.v[i] = v; f.w[i] = (uint8_t)(v * 220);
        }
        break;
      }
      case LM_DRAIN: {               // glow drips from the head, down the arms, into the belly
        f.clear();
        float pos = fmodf(ts, 4.5f) / 4.5f * 7 - 0.5f;
        for (uint8_t i = 0; i < N; i++) {
          float d = pos - bodyDown[i];
          float v = max(0.85f * bodyBump(d, 0.5f), d > 0 ? 0.25f * expf(-d * 1.2f) : 0.0f);
          f.set(i, v, (uint8_t)(v * 255));
        }
        break;
      }
      case LM_JAZZ:                  // hands flash through colours, the arms catch sparks
        f.clear(0.25f);
        for (uint8_t i : { B_HAND_L, B_HAND_R }) { f.set(i, random8() > 100 ? 1 : 0.35f, 255); f.hs[i] = random8(); }
        for (uint8_t i : { 1, 6 }) if (random8() < 50) { f.set(i, 0.7f, 255); f.hs[i] = random8(); }
        break;
      default: {                     // LM_BODYBREATH: breath fills from the feet to the head and back
        f.clear();
        float fill = (1 - cosf(2 * PI * ts / 5)) / 2 * 5;
        for (uint8_t i = 0; i < N; i++) { float v = 0.9f * bodyClamp(fill - bodyUp[i]); f.set(i, v, (uint8_t)(v * 200)); }
        break;
      }
    }
  }

  // ---- Body patterns (ambient), wiring order; ts in seconds at speed 3 = 1x ----
  static void bodyAmbient(uint8_t pat, BodyFrame &f, float ts) {
    const uint8_t N = LED_STRIP_COUNT;
    switch (pat) {
      case 10: {                     // walk: legs step, the opposite hand swings with each leg
        f.clear(0.1f);
        float ph = sinf(2 * PI * ts / 0.9f);
        float a = max(0.0f, ph), b = max(0.0f, -ph);
        f.set(B_LEG_R, a, (uint8_t)(a * 255)); f.set(B_HAND_L, a, (uint8_t)(a * 255)); f.set(1, 0.5f * a, 100);
        f.set(B_LEG_L, b, (uint8_t)(b * 255)); f.set(B_HAND_R, b, (uint8_t)(b * 255)); f.set(6, 0.5f * b, 100);
        f.set(B_HEAD_A, 0.3f + 0.2f * fabsf(ph), 60); f.set(B_HEAD_B, 0.3f + 0.2f * fabsf(ph), 60);
        break;
      }
      case 11: {                     // juggle: hand, up over the head (hangs at the top), catch, roll back under
        const float T = 2.5f;
        float u = fmodf(ts, T), p, katch = 0;
        uint8_t lap = (uint8_t)(ts / T);
        if (u < 1.3f)       { float x = 2 * u / 1.3f - 1; p = 3.5f + 3.5f * x * x * x; }
        else if (u < 1.55f) { p = 7; katch = 1 - (u - 1.3f) / 0.25f; }
        else                p = 7 + (u - 1.55f) / 0.95f * 5;
        f.clear();
        for (uint8_t i = 0; i < N; i++) {
          float d = fabsf(i - p); d = min(d, N - d);
          float v = bodyBump(d, 0.45f);
          f.set(i, v, (uint8_t)(v * 255)); f.hs[i] = lap * 50;
        }
        f.v[B_HAND_R] = max(f.v[B_HAND_R], katch);
        break;
      }
      case 12: {                     // charge: fills from the feet to the head, flashes, lets go
        float u = fmodf(ts, 3.2f);
        f.clear();
        if (u < 2.2f) { float fill = u / 2.2f * 5; for (uint8_t i = 0; i < N; i++) f.set(i, 0.7f * bodyClamp(fill - bodyUp[i]), bodyUp[i] * 50); }
        else if (u < 2.6f) { f.clear(0.85f); for (uint8_t i : { B_HEAD_A, B_HEAD_B }) { f.v[i] = 1; f.w[i] = 255; } }
        else f.clear(0.8f * (1 - (u - 2.6f) / 0.6f));
        break;
      }
      case 13: {                     // clap: two claps at the hands, each rings up the arms to the head
        f.clear();
        float u = fmodf(ts, 1.8f);
        for (float c : { u, u - 0.32f }) {
          if (c < 0) continue;
          for (uint8_t i = 0; i < 8; i++) {
            float v = bodyBump(c * 6 - bodyArm[i], 0.35f) * (bodyArm[i] == 0 ? 1 : 0.8f);
            if (bodyArm[i] == 3 && c * 6 > 3) v = max(v, expf(-(c - 0.5f) * 2));
            if (v > f.v[i]) { f.v[i] = v; f.k[i] = (uint8_t)(v * 255); }
          }
        }
        for (uint8_t i : { B_HAND_L, B_HAND_R }) f.w[i] = (uint8_t)(f.v[i] * 120);
        break;
      }
      default: {                     // halo: a bright head over a slow drifting body
        f.clear();
        for (uint8_t i = 0; i < N; i++) f.hs[i] = (uint8_t)(int)(40 * sinf(ts * 0.3f + i * 0.5f));
        for (uint8_t i : { B_HEAD_A, B_HEAD_B }) { f.set(i, 0.88f + 0.08f * sinf(ts * 1.3f), 255); f.w[i] = 120; }
        f.set(2, 0.3f, 120); f.set(5, 0.3f, 120);
        break;
      }
    }
  }

  // Ambient show: pattern and palette change on their own clocks, so the
  // combinations keep turning over; palettes blend into each other
  CRGBPalette16 pal, palTarget;
  uint8_t ambPat = 0, palIdx = 0;
  unsigned long nextPat = 0, nextPal = 0;

  // Auto's next pattern: mostly a body one, a classic only LED_CLASSIC_PCT of the time
  static uint8_t pickPattern(uint8_t cur) {
    for (;;) {
      uint32_t r = esp_random();                       // hardware RNG, independent of the per-frame random8() draws
      uint8_t p = r % 100 < LED_CLASSIC_PCT ? (r >> 8) % LED_BODY_PAT
                                            : LED_BODY_PAT + (r >> 8) % (LED_PATTERNS - LED_BODY_PAT);
      if (p != cur) return p;
    }
  }

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
      ambPat = pickPattern(ambPat);
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
      case 0:   // drift: the palette slides round the U
        for (uint8_t i = 0; i < N; i++) out[i] = ColorFromPalette(pal, ph * 2 + i * (256 / N), 230);
        break;
      case 1: { // comet: a bright head with a palette tail, round the U
        uint16_t pos = ((uint16_t)(ph * 2) * N) >> 8;
        for (uint8_t i = 0; i < N; i++) {
          uint8_t d = (pos - i + N) % N;
          out[i] = ColorFromPalette(pal, ph + d * 18, d == 0 ? 255 : d == 1 ? 150 : d == 2 ? 80 : d == 3 ? 30 : 8);
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
      case 3:   // swell: the body breathes through the palette, filling from the pants up
        for (uint8_t i = 0; i < N; i++)
          out[i] = ColorFromPalette(pal, ph, 50 + scale8(sin8(ph * 3 - (LED_RUN - height(i)) * 14), 205));
        break;
      case 4: { // bounce: a ping-pong dot from top left, round the bottom, to top right
        uint8_t tri = triwave8(ph * 3);
        uint8_t pos = ((uint16_t)tri * N) >> 8;
        for (uint8_t i = 0; i < N; i++) {
          uint8_t d = abs((int)i - (int)pos);
          out[i] = ColorFromPalette(pal, ph * 2 + i * 10, d == 0 ? 255 : d == 1 ? 90 : 10);
        }
        break;
      }
      case 5: { // mirror: ripples run out from the bottom centre and up both sides
        for (uint8_t i = 0; i < N; i++) {
          uint8_t d = fromMid(i);
          uint8_t sw = sin8(ph * 4 - d * 40);
          out[i] = ColorFromPalette(pal, ph + d * 24, 40 + scale8(sw, 215));
        }
        break;
      }
      case 6: { // lava: slow blobs that rise up the sides
        for (uint8_t i = 0; i < N; i++) {
          uint8_t a = sin8(ph * 2 + height(i) * 50), b = sin8(ph * 3 - i * 25);
          out[i] = ColorFromPalette(pal, qadd8(a / 2, b / 2), 60 + scale8(qadd8(a / 2, b / 2), 195));
        }
        break;
      }
      case 7: { // confetti: fast palette sparks
        static CRGB c[LED_STRIP_COUNT];
        for (uint8_t i = 0; i < N; i++) {
          c[i].fadeToBlackBy(40);
          if (random8() < 40) c[i] = ColorFromPalette(pal, random8(), 255);
          out[i] = c[i];
        }
        break;
      }
      case 8: { // rise: a flickering glow burns in the pants and climbs, fading, up both sides
        static uint8_t heat[LED_STRIP_COUNT];
        for (uint8_t i = 0; i < N; i++) {
          uint8_t h = height(i);
          uint8_t want = qsub8(255 - (LED_RUN - h) * 52, random8(70));
          heat[i] = want > heat[i] ? qadd8(heat[i], 40) : qsub8(heat[i], 12);
          out[i] = ColorFromPalette(pal, ph / 2 + (LED_RUN - h) * 28, max<uint8_t>(heat[i], 14));
        }
        break;
      }
      case 10: case 11: case 12: case 13: case 14: {   // body patterns, palette coloured
        BodyFrame f;
        bodyAmbient(ambPat, f, t / 1000.0f * speed / 3.0f);
        for (uint8_t u = 0; u < N; u++) {
          uint8_t i = physOf(u);
          uint8_t bri = LED_BODY_FLOOR + scale8((uint8_t)(f.v[i] * 255), 255 - LED_BODY_FLOOR);
          CRGB c = ColorFromPalette(pal, ph / 2 + scale8(f.k[i], 128) + f.hs[i], bri);
          if (f.w[i]) c = blend(c, CRGB(bri, bri, bri), f.w[i]);
          out[u] = c;
        }
        break;
      }
      case 9: { // sides: the two sides trade brightness while a dot carries it across the bottom
        uint8_t s = sin8(ph * 3);                                   // 255 = left side bright
        uint8_t dot = ((uint16_t)(255 - s) * LED_RUN) >> 8;        // bottom slot nearest the bright side
        for (uint8_t i = 0; i < N; i++) {
          if (i < LED_RUN)          out[i] = ColorFromPalette(pal, ph, 20 + scale8(s, 235));
          else if (i >= 2 * LED_RUN) out[i] = ColorFromPalette(pal, ph + 128, 20 + scale8(255 - s, 235));
          else {
            uint8_t d = abs((int)(i - LED_RUN) - (int)dot);
            out[i] = ColorFromPalette(pal, ph + scale8(255 - s, 128), d == 0 ? 255 : d == 1 ? 70 : 10);
          }
        }
        break;
      }
    }
    // The whole body stays lit: the older patterns' dark gaps fill with the palette
    for (uint8_t i = 0; i < N; i++) out[i] |= ColorFromPalette(pal, ph + i * 16, LED_BODY_FLOOR);
  }

  void renderSpark(CRGB *out, unsigned long now) {
    unsigned long el = now - sparkStart;
    if (el >= LED_SPARK_MS) { sparking = false; return; }
    uint8_t env = el < 200 ? el * 255 / 200 : (LED_SPARK_MS - el) * 255 / (LED_SPARK_MS - 200);
    uint8_t ph = (uint8_t)(el / 4);
    for (uint8_t i = 0; i < LED_STRIP_COUNT; i++)
      nblend(out[i], CRGB(CHSV(ph + i * (256 / LED_STRIP_COUNT), 255, 255)), env);
  }
};

StripLeds leds;

#endif // STRIP_LEDS_H
