#ifndef EFFECTS_AMBIENT_H
#define EFFECTS_AMBIENT_H

#include <FastLED.h>
#include "config.h"
#include "layout.h"

// External references to globals defined in main sketch
extern CRGB leds[];

// ============================================================================
// Hi-res mode support for LCD display
// ============================================================================
#if defined(HIRES_ENABLED)
extern GfxDevice *gfx;
extern bool hiResMode;
extern bool hiResRenderedThisFrame;
#if defined(TOUCH_ENABLED)
extern bool menuVisible;
#else
static bool menuVisible = false;
#endif

// Helper: Convert CRGB to RGB565
inline uint16_t toRGB565(CRGB color) {
  return ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);
}

// Grid dimensions derived from LCD size (compile-time constants)
#define HIRES_COLS (LCD_WIDTH / 8)
#define HIRES_ROWS (LCD_HEIGHT / 8)

// Shared CRGB buffer for hi-res effect rendering (one effect at a time)
static CRGB hiResCrgbBuf[HIRES_COLS * HIRES_ROWS];

// XY mapping for hi-res grid
inline uint16_t hiResXY(uint8_t x, uint8_t y) {
  return y * HIRES_COLS + x;
}
#endif // HIRES_ENABLED

// ============================================================================
// EffectCtx — portable rendering context (adapted from noodle-v2)
// ============================================================================
#define NUM_VB_SURFACES 3  // 0 = LED 8x8, 1 = hi-res LCD, 2 = pixel-mode LCD

struct EffectCtx {
  CRGB*    leds;
  uint16_t numLeds;
  uint8_t  w, h;
  uint16_t frame;
  uint32_t millisNow;
  uint8_t  speed;
  uint8_t  baseHue;
  uint8_t  param2;
  float    proximity;
  uint16_t (*xy)(uint8_t x, uint8_t y);
  uint8_t  surfaceId;
  // Audio — Phase 2 will populate these from PDM mic analysis
  bool     audioAlive;
  float    audioBass, audioMid, audioTreble, audioRms, audioBeatEnv;
};

// ============================================================================
// Shared animation state
// ============================================================================
static uint16_t ambientFrame = 0;
static uint8_t  ambientHue   = 0;
static uint32_t lastAmbientAdvanceMs = 0;

inline void advanceAmbientState() {
  uint32_t now = millis();
  if (now != lastAmbientAdvanceMs) {
    ambientFrame += 10;  // speed=10 equivalent
    ambientHue++;
    lastAmbientAdvanceMs = now;
  }
}

// ============================================================================
// Kaleidoscope post-processing — mirror/symmetry transforms on CRGB buffers
// ============================================================================
// 0=off, 1=vertical, 2=horizontal, 3=H+V, 4=6-slice, 5=8-slice
#define KSCOPE_OFF      0
#define KSCOPE_VERT     1
#define KSCOPE_HORIZ    2
#define KSCOPE_HV       3
#define KSCOPE_6SLICE   4
#define KSCOPE_8SLICE   5
#define KSCOPE_MODE_COUNT 6

static const char* const KSCOPE_NAMES[] = {
  "Off", "Vertical", "Horizontal", "H+V", "6-Slice", "8-Slice"
};

uint8_t kaleidoscopeMode = KSCOPE_OFF;

// Apply kaleidoscope symmetry to a CRGB buffer in-place
inline void applyKaleidoscope(CRGB* buf, uint8_t w, uint8_t h) {
  if (kaleidoscopeMode == KSCOPE_OFF) return;

  uint8_t cx = w / 2;
  uint8_t cy = h / 2;

  switch (kaleidoscopeMode) {

    case KSCOPE_VERT:
      // Mirror left half to right (reflect across vertical center)
      for (uint8_t y = 0; y < h; y++) {
        for (uint8_t x = 0; x < cx; x++) {
          buf[y * w + (w - 1 - x)] = buf[y * w + x];
        }
      }
      break;

    case KSCOPE_HORIZ:
      // Mirror top half to bottom (reflect across horizontal center)
      for (uint8_t y = 0; y < cy; y++) {
        for (uint8_t x = 0; x < w; x++) {
          buf[(h - 1 - y) * w + x] = buf[y * w + x];
        }
      }
      break;

    case KSCOPE_HV:
      // Mirror top-left quadrant to all four quadrants
      for (uint8_t y = 0; y < cy; y++) {
        for (uint8_t x = 0; x < cx; x++) {
          CRGB c = buf[y * w + x];
          buf[y * w + (w - 1 - x)]           = c;  // top-right
          buf[(h - 1 - y) * w + x]            = c;  // bottom-left
          buf[(h - 1 - y) * w + (w - 1 - x)]  = c;  // bottom-right
        }
      }
      break;

    case KSCOPE_6SLICE:
    case KSCOPE_8SLICE: {
      // Radial kaleidoscope — fold all pixels into one wedge, mirror to fill
      // Must copy buffer first: in-place read+write corrupts source pixels
      uint16_t total = (uint16_t)w * h;
      CRGB* tmp = (CRGB*)malloc(total * sizeof(CRGB));
      if (!tmp) break;  // OOM — skip gracefully
      memcpy(tmp, buf, total * sizeof(CRGB));

      float fcx = (float)w * 0.5f;
      float fcy = (float)h * 0.5f;
      // 6-slice = π/3 (60°), 8-slice = π/4 (45°)
      float sector = (kaleidoscopeMode == KSCOPE_6SLICE)
                        ? 1.0471976f    // π/3
                        : 0.7853982f;   // π/4

      for (uint8_t y = 0; y < h; y++) {
        for (uint8_t x = 0; x < w; x++) {
          float dx = (float)x - fcx;
          float dy = (float)y - fcy;
          float angle = atan2f(dy, dx);
          float radius = sqrtf(dx * dx + dy * dy);
          // Normalize to [0, 2π)
          if (angle < 0) angle += 6.2831853f;
          // Fold into first sector
          float foldedAngle = fmodf(angle, sector);
          // Mirror alternate sectors for seamless kaleidoscope joins
          int sectorIdx = (int)(angle / sector);
          if (sectorIdx & 1) foldedAngle = sector - foldedAngle;
          // Map back to source coordinates
          int sx = (int)(fcx + radius * cosf(foldedAngle) + 0.5f);
          int sy = (int)(fcy + radius * sinf(foldedAngle) + 0.5f);
          if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
            buf[y * w + x] = tmp[sy * w + sx];
          } else {
            buf[y * w + x] = CRGB::Black;
          }
        }
      }
      free(tmp);
      break;
    }
  }
}

// ============================================================================
// Audio-reactive mapping (per-effect cheatsheet — keep in sync with effects below)
// ============================================================================
// Drama scaling: audioDrama (0..200) multiplies every audio field by drama/100
// in fillCtxAudio() — one knob, every effect responds.
//
//   #  Effect      Audio inputs                  Internal parameter(s)
//  ─  ─────────── ───────────────────────────── ─────────────────────────────
//   0 Plasma      bass                          phase velocity
//   1 Galaxy      mid; beatEnv; rms             rotation; star spawn; star V
//   2 Ripple      beatEnv                       ring phase reset
//   3 Chevrons    bass; rms                     scroll speed; wave amplitude
//   4 Stripes     treble; bass                  shimmer rate; stripe width
//   5 Checker     beatEnv; rms                  beat shift step; contrast
//   6 Scanline    bass; beatEnv; rms            travel speed; halo; trail len
//   7 Perlin      bass; mid; treble             scroll; hue cycle; detail amp
//   8 Distorsion  bass / mid / treble           R / G / B channel phases
//   9 ZVortex     mid                           spin speed
//  10 Snakes      bass; rms                     snake speed; trail brightness
//  11 Sinusoid    bass; mid; treble             emitter size; phase vel; sparkle
//  12 Puzzle      rms                           slide speed
//  13 Bumpmap     rms; beatEnv                  detail divisor; hue shift
//  14 Xorcery     bass; mid; beatEnv            scale; hue drift; XOR offset
//  15 Hiphotic    bass; mid; beatEnv            grid mul; phase rate; pulse
// ============================================================================

// Populate audio fields from AudioSpectrum (when available)
inline void fillCtxAudio(EffectCtx& ctx) {
  #ifdef TARGET_CORES3
  extern struct AudioSpectrum audioSpectrum;
  extern uint8_t audioDrama;
  if (audioSpectrum.alive && audioDrama > 0) {
    const float dramaF = (float)audioDrama / 100.0f;  // 0..2
    ctx.audioAlive   = true;
    // Cap at 1.5 so dramatic mode exceeds natural max without going unhinged
    ctx.audioBass    = constrain(audioSpectrum.bass    * dramaF, 0.0f, 1.5f);
    ctx.audioMid     = constrain(audioSpectrum.mid     * dramaF, 0.0f, 1.5f);
    ctx.audioTreble  = constrain(audioSpectrum.treble  * dramaF, 0.0f, 1.5f);
    ctx.audioRms     = constrain(audioSpectrum.rms     * dramaF, 0.0f, 1.5f);
    ctx.audioBeatEnv = constrain(audioSpectrum.beatEnv * dramaF, 0.0f, 1.5f);
    return;
  }
  #endif
  ctx.audioAlive = false;
  ctx.audioBass = ctx.audioMid = ctx.audioTreble = ctx.audioRms = ctx.audioBeatEnv = 0.0f;
}

// Fill EffectCtx for 8x8 LED rendering
inline void fillCtxLed(EffectCtx& ctx, uint8_t sid) {
  advanceAmbientState();
  ctx.leds       = leds;
  ctx.numLeds    = NUM_LEDS;
  ctx.w          = MATRIX_WIDTH;
  ctx.h          = MATRIX_HEIGHT;
  ctx.frame      = ambientFrame;
  ctx.millisNow  = millis();
  ctx.speed      = 10;
  ctx.baseHue    = ambientHue;
  ctx.param2     = 128;
  ctx.proximity  = 0.0f;
  ctx.xy         = XY;
  ctx.surfaceId  = sid;
  fillCtxAudio(ctx);
}

#if defined(HIRES_ENABLED)
// Fill EffectCtx for hi-res LCD rendering
inline void fillCtxHiRes(EffectCtx& ctx, CRGB* buf, uint8_t sid) {
  advanceAmbientState();
  ctx.leds       = buf;
  ctx.numLeds    = HIRES_COLS * HIRES_ROWS;
  ctx.w          = HIRES_COLS;
  ctx.h          = HIRES_ROWS;
  ctx.frame      = ambientFrame;
  ctx.millisNow  = millis();
  ctx.speed      = 10;
  ctx.baseHue    = ambientHue;
  ctx.param2     = 128;
  ctx.proximity  = 0.0f;
  ctx.xy         = hiResXY;
  ctx.surfaceId  = sid;
  fillCtxAudio(ctx);
}

// Blit CRGB buffer to LCD as 8x8 pixel blocks
inline void blitHiRes(CRGB* buf) {
  applyKaleidoscope(buf, HIRES_COLS, HIRES_ROWS);
  for (uint8_t y = 0; y < HIRES_ROWS; y++) {
    for (uint8_t x = 0; x < HIRES_COLS; x++) {
      gfx->fillRect(x * 8, y * 8, 8, 8, toRGB565(buf[y * HIRES_COLS + x]));
    }
  }
  hiResRenderedThisFrame = true;
}

// ============================================================================
// Pixel mode — expanded resolution for LCD (fills screen better than 8x8)
// Each pixel block is 20×20px → Core S3: 16×12, LCD-169: 12×14, LCD-13: 12×12
// ============================================================================
#define PIXEL_MODE_COLS (LCD_WIDTH / 20)
#define PIXEL_MODE_ROWS (LCD_HEIGHT / 20)
#define PIXEL_MODE_BLOCK 20

static CRGB pixelModeBuf[PIXEL_MODE_COLS * PIXEL_MODE_ROWS];

inline uint16_t pixelModeXY(uint8_t x, uint8_t y) {
  return y * PIXEL_MODE_COLS + x;
}

inline void fillCtxPixel(EffectCtx& ctx) {
  advanceAmbientState();
  ctx.leds       = pixelModeBuf;
  ctx.numLeds    = PIXEL_MODE_COLS * PIXEL_MODE_ROWS;
  ctx.w          = PIXEL_MODE_COLS;
  ctx.h          = PIXEL_MODE_ROWS;
  ctx.frame      = ambientFrame;
  ctx.millisNow  = millis();
  ctx.speed      = 10;
  ctx.baseHue    = ambientHue;
  ctx.param2     = 128;
  ctx.proximity  = 0.0f;
  ctx.xy         = pixelModeXY;
  ctx.surfaceId  = 2;  // distinct from LED(0) and hi-res(1)
  fillCtxAudio(ctx);
}

inline void blitPixelMode(CRGB* buf) {
  for (uint8_t y = 0; y < PIXEL_MODE_ROWS; y++) {
    for (uint8_t x = 0; x < PIXEL_MODE_COLS; x++) {
      gfx->fillRect(x * PIXEL_MODE_BLOCK, y * PIXEL_MODE_BLOCK,
                     PIXEL_MODE_BLOCK, PIXEL_MODE_BLOCK,
                     toRGB565(buf[y * PIXEL_MODE_COLS + x]));
    }
  }
}

#endif // HIRES_ENABLED

// ============================================================================
// Helper functions (from noodle-v2)
// ============================================================================

// Distorsion gamma table (Yaroslaw Turbin)
static const uint8_t distorsionExpGamma[256] PROGMEM = {
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,
  1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
  1,   2,   2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,
  4,   4,   4,   4,   4,   5,   5,   5,   5,   5,   6,   6,   6,   7,   7,
  7,   7,   8,   8,   8,   9,   9,   9,   10,  10,  10,  11,  11,  12,  12,
  12,  13,  13,  14,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,
  19,  20,  20,  21,  21,  22,  23,  23,  24,  24,  25,  26,  26,  27,  28,
  28,  29,  30,  30,  31,  32,  32,  33,  34,  35,  35,  36,  37,  38,  39,
  39,  40,  41,  42,  43,  44,  44,  45,  46,  47,  48,  49,  50,  51,  52,
  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,
  68,  70,  71,  72,  73,  74,  75,  77,  78,  79,  80,  82,  83,  84,  85,
  87,  89,  91,  92,  93,  95,  96,  98,  99,  100, 101, 102, 105, 106, 108,
  109, 111, 112, 114, 115, 117, 118, 120, 121, 123, 125, 126, 128, 130, 131,
  133, 135, 136, 138, 140, 142, 143, 145, 147, 149, 151, 152, 154, 156, 158,
  160, 162, 164, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185, 187,
  190, 192, 194, 196, 198, 200, 202, 204, 207, 209, 211, 213, 216, 218, 220,
  222, 225, 227, 229, 232, 234, 236, 239, 241, 244, 246, 249, 251, 253, 254, 255
};

// Turbin's cos_wave: 0 at 0, peaks at 128. Equivalent to 255-cos8.
inline uint8_t cwave(uint8_t x) {
  return (uint8_t)(255 - cos8(x));
}

// Pixelblaze helpers (for Xorcery)
inline float pbTriangle(float t) {
  t -= floorf(t);
  return (t < 0.5f) ? (t * 2.0f) : ((1.0f - t) * 2.0f);
}

inline float pbWave(float t) {
  return 0.5f + 0.5f * sinf(t * 2.0f * (float)PI);
}

// ============================================================================
// Stateful effect structs (per-surface state)
// ============================================================================

// --- Snakes ---
#define SNAKES_LEN 8
#define SNAKES_MAX 16

struct SnakeState {
  bool     inited;
  uint8_t  count;
  uint8_t  w, h;
  float    posX[SNAKES_MAX];
  float    posY[SNAKES_MAX];
  float    speedX[SNAKES_MAX];
  float    subY[SNAKES_MAX];
  int8_t   hueOffset[SNAKES_MAX];
  uint8_t  dir[SNAKES_MAX];
  uint32_t trail[SNAKES_MAX];
};
static SnakeState snakeStates[NUM_VB_SURFACES];

inline void snakeInit(SnakeState& s, uint8_t count, uint8_t w, uint8_t h) {
  s.count = count; s.w = w; s.h = h; s.inited = true;
  for (uint8_t i = 0; i < count; i++) {
    s.posX[i]      = random8(w);
    s.posY[i]      = random8(h);
    s.speedX[i]    = 1.0f + random8() / 512.0f;
    s.subY[i]      = 0.0f;
    s.hueOffset[i] = (int8_t)random8(64) - 32;
    s.dir[i]       = random8(4);
    s.trail[i]     = 0;
  }
}

inline void snakeStep(SnakeState& s, uint8_t i, uint8_t newDir) {
  s.dir[i] = newDir;
  switch (newDir) {
    case 0b00: s.posY[i] = (s.posY[i] >= s.h - 1) ? 0 : s.posY[i] + 1; break;
    case 0b01: s.posY[i] = (s.posY[i] <= 0)       ? s.h - 1 : s.posY[i] - 1; break;
    case 0b10: s.posX[i] = (s.posX[i] <= 0)       ? s.w - 1 : s.posX[i] - 1; break;
    case 0b11: s.posX[i] = (s.posX[i] >= s.w - 1) ? 0 : s.posX[i] + 1; break;
  }
}

// --- Puzzle ---
#define PUZZLE_MAX_GRID 10  // supports up to 40/4=10 columns on hi-res LCD

struct PuzzleState {
  bool     inited;
  uint8_t  w, h;
  uint8_t  psize;
  uint8_t  pcols, prows;
  uint8_t  puzzle[PUZZLE_MAX_GRID][PUZZLE_MAX_GRID];
  uint8_t  zdot[2];
  uint8_t  etap;
  int8_t   move[2];
  int16_t  shift[2];
  uint8_t  tmpp;
  bool     xory;
};
static PuzzleState puzzleStates[NUM_VB_SURFACES];

inline void puzzleInit(PuzzleState& s, uint8_t w, uint8_t h, uint8_t psize) {
  s.w = w; s.h = h; s.psize = psize;
  s.pcols = w / psize; if (s.pcols > PUZZLE_MAX_GRID) s.pcols = PUZZLE_MAX_GRID;
  s.prows = h / psize; if (s.prows > PUZZLE_MAX_GRID) s.prows = PUZZLE_MAX_GRID;
  if (s.pcols < 2) s.pcols = 2;
  if (s.prows < 2) s.prows = 2;
  for (uint8_t x = 0; x < s.pcols; x++)
    for (uint8_t y = 0; y < s.prows; y++)
      s.puzzle[x][y] = random8(16, 255);
  s.zdot[0] = random8(s.pcols);
  s.zdot[1] = random8(s.prows);
  s.puzzle[s.zdot[0]][s.zdot[1]] = 0;
  s.etap = 0;
  s.move[0] = 0; s.move[1] = 0;
  s.shift[0] = 0; s.shift[1] = 0;
  s.tmpp = 0;
  s.xory = false;
  s.inited = true;
}

// Build a once-per-frame palette: HeatColors_p rotated into baseHue.
inline CRGBPalette16 puzzleRotatedHeat(uint8_t baseHue) {
  CRGBPalette16 pal = HeatColors_p;
  for (uint8_t i = 0; i < 16; i++) {
    CRGB c = pal[i];
    CHSV hsv = rgb2hsv_approximate(c);
    hsv.hue += baseHue;
    pal[i] = CRGB(hsv);
  }
  return pal;
}

inline void puzzleFillCell(EffectCtx& ctx, const CRGBPalette16& pal,
                           int x1, int y1, uint8_t psize, uint8_t col) {
  for (int dy = 0; dy < psize; dy++) {
    int y = y1 + dy;
    if (y < 0 || y >= ctx.h) continue;
    for (int dx = 0; dx < psize; dx++) {
      int x = x1 + dx;
      if (x < 0 || x >= ctx.w) continue;
      CRGB c;
      if (col == 0) {
        c = CRGB::Black;
      } else {
        bool border = (dx == 0 || dx == psize - 1 || dy == 0 || dy == psize - 1);
        uint8_t idx = border ? (uint8_t)qsub8(col, 16) : col;
        c = ColorFromPalette(pal, idx);
      }
      ctx.leds[ctx.xy((uint8_t)x, (uint8_t)y)] = c;
    }
  }
}

// Sub-pixel additive tile — used for the sliding cell
inline void puzzleWuPixel(EffectCtx& ctx, int32_t x, int32_t y, CRGB col) {
  uint8_t xx = x & 0xff, yy = y & 0xff;
  uint8_t ix = 255 - xx, iy = 255 - yy;
  uint8_t wu[4] = {
    (uint8_t)((ix * iy + ix + iy) >> 8),
    (uint8_t)((xx * iy + xx + iy) >> 8),
    (uint8_t)((ix * yy + ix + yy) >> 8),
    (uint8_t)((xx * yy + xx + yy) >> 8),
  };
  for (uint8_t i = 0; i < 4; i++) {
    int16_t xn = (x >> 8) + (i & 1);
    int16_t yn = (y >> 8) + ((i >> 1) & 1);
    if (xn < 0 || yn < 0 || xn >= ctx.w || yn >= ctx.h) continue;
    CRGB& dst = ctx.leds[ctx.xy((uint8_t)xn, (uint8_t)yn)];
    dst.r = qadd8(dst.r, (col.r * wu[i]) >> 8);
    dst.g = qadd8(dst.g, (col.g * wu[i]) >> 8);
    dst.b = qadd8(dst.b, (col.b * wu[i]) >> 8);
  }
}

inline void puzzleDrawWuCell(EffectCtx& ctx, const CRGBPalette16& pal,
                             int32_t x1, int32_t y1, uint8_t psize, uint8_t col) {
  if (col == 0) return;
  int32_t Lx = psize * 256;
  int32_t Ly = psize * 256;
  for (int32_t dy = 0; dy < Ly; dy += 256) {
    for (int32_t dx = 0; dx < Lx; dx += 256) {
      bool border = (dx == 0 || dx == Lx - 256 || dy == 0 || dy == Ly - 256);
      uint8_t idx = border ? (uint8_t)qsub8(col, 16) : col;
      puzzleWuPixel(ctx, x1 + dx, y1 + dy, ColorFromPalette(pal, idx));
    }
  }
}

// ============================================================================
// Render functions — ported from noodle-v2 matrix_task.cpp
// All render functions write to ctx.leds[] using ctx.xy() mapping.
// Audio-reactive code gracefully degrades when ctx.audioAlive == false.
// ============================================================================

void renderPlasma(EffectCtx& ctx) {
  static uint16_t plasmaPhase = 0;
  static uint16_t prevFrame   = 0xFFFF;
  if (ctx.frame != prevFrame) {
    float now    = (float)ctx.millisNow;
    float dirMul = sinf(now / 7000.0f);
    float bass   = ctx.audioAlive ? (1.0f + 2.5f * ctx.audioBass) : 1.0f;
    int32_t delta = (int32_t)((float)ctx.speed * dirMul * bass);
    plasmaPhase = (uint16_t)((int32_t)plasmaPhase + delta);
    prevFrame = ctx.frame;
  }
  uint16_t f = plasmaPhase;

  for (int y = 0; y < ctx.h; y++) {
    for (int x = 0; x < ctx.w; x++) {
      uint8_t sx = (x * 255) / ctx.w;
      uint8_t sy = (y * 255) / ctx.h;
      uint16_t sum = (uint16_t)sin8(sx + f)
                   + (uint16_t)sin8(sy + f / 2)
                   + (uint16_t)sin8((sx + sy) / 2 + f / 3);
      uint8_t wave = sum / 3;
      uint8_t arc = 90 + scale8(ctx.param2, 160);
      uint8_t h = ctx.baseHue + scale8(wave, arc);
      uint8_t v = 60 + (wave * 3) / 4;
      ctx.leds[ctx.xy(x, y)] = CHSV(h, 255, v);
    }
  }
}

void renderGalaxy(EffectCtx& ctx) {
  static int32_t phaseA = 0, phaseB = 0, phaseC = 0;
  static uint16_t prevFrame = 0xFFFF;
  // Audio: mid scales rotation rate (+ up to 3×)
  float audioSpd = ctx.audioAlive ? (1.0f + 3.0f * ctx.audioMid) : 1.0f;
  if (ctx.frame != prevFrame) {
    float now = (float)ctx.millisNow;
    float spnScale = 0.1f + (float)ctx.param2 * (0.6f / 255.0f);
    float spd = (float)ctx.speed * spnScale * audioSpd;
    phaseA += (int32_t)(sinf(now / 4500.0f) * spd);
    phaseB += (int32_t)(sinf(now / 3700.0f) * spd);
    phaseC += (int32_t)(sinf(now / 5200.0f) * spd);
    prevFrame = ctx.frame;
  }
  uint8_t pa = (uint8_t)phaseA;
  uint8_t pb = (uint8_t)phaseB;
  uint8_t pc = (uint8_t)phaseC;

  for (int y = 0; y < ctx.h; y++) {
    for (int x = 0; x < ctx.w; x++) {
      uint8_t sx = (x * 255) / ctx.w;
      uint8_t sy = (y * 255) / ctx.h;
      uint8_t h = ctx.baseHue + 180
                  + sin8(sx + pa) / 3
                  + cos8(sy + pb) / 3;
      uint8_t v = sin8(sx + sy + pc) / 3 + 40;
      ctx.leds[ctx.xy(x, y)] = CHSV(h, 230, v);
    }
  }
  // Audio: beat triggers extra star spawns; rms boosts star brightness
  uint8_t starThreshold = 15;
  uint8_t starV = 200;
  if (ctx.audioAlive) {
    // Up to ~3× spawn rate on strong beats (capped before random8 overflow)
    int boosted = 15 + (int)(ctx.audioBeatEnv * 40.0f);
    starThreshold = (uint8_t)constrain(boosted, 0, 200);
    // 150..255 brightness range with rms
    starV = (uint8_t)constrain((int)(150.0f + ctx.audioRms * 105.0f), 0, 255);
  }
  if (random8() < starThreshold) {
    ctx.leds[ctx.xy(random8(ctx.w), random8(ctx.h))] =
      CHSV(ctx.baseHue + 180 + random8(40), 80, starV);
  }
}

void renderRipple(EffectCtx& ctx) {
  static uint16_t lastBeatFrame = 0;
  static float    prevBeatEnv   = 0.0f;
  if (ctx.audioAlive && ctx.audioBeatEnv > 0.5f &&
      ctx.audioBeatEnv > prevBeatEnv) {
    lastBeatFrame = ctx.frame;
  }
  prevBeatEnv = ctx.audioBeatEnv;
  uint16_t phase = ctx.audioAlive ? (uint16_t)(ctx.frame - lastBeatFrame)
                                  : ctx.frame;

  uint8_t cx = ctx.w / 2;
  uint8_t cy = ctx.h / 2;
  int sxScale = (ctx.w > 0) ? (48 / ctx.w) : 4;
  int syScale = (ctx.h > 0) ? (48 / ctx.h) : 12;
  if (sxScale < 1) sxScale = 1;
  if (syScale < 1) syScale = 1;
  for (int y = 0; y < ctx.h; y++) {
    for (int x = 0; x < ctx.w; x++) {
      int dx = (x - cx) * sxScale;
      int dy = (y - cy) * syScale;
      uint8_t dist = sqrt16(dx * dx + dy * dy);
      uint8_t ringMul = 2 + (ctx.param2 >> 5);
      uint8_t ring = sin8(dist * ringMul - phase);
      uint8_t h = ctx.baseHue + scale8(ring, 85) + scale8(dist, 85);
      uint8_t v = ring / 2 + 100;
      ctx.leds[ctx.xy(x, y)] = CHSV(h, 240, v);
    }
  }
}

void renderChevrons(EffectCtx& ctx) {
  int mid = ctx.w / 2;
  int hSpan = 60;
  // Audio: bass scales scroll speed (up to ~3.5×); rms boosts wave amplitude.
  float audioMotion = ctx.audioAlive ? (1.0f + 2.5f * ctx.audioBass) : 1.0f;
  uint16_t audioFrame = (uint16_t)((float)ctx.frame * audioMotion);
  float audioAmp = ctx.audioAlive ? (1.0f + 0.5f * ctx.audioRms) : 1.0f;
  for (int y = 0; y < ctx.h; y++) {
    for (int x = 0; x < ctx.w; x++) {
      int dx = abs(x - mid);
      uint8_t chevMul = 8 + (ctx.param2 >> 2);
      uint8_t wave = sin8(dx * chevMul + y * 45 - audioFrame);
      uint8_t h = ctx.baseHue + wave / 3 + (y * hSpan) / ctx.h;
      uint8_t v = scale8(wave, wave);
      v = (uint8_t)constrain((int)((float)v * audioAmp), 0, 255);
      if (v < 25) v = 25;
      ctx.leds[ctx.xy(x, y)] = CHSV(h, 255, v);
    }
  }
}

void renderStripes(EffectCtx& ctx) {
  const int hSpan = 90;
  // Audio: treble accelerates shimmer; bass widens stripes (lower stripeMul = wider bands).
  uint8_t shimSpdBoost = ctx.audioAlive ? (uint8_t)(ctx.audioTreble * 180.0f) : 0;
  uint8_t stripeMul = 3;
  if (ctx.audioAlive) {
    float bassFactor = 1.0f - 0.6f * ctx.audioBass;  // 1.0 .. 0.1 (bass=1.5 dramatic)
    stripeMul = (uint8_t)constrain((int)((float)3 * bassFactor + 0.5f), 1, 6);
  }
  for (int y = 0; y < ctx.h; y++) {
    uint8_t sy = (y * 255) / ctx.h;
    uint8_t wave = sin8(sy * stripeMul + ctx.frame);
    uint8_t h = ctx.baseHue + (y * hSpan) / ctx.h;
    uint8_t v = wave;
    uint8_t shimMul = 4 + (ctx.param2 >> 3);
    for (int x = 0; x < ctx.w; x++) {
      uint8_t shimmer = sin8(x * shimMul + (ctx.frame + shimSpdBoost) / 3) / 8;
      ctx.leds[ctx.xy(x, y)] = CHSV(h + shimmer, 240, v);
    }
  }
}

void renderChecker(EffectCtx& ctx) {
  // Audio: beat onset bumps the shift step (visible flip on every beat);
  // rms widens the contrast between fg/bg squares.
  static uint8_t beatShiftAccum = 0;
  static float   prevBeatEnv    = 0.0f;
  if (ctx.audioAlive && ctx.audioBeatEnv > 0.5f && ctx.audioBeatEnv > prevBeatEnv) {
    beatShiftAccum++;
  }
  prevBeatEnv = ctx.audioBeatEnv;

  uint8_t shift = (uint8_t)((ctx.frame >> 4) + beatShiftAccum);
  uint8_t fgV = 220, bgV = 180;
  if (ctx.audioAlive) {
    // contrast widens with rms: fg brighter, bg darker
    fgV = (uint8_t)constrain((int)(180.0f + 75.0f * ctx.audioRms), 0, 255);
    bgV = (uint8_t)constrain((int)(180.0f - 100.0f * ctx.audioRms), 30, 255);
  }
  for (int y = 0; y < ctx.h; y++) {
    for (int x = 0; x < ctx.w; x++) {
      bool on = ((x + y + shift) & 1);
      uint8_t h = on ? ctx.baseHue : ctx.param2;
      uint8_t v = on ? fgV : bgV;
      uint8_t pulse = sin8(ctx.frame / 2) / 4;
      v = qadd8(v, on ? pulse : 0);
      ctx.leds[ctx.xy(x, y)] = CHSV(h, 255, v);
    }
  }
}

void renderScanline(EffectCtx& ctx) {
  // Audio: bass accelerates travel; beatEnv brightens trail; rms extends trail length.
  float audioSpd = ctx.audioAlive ? (1.0f + 2.5f * ctx.audioBass) : 1.0f;
  uint16_t audioFrame = (uint16_t)((float)ctx.frame * audioSpd);
  uint8_t pos = (audioFrame >> 3) % (ctx.h * 2);
  uint8_t row = (pos < ctx.h) ? pos : (ctx.h * 2 - 1 - pos);

  int trail = 1 + (ctx.param2 >> 6);
  if (ctx.audioAlive) {
    trail = (int)constrain((int)((float)trail * (1.0f + 1.5f * ctx.audioRms)), 1, 6);
  }
  uint8_t haloBoost = ctx.audioAlive ? (uint8_t)(ctx.audioBeatEnv * 100.0f) : 0;
  for (int y = 0; y < ctx.h; y++) {
    int dist = abs(y - (int)row);
    uint8_t v;
    if (dist == 0) v = 255;
    else if (dist <= trail) v = qadd8(130 - (dist - 1) * 35, haloBoost);
    else v = 20;

    uint8_t h = ctx.baseHue + (y * 45) / ctx.h + sin8(ctx.frame / 2) / 4;
    for (int x = 0; x < ctx.w; x++) {
      uint8_t xShimmer = sin8(x * 30 + ctx.frame) / 8;
      ctx.leds[ctx.xy(x, y)] = CHSV(h + xShimmer, (dist == 0) ? 200 : 255, v);
    }
  }
}

void renderPerlin(EffectCtx& ctx) {
  uint16_t zoom = 64 + ctx.param2;
  // Audio: bass scales scroll velocity; mid shifts the hue base; treble adds
  // high-frequency detail through the third sin layer.
  float audioScroll = ctx.audioAlive ? (1.0f + 2.0f * ctx.audioBass) : 1.0f;
  uint16_t audioFrame = (uint16_t)((float)ctx.frame * audioScroll);
  uint8_t audioHue = ctx.baseHue + (ctx.audioAlive ? (uint8_t)(ctx.audioMid * 80.0f) : 0);
  uint8_t audioDetail = ctx.audioAlive ? (uint8_t)(ctx.audioTreble * 120.0f) : 0;
  for (int y = 0; y < ctx.h; y++) {
    for (int x = 0; x < ctx.w; x++) {
      uint8_t sxBase = (x * 255) / ctx.w;
      uint8_t syBase = (y * 255) / ctx.h;
      uint8_t sx = (uint8_t)((uint16_t)sxBase * zoom / 128);
      uint8_t sy = (uint8_t)((uint16_t)syBase * zoom / 128);
      uint8_t n1 = sin8(sx * 2 + audioFrame + sin8(sy * 3 + audioFrame / 2));
      uint8_t n2 = sin8(sy * 3 - audioFrame / 3 + cos8(sx * 2 + audioFrame / 4));
      uint8_t n3 = sin8((sx + sy) + audioFrame / 2);
      uint8_t val = (n1 / 3 + n2 / 3 + n3 / 3);
      val = qadd8(val, scale8(n3, audioDetail));
      uint8_t h = audioHue + scale8(val, 170);
      uint8_t v = val / 2 + 100;
      ctx.leds[ctx.xy(x, y)] = CHSV(h, 220, v);
    }
  }
}

void renderDistorsion(EffectCtx& ctx) {
  const uint8_t scale = 4;
  const uint8_t w = 1 + (ctx.param2 >> 5);
  const uint8_t hue = ctx.baseHue;

  // Audio: bass / mid / treble each drive one of the three color-channel
  // time phases. Natural 3-band-to-3-channel fit.
  float audioR = ctx.audioAlive ? (1.0f + 3.0f * ctx.audioBass)   : 1.0f;
  float audioG = ctx.audioAlive ? (1.0f + 3.0f * ctx.audioMid)    : 1.0f;
  float audioB = ctx.audioAlive ? (1.0f + 3.0f * ctx.audioTreble) : 1.0f;
  uint16_t a  = (uint16_t)((float)(ctx.frame >> 2) * audioR);
  uint16_t a2 = (uint16_t)((float)(a >> 1)         * audioG);
  uint16_t a3 = (uint16_t)((float)(a / 3)          * audioB);

  uint16_t cx  = ((uint16_t)sin8((a3     ) & 0xFF) * ctx.w) >> 8;
  uint16_t cy  = ((uint16_t)sin8((a3 + 32) & 0xFF) * ctx.h) >> 8;
  uint16_t cx1 = ((uint16_t)sin8((a2 + 64) & 0xFF) * ctx.w) >> 8;
  uint16_t cy1 = ((uint16_t)sin8((a2 +128) & 0xFF) * ctx.h) >> 8;
  uint16_t cx2 = ((uint16_t)sin8((a  +160) & 0xFF) * ctx.w) >> 8;
  uint16_t cy2 = ((uint16_t)sin8((a  +200) & 0xFF) * ctx.h) >> 8;
  cx *= scale; cy *= scale; cx1 *= scale; cy1 *= scale; cx2 *= scale; cy2 *= scale;

  uint16_t xoffs = 0;
  for (int x = 0; x < ctx.w; x++) {
    xoffs += scale;
    uint16_t yoffs = 0;
    for (int y = 0; y < ctx.h; y++) {
      yoffs += scale;

      uint8_t rdistort = cwave((cwave(((x<<3) + a ) & 0xFF) + cwave(((y<<3) - a2) & 0xFF) + a3 + hue     ) & 0xFF) >> 1;
      uint8_t gdistort = cwave((cwave(((x<<3) - a2) & 0xFF) + cwave(((y<<3) + a3) & 0xFF) + a  + 32 + hue) & 0xFF) >> 1;
      uint8_t bdistort = cwave((cwave(((x<<3) + a3) & 0xFF) + cwave(((y<<3) - a ) & 0xFF) + a2 + 64 + hue) & 0xFF) >> 1;

      int dxr = (int)xoffs - (int)cx;   int dyr = (int)yoffs - (int)cy;
      int dxg = (int)xoffs - (int)cx1;  int dyg = (int)yoffs - (int)cy1;
      int dxb = (int)xoffs - (int)cx2;  int dyb = (int)yoffs - (int)cy2;

      uint8_t valueR = rdistort + w * ((a  - (uint16_t)((dxr*dxr + dyr*dyr) >> 7)) & 0xFF);
      uint8_t valueG = gdistort + w * ((a2 - (uint16_t)((dxg*dxg + dyg*dyg) >> 7)) & 0xFF);
      uint8_t valueB = bdistort + w * ((a3 - (uint16_t)((dxb*dxb + dyb*dyb) >> 7)) & 0xFF);

      valueR = cwave(valueR);
      valueG = cwave(valueG);
      valueB = cwave(valueB);

      CRGB& px = ctx.leds[ctx.xy(x, y)];
      px.r = pgm_read_byte(distorsionExpGamma + valueR);
      px.g = pgm_read_byte(distorsionExpGamma + valueG);
      px.b = pgm_read_byte(distorsionExpGamma + valueB);
    }
  }
}

void renderZVortex(EffectCtx& ctx) {
  const float cxh = ctx.w * 0.5f;
  const float cyh = ctx.h * 0.5f;
  const int16_t radius = (int16_t)cxh;

  static uint16_t vortexPhase = 0;
  static uint16_t prevFrame   = 0xFFFF;
  if (ctx.frame != prevFrame) {
    float mul = ctx.audioAlive ? (1.0f + 3.0f * ctx.audioMid) : 1.0f;
    vortexPhase += (uint16_t)((float)ctx.speed * 2.0f * mul);
    prevFrame = ctx.frame;
  }
  uint8_t zvOffset = (uint8_t)vortexPhase;
  int8_t effTimer  = (int8_t)(sin8(ctx.frame >> 2) / 10);

  uint8_t tight = 1 + (ctx.param2 >> 6);
  for (int x = 0; x < ctx.w; x++) {
    for (int y = 0; y < ctx.h; y++) {
      int16_t a = (int16_t)(cyh - y - 0.5f);
      int16_t b = (int16_t)(cxh - x - 0.5f);
      int16_t dist = (int16_t)sin8((uint8_t)((a * a + b * b) * tight));
      dist += radius;

      int br = map(dist, -(int)effTimer, (int)radius, 255, 110) + zvOffset;
      uint8_t v = sin8((uint8_t)br);

      int hArc = map(dist, (int)radius, -3, 0, 130);
      uint8_t h = ctx.baseHue + (uint8_t)(hArc >> 1);

      ctx.leds[ctx.xy(x, y)] = CHSV(h, 255, v);
    }
  }
}

void renderSnakes(EffectCtx& ctx) {
  if (ctx.surfaceId >= NUM_VB_SURFACES) return;
  SnakeState& s = snakeStates[ctx.surfaceId];

  uint16_t cap = ((uint16_t)ctx.w * ctx.h) / 4;
  if (cap > SNAKES_MAX) cap = SNAKES_MAX;
  if (cap < 1) cap = 1;
  uint8_t want = 1 + ((uint16_t)ctx.param2 * (cap - 1)) / 255;

  if (!s.inited || s.count != want || s.w != ctx.w || s.h != ctx.h) {
    snakeInit(s, want, ctx.w, ctx.h);
  }

  // Audio: bass scales snake speed (+ up to 4×); rms scales trail brightness.
  float audioSpd = ctx.audioAlive ? (1.0f + 3.0f * ctx.audioBass) : 1.0f;
  float speedfactor = ((float)ctx.speed / 100.0f + 0.005f) * audioSpd;
  uint8_t audioTrailV = ctx.audioAlive
    ? (uint8_t)constrain((int)(120.0f + 135.0f * ctx.audioRms), 60, 255)
    : 255;

  for (uint16_t i = 0; i < ctx.numLeds; i++) ctx.leds[i] = CRGB::Black;

  for (uint8_t i = 0; i < s.count; i++) {
    s.subY[i] += s.speedX[i] * speedfactor;
    if (s.subY[i] >= 1.0f) {
      s.subY[i] -= (int)s.subY[i];
      uint8_t roll = random8(9);
      uint8_t code;
      uint8_t newDir = s.dir[i];
      if (roll == 0) {
        bool leftTurn = random8(2);
        if (leftTurn) {
          code = 0b01;
          switch (s.dir[i]) {
            case 0b10: newDir = 0b01; break;
            case 0b11: newDir = 0b00; break;
            case 0b00: newDir = 0b10; break;
            case 0b01: newDir = 0b11; break;
          }
        } else {
          code = 0b11;
          switch (s.dir[i]) {
            case 0b11: newDir = 0b01; break;
            case 0b10: newDir = 0b00; break;
            case 0b01: newDir = 0b10; break;
            case 0b00: newDir = 0b11; break;
          }
        }
      } else {
        code = 0b00;
      }
      s.trail[i] = (s.trail[i] << 2) | code;
      snakeStep(s, i, newDir);
    }

    int8_t dx = 0, dy = 0;
    switch (s.dir[i]) {
      case 0b01: dy =  1; break;
      case 0b00: dy = -1; break;
      case 0b10: dx =  1; break;
      case 0b11: dx = -1; break;
    }

    uint8_t baseH = ctx.baseHue + s.hueOffset[i];
    uint8_t hx = (uint8_t)s.posX[i];
    uint8_t hy = (uint8_t)s.posY[i];

    ctx.leds[ctx.xy(hx, hy)] += CRGB(CHSV(baseH, 255, (uint8_t)(s.subY[i] * 255.0f)));

    uint32_t temp = s.trail[i];
    for (uint8_t m = 0; m < SNAKES_LEN; m++) {
      hx = (ctx.w + hx + dx) % ctx.w;
      hy = (ctx.h + hy + dy) % ctx.h;
      uint8_t bodyHue = baseH + (uint8_t)((m + s.subY[i]) * 4);
      ctx.leds[ctx.xy(hx, hy)] += CRGB(CHSV(bodyHue, 255, audioTrailV));

      if (temp & 0b01) {
        temp >>= 1;
        if (temp & 0b01) {
          if (dx == 0) { dx = -dy; dy = 0; }
          else         { dy = dx;  dx = 0; }
        } else {
          if (dx == 0) { dx = dy;  dy = 0; }
          else         { dy = -dx; dx = 0; }
        }
        temp >>= 1;
      } else {
        temp >>= 2;
      }
    }

    hx = (ctx.w + hx + dx) % ctx.w;
    hy = (ctx.h + hy + dy) % ctx.h;
    uint8_t tailHue = baseH + (uint8_t)((SNAKES_LEN + s.subY[i]) * 4);
    ctx.leds[ctx.xy(hx, hy)] += CRGB(CHSV(tailHue, 255, (uint8_t)((1.0f - s.subY[i]) * 255.0f)));
  }
}

void renderSinusoid(EffectCtx& ctx) {
  uint8_t variant = ((uint16_t)ctx.param2 * 9) / 256;
  if (variant > 8) variant = 8;

  // Audio: bass scales emitter size (wavelength); mid scales phase velocity;
  // treble adds sparkle accents per pixel.
  const float audioSize  = ctx.audioAlive ? (1.0f + 1.0f * ctx.audioBass) : 1.0f;
  const float audioPhase = ctx.audioAlive ? (1.0f + 2.5f * ctx.audioMid)  : 1.0f;
  const float audioSparkleP = ctx.audioAlive ? (ctx.audioTreble * 80.0f) : 0.0f;
  const float speedfactor = 1.0f;
  const float e_s3_size   = 5.0f * audioSize;
  const float _scale      = 1.0f;
  const float emitterX    = ctx.w * 0.5f;
  const float emitterY    = ctx.h * 0.5f;
  const float time_shift  = (float)ctx.frame * audioPhase;

  float c1x = e_s3_size * sinf(speedfactor * 0.003f  * time_shift) - emitterX;
  float c1y = e_s3_size * cosf(speedfactor * 0.0022f * time_shift) - emitterY;
  float c2x = e_s3_size * sinf(speedfactor * 0.0021f * time_shift) - emitterX;
  float c2y = e_s3_size * cosf(speedfactor * 0.002f  * time_shift) - emitterY;
  float c3x = e_s3_size * cosf(speedfactor * 0.0041f * time_shift) - emitterX;
  float c3y = e_s3_size * sinf(speedfactor * 0.0052f * time_shift) - emitterY;

  uint8_t hueA = ctx.baseHue;
  uint8_t hueB = ctx.baseHue + 85;
  uint8_t hueC = ctx.baseHue + 170;

  for (uint8_t y = 0; y < ctx.h; y++) {
    for (uint8_t x = 0; x < ctx.w; x++) {
      uint8_t v1 = 0, v2 = 0, v3 = 0;

      switch (variant) {
        case 0: {
          float cx = x + c1x, cy = y + c1y;
          v1 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(cx*cx + cy*cy))));
          cx = x + c2x; cy = y + c2y;
          v2 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(cx*cx + cy*cy))));
          break;
        }
        case 1: {
          float cx = x + c1x, cy = y + c1y;
          v1 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(cx*cx + cy*cy))));
          cx = x + c2x; cy = y + c2y;
          uint8_t vb = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(cx*cx + cy*cy))));
          if (vb > v1) v1 = vb;
          v2 = vb >> 1;
          v3 = vb >> 3;
          break;
        }
        case 2: {
          float cx = x + c1x, cy = y + c1y;
          v1 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(cx*cx + cy*cy))));
          cx = x + c2x; cy = y + c2y;
          v2 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(cx*cx + cy*cy))));
          cx = x + c3x; cy = y + c3y;
          v3 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(cx*cx + cy*cy))));
          break;
        }
        case 3: {
          float cx = x + c1x, cy = y + c1y;
          float d = cx*cx + cy*cy;
          v1 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(d))));
          float t2 = d + 0.01f * time_shift * speedfactor;
          uint8_t vb = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(t2))));
          v2 = vb; v3 = vb;
          break;
        }
        case 4: {
          float cx = x + c1x, cy = y + c1y;
          float d = cx*cx + cy*cy;
          v1 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(d))));
          v2 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(d + 0.01f * time_shift * speedfactor))));
          v3 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(d + 0.025f * time_shift * speedfactor))));
          break;
        }
        case 5: {
          float cx = x + c1x, cy = y + c1y;
          v1 = (uint8_t)(127.0f * (1.0f + sinf(_scale * sqrtf(cx*cx + cy*cy))));
          v2 = (uint8_t)(127.0f * (1.0f + sinf(_scale * x * 10.0f)));
          v3 = (uint8_t)(127.0f * (1.0f + sinf(_scale * y * 10.0f)));
          break;
        }
        case 6: {
          float cx = x + c1x, cy = y + c1y;
          v1 = (uint8_t)(127.0f * (1.0f + sinf(3.0f * atan2f(cy, cx) + _scale * hypotf(cy, cx))));
          cx = x + c2x; cy = y + c2y;
          v2 = (uint8_t)(127.0f * (1.0f + sinf(3.0f * atan2f(cy, cx) + _scale * hypotf(cy, cx))));
          cx = x + c3x; cy = y + c3y;
          v3 = (uint8_t)(127.0f * (1.0f + sinf(3.0f * atan2f(cy, cx) + _scale * hypotf(cy, cx))));
          break;
        }
        case 7:
        case 8: {
          float cx = x + c1x, cy = y + c1y;
          float fv = 127.0f * (1.0f + sinf(3.0f * atan2f(cy, cx) + _scale * hypotf(cy, cx)));
          float d = sqrtf(cx*cx + cy*cy);
          if (d < 0.5f) d = 0.5f;
          d /= 10.0f;
          int16_t cut = (int16_t)(1.0f / (d * d));
          int16_t vv = (int16_t)fv - cut;
          v3 = (uint8_t)constrain(vv, 0, 255);

          cx = x + c2x; cy = y + c2y;
          fv = 127.0f * (1.0f + sinf(3.0f * atan2f(cy, cx) + _scale * hypotf(cy, cx)));
          d = sqrtf(cx*cx + cy*cy);
          if (d < 0.5f) d = 0.5f;
          d /= 10.0f;
          cut = (int16_t)(1.0f / (d * d));
          vv = (int16_t)fv - cut;
          uint8_t v2nd = (uint8_t)constrain(vv, 0, 255);
          if (variant == 7) {
            v1 = v2nd;
          } else {
            v2 = v2nd;
            if (v2nd > v3) v3 = v2nd;
          }
          break;
        }
      }

      CRGB c = CRGB::Black;
      if (v1) c += CRGB(CHSV(hueA, 255, v1));
      if (v2) c += CRGB(CHSV(hueB, 255, v2));
      if (v3) c += CRGB(CHSV(hueC, 255, v3));
      // Treble sparkle: random small white-ish additive pixels at high treble.
      if (audioSparkleP > 0.5f && random8() < (uint8_t)audioSparkleP) {
        c += CRGB(60, 60, 60);
      }
      ctx.leds[ctx.xy(x, y)] = c;
    }
  }
}

void renderPuzzle(EffectCtx& ctx) {
  // Clear entire buffer first — puzzle grid may not cover all pixels
  for (uint16_t i = 0; i < ctx.numLeds; i++) ctx.leds[i] = CRGB::Black;

  if (ctx.w < 4 || ctx.h < 4) return;
  if (ctx.surfaceId >= NUM_VB_SURFACES) return;

  uint8_t psize = (ctx.numLeds > 96) ? 4 : 2;
  PuzzleState& s = puzzleStates[ctx.surfaceId];
  if (!s.inited || s.w != ctx.w || s.h != ctx.h || s.psize != psize) {
    puzzleInit(s, ctx.w, ctx.h, psize);
  }

  CRGBPalette16 pal = puzzleRotatedHeat(ctx.baseHue);

  for (uint8_t x = 0; x < s.pcols; x++) {
    for (uint8_t y = 0; y < s.prows; y++) {
      puzzleFillCell(ctx, pal, x * psize, y * psize, psize, s.puzzle[x][y]);
    }
  }

  uint8_t slideMul = 4 + (ctx.param2 >> 5);
  // Audio: rms scales slide speed; beat onset adds a transient burst.
  float audioSpd = ctx.audioAlive ? (1.0f + 2.0f * ctx.audioRms + ctx.audioBeatEnv) : 1.0f;
  int16_t shspeed = (int16_t)((float)(ctx.speed * slideMul) * audioSpd);
  if (shspeed < 16) shspeed = 16;

  switch (s.etap) {
    case 0: {
      s.xory = !s.xory;
      if (s.xory) {
        if (s.zdot[0] == s.pcols - 1)      s.move[0] = -1;
        else if (s.zdot[0] == 0)           s.move[0] = 1;
        else if (s.move[0] == 0)           s.move[0] = ((random8() & 1) ? 1 : -1);
        s.move[1] = 0;
      } else {
        if (s.zdot[1] == s.prows - 1)      s.move[1] = -1;
        else if (s.zdot[1] == 0)           s.move[1] = 1;
        else if (s.move[1] == 0)           s.move[1] = ((random8() & 1) ? 1 : -1);
        s.move[0] = 0;
      }
      s.etap = 1;
      break;
    }
    case 1: {
      uint8_t tx = s.zdot[0] + s.move[0];
      uint8_t ty = s.zdot[1] + s.move[1];
      s.tmpp = s.puzzle[tx][ty];
      s.puzzle[tx][ty] = 0;
      s.etap = 2;
      break;
    }
    case 2: {
      int32_t x1 = (int32_t)((s.zdot[0] + s.move[0]) * psize) * 256 + s.shift[0];
      int32_t y1 = (int32_t)((s.zdot[1] + s.move[1]) * psize) * 256 + s.shift[1];
      puzzleDrawWuCell(ctx, pal, x1, y1, psize, s.tmpp);
      s.shift[0] -= s.move[0] * shspeed;
      s.shift[1] -= s.move[1] * shspeed;
      int16_t limit = psize * 256;
      if (abs(s.shift[0]) >= limit || abs(s.shift[1]) >= limit) {
        s.shift[0] = 0;
        s.shift[1] = 0;
        s.puzzle[s.zdot[0]][s.zdot[1]] = s.tmpp;
        s.etap = 3;
      }
      break;
    }
    case 3: {
      s.zdot[0] += s.move[0];
      s.zdot[1] += s.move[1];
      s.etap = 0;
      break;
    }
  }
}

// Bumpmap — Perlin noise height field + simulated diffuse light
#define BUMP_MAX_W 42  // supports up to 40-wide hi-res grids (40+2)

void renderBumpmap(EffectCtx& ctx) {
  if (ctx.w > 40 || ctx.h > 40) {
    for (uint16_t i = 0; i < ctx.numLeds; i++) ctx.leds[i] = CRGB::Black;
    return;
  }

  static float audioMulSmooth = 1.0f;
  float audioMulTarget = ctx.audioAlive ? (0.4f + 1.4f * ctx.audioRms) : 1.0f;
  audioMulSmooth = 0.6f * audioMulSmooth + 0.4f * audioMulTarget;
  const float audioMul = audioMulSmooth;

  const float beatEnv         = ctx.audioBeatEnv;
  const float beatBrightBoost = beatEnv * 0.45f;
  const uint8_t beatHueShift  = (uint8_t)(beatEnv * 35.0f);

  static byte bump[BUMP_MAX_W * BUMP_MAX_W];
  const uint8_t bw = ctx.w + 2;

  uint8_t maxDim = (ctx.w > ctx.h) ? ctx.w : ctx.h;
  uint16_t stride = 400 / (maxDim ? maxDim : 1);
  if (stride < 4) stride = 4;

  uint16_t t = ctx.frame;
  for (uint8_t j = 0; j < ctx.h + 2; j++) {
    for (uint8_t i = 0; i < ctx.w + 2; i++) {
      bump[j * bw + i] = inoise8_raw(i * stride, j * stride, t) / 2;
    }
  }

  uint16_t divisor = 8 + ((uint16_t)ctx.param2 * 40) / 255;
  if (ctx.audioAlive) {
    float divFactor = 1.5f - 0.8f * ctx.audioRms;
    if (divFactor < 0.4f) divFactor = 0.4f;
    divisor = (uint16_t)((float)divisor * divFactor);
  }
  if (divisor < 1) divisor = 1;

  int16_t vly = -(int16_t)(ctx.h / 2 + 1);
  for (uint8_t y = 0; y < ctx.h; y++) {
    ++vly;
    int16_t vlx = -(int16_t)(ctx.w / 2 + 1);
    uint8_t jy = y + 1;
    for (uint8_t x = 0; x < ctx.w; x++) {
      ++vlx;
      uint8_t ix = x + 1;
      int16_t nx = (int16_t)(int8_t)bump[jy * bw + (ix + 1)]
                 - (int16_t)(int8_t)bump[jy * bw + (ix - 1)];
      int16_t ny = (int16_t)(int8_t)bump[(jy + 1) * bw + ix]
                 - (int16_t)(int8_t)bump[(jy - 1) * bw + ix];
      int16_t difx = abs(vlx * 7 - nx);
      int16_t dify = abs(vly * 7 - ny);
      int32_t temp = (int32_t)difx * difx + (int32_t)dify * dify;
      int32_t col = 255 - temp / (int32_t)divisor;
      if (col < 0) col = 0;
      if (col > 255) col = 255;
      uint8_t hueDrift = (uint8_t)(ctx.millisNow / 60);
      uint8_t hue = ctx.baseHue + beatHueShift +
                    ((uint8_t)col >> 1) + hueDrift;
      float scaled = (float)col * (audioMul + beatBrightBoost);
      if (scaled > 255.0f) scaled = 255.0f;
      ctx.leds[ctx.xy(x, y)] = CHSV(hue, 255, (uint8_t)scaled);
    }
  }
}

void renderXorcery(EffectCtx& ctx) {
  float now = (float)ctx.millisNow;
  float t1 = fmodf(now / 6553.5f, 1.0f);
  float t2 = t1 * 2.0f * (float)PI;
  float t3 = fmodf(now / 32768.0f, 1.0f);
  float t4 = fmodf(now / 13107.0f, 1.0f) * 2.0f * (float)PI;

  const float m       = 0.3f + pbTriangle(t1) * 0.2f;
  const float h_base  = sinf(t2);
  const float modAmp  = pbTriangle(t3) * 10.0f + 4.0f * sinf(t4);

  // Audio: bass scales xor pattern scale; mid adds hue drift; beat shifts the
  // z-axis XOR offset (creates a momentary kaleidoscopic jump on each beat).
  float audioScale = ctx.audioAlive ? (1.0f + 1.5f * ctx.audioBass) : 1.0f;
  const uint8_t maxDim = (ctx.w > ctx.h) ? ctx.w : ctx.h;
  float xorFactor = (float)maxDim * 0.5f * audioScale;
  const float minXor = 5.0f + (float)ctx.param2 * (10.0f / 255.0f);
  if (xorFactor < minXor) xorFactor = minXor;

  int zi = (int)(xorFactor * -0.5f);
  if (ctx.audioAlive) zi += (int)(ctx.audioBeatEnv * 30.0f);

  const uint8_t denomW = (ctx.w > 1) ? (ctx.w - 1) : 1;
  const uint8_t denomH = (ctx.h > 1) ? (ctx.h - 1) : 1;

  const float hueShift = (float)ctx.baseHue / 255.0f
                       + (ctx.audioAlive ? (ctx.audioMid * 0.3f) : 0.0f);

  for (int y = 0; y < ctx.h; y++) {
    float yn = (float)y / (float)denomH;
    int   yi = (int)(xorFactor * (yn - 0.5f));
    for (int x = 0; x < ctx.w; x++) {
      float xn = (float)x / (float)denomW;
      int   xi = (int)(xorFactor * (xn - 0.5f));

      int xored = xi ^ yi ^ zi;

      float arg = ((float)xored / 50.0f) * modAmp;
      arg = fmodf(arg, m);

      float h = h_base + pbWave(arg);

      float v = fmodf(fabsf(h) + fabsf(m) + t1, 1.0f);
      v = pbTriangle(v * v);
      v = v * v * v;

      float hf = pbTriangle(h) / 5.0f + (xn + yn) / 3.0f + t1 + hueShift;
      hf = hf - floorf(hf);

      uint8_t hue = (uint8_t)(hf * 255.0f);
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
      uint8_t val = (uint8_t)(v * 255.0f);

      ctx.leds[ctx.xy(x, y)] = CHSV(hue, 255, val);
    }
  }
}

void renderHiphotic(EffectCtx& ctx) {
  static uint16_t hipPhase  = 0;
  static uint16_t prevFrame = 0xFFFF;
  // Audio: mid scales phase advancement rate.
  float audioPhase = ctx.audioAlive ? (1.0f + 2.0f * ctx.audioMid) : 1.0f;
  if (ctx.frame != prevFrame) {
    uint8_t adv = ctx.speed / 4;
    if (adv == 0) adv = 1;
    adv = (uint8_t)constrain((int)((float)adv * audioPhase), 1, 255);
    hipPhase += adv;
    prevFrame = ctx.frame;
  }
  uint8_t a = (uint8_t)hipPhase;

  uint8_t maxMul = 6 + (uint8_t)((uint16_t)ctx.param2 * 22 / 255);
  uint8_t mulX, mulY;
  if (ctx.audioAlive) {
    // Audio: bass drives the grid multiplier (replaces beatsin8 "breathing").
    // Beat onset adds a transient pulse for visible accents.
    float spread = (float)(maxMul - 2);  // 0..22ish
    float base = 2.0f + ctx.audioBass * spread;
    float pulse = ctx.audioBeatEnv * 4.0f;
    mulX = (uint8_t)constrain((int)(base + pulse + 0.5f), 2, (int)maxMul);
    // Slight offset on Y so X/Y don't collapse to the same value
    mulY = (uint8_t)constrain((int)(base * 0.9f + pulse + 0.5f), 2, (int)maxMul);
  } else {
    mulX = beatsin8(10, 2, maxMul);
    mulY = beatsin8(12, 2, maxMul);
  }

  for (int y = 0; y < ctx.h; y++) {
    for (int x = 0; x < ctx.w; x++) {
      uint8_t inner = cos8((uint8_t)(x * mulX) + a) +
                      sin8((uint8_t)(y * mulY) + a) +
                      a;
      uint8_t v = sin8(inner);
      uint8_t hue = ctx.baseHue + (v >> 2);
      uint8_t val = 80 + scale8(v, 175);
      ctx.leds[ctx.xy(x, y)] = CHSV(hue, 255, val);
    }
  }
}

// ============================================================================
// Wrapper functions — LED 8x8 and Hi-res LCD
// ============================================================================

#define WRAP_LED(wrapName, renderFn) \
  void wrapName() { EffectCtx ctx; fillCtxLed(ctx, 0); renderFn(ctx); }

void ambientPlasmaLed()     { EffectCtx ctx; fillCtxLed(ctx, 0); renderPlasma(ctx); }
void ambientGalaxyLed()     { EffectCtx ctx; fillCtxLed(ctx, 0); renderGalaxy(ctx); }
void ambientRippleLed()     { EffectCtx ctx; fillCtxLed(ctx, 0); renderRipple(ctx); }
void ambientChevronsLed()   { EffectCtx ctx; fillCtxLed(ctx, 0); renderChevrons(ctx); }
void ambientStripesLed()    { EffectCtx ctx; fillCtxLed(ctx, 0); renderStripes(ctx); }
void ambientCheckerLed()    { EffectCtx ctx; fillCtxLed(ctx, 0); renderChecker(ctx); }
void ambientScanlineLed()   { EffectCtx ctx; fillCtxLed(ctx, 0); renderScanline(ctx); }
void ambientPerlinLed()     { EffectCtx ctx; fillCtxLed(ctx, 0); renderPerlin(ctx); }
void ambientDistorsionLed() { EffectCtx ctx; fillCtxLed(ctx, 0); renderDistorsion(ctx); }
void ambientZVortexLed()    { EffectCtx ctx; fillCtxLed(ctx, 0); renderZVortex(ctx); }
void ambientSnakesLed()     { EffectCtx ctx; fillCtxLed(ctx, 0); renderSnakes(ctx); }
void ambientSinusoidLed()   { EffectCtx ctx; fillCtxLed(ctx, 0); renderSinusoid(ctx); }
void ambientPuzzleLed()     { EffectCtx ctx; fillCtxLed(ctx, 0); renderPuzzle(ctx); }
void ambientBumpmapLed()    { EffectCtx ctx; fillCtxLed(ctx, 0); renderBumpmap(ctx); }
void ambientXorceryLed()    { EffectCtx ctx; fillCtxLed(ctx, 0); renderXorcery(ctx); }
void ambientHiphoticLed()   { EffectCtx ctx; fillCtxLed(ctx, 0); renderHiphotic(ctx); }

#if defined(HIRES_ENABLED)
void ambientPlasmaHiRes()     { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderPlasma(ctx);     blitHiRes(hiResCrgbBuf); }
void ambientGalaxyHiRes()     { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderGalaxy(ctx);     blitHiRes(hiResCrgbBuf); }
void ambientRippleHiRes()     { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderRipple(ctx);     blitHiRes(hiResCrgbBuf); }
void ambientChevronsHiRes()   { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderChevrons(ctx);   blitHiRes(hiResCrgbBuf); }
void ambientStripesHiRes()    { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderStripes(ctx);    blitHiRes(hiResCrgbBuf); }
void ambientCheckerHiRes()    { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderChecker(ctx);    blitHiRes(hiResCrgbBuf); }
void ambientScanlineHiRes()   { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderScanline(ctx);   blitHiRes(hiResCrgbBuf); }
void ambientPerlinHiRes()     { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderPerlin(ctx);     blitHiRes(hiResCrgbBuf); }
void ambientDistorsionHiRes() { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderDistorsion(ctx); blitHiRes(hiResCrgbBuf); }
void ambientZVortexHiRes()    { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderZVortex(ctx);    blitHiRes(hiResCrgbBuf); }
void ambientSnakesHiRes()     { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderSnakes(ctx);     blitHiRes(hiResCrgbBuf); }
void ambientSinusoidHiRes()   { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderSinusoid(ctx);   blitHiRes(hiResCrgbBuf); }
void ambientPuzzleHiRes()     { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderPuzzle(ctx);     blitHiRes(hiResCrgbBuf); }
void ambientBumpmapHiRes()    { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderBumpmap(ctx);    blitHiRes(hiResCrgbBuf); }
void ambientXorceryHiRes()    { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderXorcery(ctx);    blitHiRes(hiResCrgbBuf); }
void ambientHiphoticHiRes()   { EffectCtx ctx; fillCtxHiRes(ctx, hiResCrgbBuf, 1); renderHiphotic(ctx);   blitHiRes(hiResCrgbBuf); }
#endif

// ============================================================================
// Function pointer tables
// ============================================================================
typedef void (*AmbientFunc)();

const AmbientFunc ambientLedFuncs[NUM_AMBIENT_EFFECTS] = {
  ambientPlasmaLed,     ambientGalaxyLed,     ambientRippleLed,
  ambientChevronsLed,   ambientStripesLed,    ambientCheckerLed,
  ambientScanlineLed,   ambientPerlinLed,     ambientDistorsionLed,
  ambientZVortexLed,    ambientSnakesLed,     ambientSinusoidLed,
  ambientPuzzleLed,     ambientBumpmapLed,    ambientXorceryLed,
  ambientHiphoticLed
};

#if defined(HIRES_ENABLED)
const AmbientFunc ambientHiResFuncs[NUM_AMBIENT_EFFECTS] = {
  ambientPlasmaHiRes,     ambientGalaxyHiRes,     ambientRippleHiRes,
  ambientChevronsHiRes,   ambientStripesHiRes,    ambientCheckerHiRes,
  ambientScanlineHiRes,   ambientPerlinHiRes,     ambientDistorsionHiRes,
  ambientZVortexHiRes,    ambientSnakesHiRes,     ambientSinusoidHiRes,
  ambientPuzzleHiRes,     ambientBumpmapHiRes,    ambientXorceryHiRes,
  ambientHiphoticHiRes
};

// Raw render function table (for pixel-mode dispatch)
typedef void (*RenderFn)(EffectCtx&);
const RenderFn renderFuncs[NUM_AMBIENT_EFFECTS] = {
  renderPlasma,     renderGalaxy,     renderRipple,
  renderChevrons,   renderStripes,    renderChecker,
  renderScanline,   renderPerlin,     renderDistorsion,
  renderZVortex,    renderSnakes,     renderSinusoid,
  renderPuzzle,     renderBumpmap,    renderXorcery,
  renderHiphotic
};

// Pixel-mode rendering: expanded resolution, fills LCD screen
void runPixelEffect(uint8_t index) {
  if (index >= NUM_AMBIENT_EFFECTS) return;
  EffectCtx ctx;
  fillCtxPixel(ctx);
  renderFuncs[index](ctx);
  applyKaleidoscope(pixelModeBuf, PIXEL_MODE_COLS, PIXEL_MODE_ROWS);
  blitPixelMode(pixelModeBuf);
}
#endif

// Run ambient effect by index
void runAmbientEffect(uint8_t index) {
  if (index >= NUM_AMBIENT_EFFECTS) return;
  #if defined(HIRES_ENABLED)
  if (hiResMode && !menuVisible && gfx != nullptr) {
    ambientHiResFuncs[index]();
    return;
  }
  #endif
  ambientLedFuncs[index]();
  applyKaleidoscope(leds, MATRIX_WIDTH, MATRIX_HEIGHT);
}

#endif // EFFECTS_AMBIENT_H
