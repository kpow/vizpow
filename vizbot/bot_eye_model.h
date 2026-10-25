#ifndef BOT_EYE_MODEL_H
#define BOT_EYE_MODEL_H

#include <Arduino.h>
#include <stddef.h>
#include "config.h"
#include "bot_faces.h"
#include "bot_eye_params.h"

// ============================================================================
// Bot Eye Model — one parametric eye per side, lids, damped springs
// ============================================================================
// Port of the eye lab's "after" model (docs/eye-art-direction.md §3-4, with
// the round 2 and round 4 reviews applied). Per-face values come from
// bot_eye_params.h; brow angle/length/thickness/visibility still come from
// the BotExpression table in bot_faces.h, and mouths are drawn by bot_eyes.h.
//
// step() moves every value toward the current face with time-based springs
// (blinks, gaze, idle fidgets and swaps included). render() draws the eyes as
// one vertical span per pixel column, solid fills, no antialiasing, straight
// to the canvas. Pupils and the X / spiral overlays are black cut-outs clipped
// per column to that span. No per-frame heap: everything lives in the small
// static column buffers below.
// ============================================================================

extern GfxDevice *gfx;

#define BOT_EYE_MAX_COLS     160   // columns per eye (R 48 eye at its widest is ~125)
#define BOT_EYE_MAX_CUTS     6     // cut-out runs per column (spiral arms need 4)
#define BOT_EYE_POLY_N       44    // pupil outline points
#define BOT_EYE_BLINK_QUEUE  6
#define BOT_EYE_GAZE_MAX_X   0.35f // gaze clamp, x R
#define BOT_EYE_GAZE_MAX_Y   0.25f
#define BOT_EYE_BLINK_CLOSE_MS 80
#define BOT_EYE_BLINK_OPEN_MS  140

static_assert(sizeof(botEyeParams) / sizeof(botEyeParams[0]) == BOT_NUM_EXPRESSIONS,
              "bot_eye_params.h must have one row per expression");

// ============================================================================
// Helpers
// ============================================================================

static inline float eyeClamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float eyeLerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float eyeSmooth(float x) { return x <= 0 ? 0 : (x >= 1 ? 1 : x * x * (3 - 2 * x)); }
static inline float eyeBump(float p, float a, float b) { return eyeClamp((p - a) / (b - a), 0, 1); }
static inline float eyeRnd01() { return (float)random(1L << 24) * (1.0f / 16777216.0f); }
static inline float eyeRnd(float a, float b) { return a + (b - a) * eyeRnd01(); }
static inline bool eyeReached(unsigned long now, unsigned long at) { return (long)(now - at) >= 0; }
// Seconds on a one-hour wrap: keeps float precision for sin() however long the bot runs
static inline float eyeSecs(unsigned long now) { return (float)(now % 3600000UL) * 0.001f; }

// ============================================================================
// Damped spring
// ============================================================================

struct BotSpring {
  float x, v;   // value, velocity
  float f, z;   // natural frequency (Hz), damping ratio
  float t;      // last target (for the at-rest test)

  void init(float x0, float f0, float z0) { x = t = x0; v = 0; f = f0; z = z0; }
  void snap(float target) { x = t = target; v = 0; }

  // Semi-implicit Euler in <= 8 ms sub-steps, so motion is the same at any frame rate
  float step(float target, float dt) {
    t = target;
    float w = TWO_PI * f;
    int n = (int)ceilf(dt / 0.008f);
    if (n < 1) n = 1;
    float h = dt / n;
    for (int i = 0; i < n; i++) {
      v += (w * w * (target - x) - 2.0f * z * w * v) * h;
      x += v * h;
    }
    if (!isfinite(x) || !isfinite(v)) { x = target; v = 0; }
    return x;
  }
};

// Largest excursion after a unit velocity kick: kicks are divided by this so a
// pop of 0.06 peaks at 0.06 whatever the spring's tuning.
static float botSpringPeak(float f, float z) {
  BotSpring s;
  s.init(0, f, z);
  s.v = 1;
  float m = 0;
  for (int i = 0; i < 2000; i++) { s.step(0, 0.001f); m = max(m, fabsf(s.x)); }
  return m;
}

#define BOT_EYE_F_POP (1.0f / (TWO_PI * 0.12f))   // critically damped bump peaking at 120 ms

// ============================================================================
// Parameter keys
// ============================================================================
// Shape keys spring at the face's transition speed; pupil keys at 3 Hz.

enum BotEyeKey : uint8_t {
  EK_ASPECT = 0, EK_SCALE, EK_LIDTOP, EK_SLANT, EK_SAG, EK_LIDBOT, EK_BOW,
  EK_SHUT, EK_CUP, EK_INSET, EK_PDY,
  EK_PW, EK_PH, EK_HEART, EK_STAR,
  EK_COUNT
};
#define EK_SHAPE_COUNT EK_PW

static const float* botEyePair(const BotEyeParams& p, uint8_t k) {
  switch (k) {
    case EK_ASPECT: return p.aspect;  case EK_SCALE:  return p.scale;
    case EK_LIDTOP: return p.lidTop;  case EK_SLANT:  return p.slant;
    case EK_SAG:    return p.sag;     case EK_LIDBOT: return p.lidBot;
    case EK_BOW:    return p.bow;     case EK_SHUT:   return p.shut;
    case EK_CUP:    return p.cup;     case EK_INSET:  return p.inset;
    case EK_PDY:    return p.pdy;     case EK_PW:     return p.pw;
    case EK_PH:     return p.ph;      case EK_HEART:  return p.heart;
    default:        return p.star;
  }
}

// Pupil presets used by entries, fidgets and idle looks (pw = ph, x R)
enum BotPupilPreset : uint8_t { PUP_NONE = 0, PUP_SMALL, PUP_DOT, PUP_BIG };
static const float BOT_PUPIL_PRESET[] = { 0.0f, 0.18f, 0.08f, 0.36f };

// Why a blink started (swap and exit blinks ignore the no-blink rules)
enum BotBlinkWhy : uint8_t { BW_AUTO = 0, BW_DOUBLE, BW_SWAP, BW_EXIT, BW_SACCADE, BW_FIDGET };

// ============================================================================
// Idle fidgets (NEUTRAL only)
// ============================================================================

enum BotFidget : uint8_t { FID_NONE = 0, FID_EYEROLL, FID_PEEK, FID_GLANCE, FID_TRIPLE, FID_YAWN };
static const uint16_t BOT_FIDGET_MS[] = { 0, 1700, 2300, 4200, 1100, 3000 };

struct BotFidgetOut {
  uint8_t n;            // eye overrides, applied to both eyes
  uint8_t key[3];
  float val[3];
  uint8_t pupil;        // BotPupilPreset
  bool gaze, pursuit;   // gaze target and how to get there
  float gx, gy;
};

static void botFidgetEval(uint8_t id, float p, BotFidgetOut &o) {
  o.n = 0; o.pupil = PUP_NONE; o.gaze = false; o.pursuit = false; o.gx = o.gy = 0;
  switch (id) {
    case FID_EYEROLL: {
      float a = -HALF_PI - p * TWO_PI;
      o.n = 1; o.key[0] = EK_LIDTOP; o.val[0] = 0.22f;
      o.pupil = PUP_SMALL;
      o.gaze = true; o.pursuit = true; o.gx = cosf(a) * 0.24f; o.gy = sinf(a) * 0.16f;
      break;
    }
    case FID_PEEK:
      o.gaze = true;
      if (p < 0.55f) {
        o.n = 2; o.key[0] = EK_LIDTOP; o.val[0] = 0.42f; o.key[1] = EK_LIDBOT; o.val[1] = 0.2f;
        o.gx = 0.3f;
      } else {
        o.n = 1; o.key[0] = EK_SCALE; o.val[0] = 1.06f;
        o.pupil = PUP_DOT;
      }
      break;
    case FID_GLANCE: {
      static const float seq[5][2] = { {-0.26f, -0.04f}, {0.26f, -0.04f}, {0.18f, -0.13f}, {-0.1f, 0.08f}, {0, 0} };
      int s = min(4, (int)floorf(p * 5));
      o.gaze = true; o.gx = seq[s][0]; o.gy = seq[s][1];
      break;
    }
    case FID_YAWN: {
      float c = eyeBump(p, 0, 0.25f) * (1 - eyeBump(p, 0.62f, 0.72f));
      float w = eyeBump(p, 0.68f, 0.76f) * (1 - eyeBump(p, 0.85f, 1));
      o.n = 3;
      o.key[0] = EK_SHUT; o.val[0] = c;
      o.key[1] = EK_CUP; o.val[1] = 0.3f * c;
      o.key[2] = EK_ASPECT; o.val[2] = 0.92f + 0.1f * w;
      if (w > 0) o.pupil = PUP_SMALL;
      o.gaze = true; o.pursuit = true; o.gy = -0.08f * c;
      break;
    }
    default: break;   // FID_TRIPLE only queues blinks
  }
}

// Idle looks between fidgets
enum BotIdleLook : uint8_t { LOOK_NONE = 0, LOOK_PUPIL, LOOK_CURIOUS, LOOK_BORED };

// ============================================================================
// Raster buffers (static, reused every frame)
// ============================================================================

struct BotEyeGeom {
  float ex, ey;          // eye centre
  float R, Rb, Rw, Hh;   // base R, scaled R, drawn half-width, half-height
  float ltRaw, lt, lb;   // top lid (un-blinked / blinked), bottom lid
  float sl, sag, bow, shut, cup, pup;
  int8_t side, inS;      // -1 left / +1 right; +1 when +u points at the nose
};

// One eye's columns: span rows (bot < top = empty) and the cut-out runs inside
struct BotEyeCols {
  int16_t x0, n;
  int16_t top[BOT_EYE_MAX_COLS], bot[BOT_EYE_MAX_COLS];
  uint8_t cn[BOT_EYE_MAX_COLS];
  int16_t ca[BOT_EYE_MAX_COLS][BOT_EYE_MAX_CUTS], cb[BOT_EYE_MAX_COLS][BOT_EYE_MAX_CUTS];
};

// Row bands of the drawn eye and their horizontal shift (glitch), output columns
struct BotEyeRows { int16_t r0, r1, dx; };
struct BotEyeOut {
  BotEyeRows rows[7];
  uint8_t nRows;
  int16_t x0, x1;
};

static BotEyeCols botEyeCols[2];
static BotEyeOut botEyeOut[2];
static int16_t botStrokeTop[LCD_WIDTH], botStrokeBot[LCD_WIDTH];   // union extents per column
static int16_t botStrokeA[LCD_WIDTH], botStrokeB[LCD_WIDTH];       // dilated by the stroke disc

// Pupil outline unit tables: circle, heart and star terms per point
static float botPolyCos[BOT_EYE_POLY_N], botPolySin[BOT_EYE_POLY_N];
static float botPolyHX[BOT_EYE_POLY_N], botPolyHY[BOT_EYE_POLY_N];
static float botPolySX[BOT_EYE_POLY_N], botPolySY[BOT_EYE_POLY_N];

static void botEyeInitTables() {
  for (int i = 0; i < BOT_EYE_POLY_N; i++) {
    float a = (float)i / BOT_EYE_POLY_N * TWO_PI, ca = cosf(a), sa = sinf(a);
    botPolyCos[i] = ca;
    botPolySin[i] = sa;
    float t = a + HALF_PI, sn = sinf(t);
    botPolyHX[i] = sn * sn * sn;
    botPolyHY[i] = -(13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t)) / 16.0f - 0.15f;
    botPolySX[i] = (ca < 0 ? -1 : 1) * powf(fabsf(ca), 2.6f) * 1.12f;
    botPolySY[i] = (sa < 0 ? -1 : 1) * powf(fabsf(sa), 2.6f) * 1.12f;
  }
}

static inline void botVSpan(int16_t x, int16_t a, int16_t b, uint16_t c) {
  if (x < 0 || x >= LCD_WIDTH) return;
  if (a < 0) a = 0;
  if (b > LCD_HEIGHT - 1) b = LCD_HEIGHT - 1;
  if (b >= a) gfx->drawFastVLine(x, a, b - a + 1, c);
}

// Add a cut-out run to column i, clipped to the eye span, merged with what is there
static void botCutAdd(BotEyeCols &c, int i, int16_t a, int16_t b) {
  if (a < c.top[i]) a = c.top[i];
  if (b > c.bot[i]) b = c.bot[i];
  if (a > b) return;
  int16_t *A = c.ca[i], *B = c.cb[i];
  uint8_t w = 0;
  for (uint8_t r = 0; r < c.cn[i]; r++) {
    if (B[r] + 1 < a || A[r] > b + 1) { A[w] = A[r]; B[w] = B[r]; w++; }
    else { a = min(a, A[r]); b = max(b, B[r]); }
  }
  if (w == BOT_EYE_MAX_CUTS) { w--; a = min(a, A[w]); b = max(b, B[w]); }
  A[w] = a; B[w] = b;
  c.cn[i] = w + 1;
}

// Capsule (segment + radius) cut, one interval per column: the shape is convex,
// so the column's run is the union of the two end discs and the swept band.
static void botCapsuleCut(BotEyeCols &c, float ax, float ay, float bx, float by, float r) {
  int xa = max((int)c.x0, (int)floorf(min(ax, bx) - r));
  int xb = min((int)(c.x0 + c.n - 1), (int)ceilf(max(ax, bx) + r));
  float dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
  if (L2 < 1e-6f) L2 = 1e-6f;
  float L = sqrtf(L2), nx = -dy / L, ny = dx / L, r2 = r * r;
  for (int x = xa; x <= xb; x++) {
    float xc = x + 0.5f, px = xc - ax, lo = 1e9f, hi = -1e9f;
    float d0 = xc - ax, d1 = xc - bx;
    if (d0 * d0 <= r2) { float h = sqrtf(r2 - d0 * d0); lo = min(lo, ay - h); hi = max(hi, ay + h); }
    if (d1 * d1 <= r2) { float h = sqrtf(r2 - d1 * d1); lo = min(lo, by - h); hi = max(hi, by + h); }
    if (fabsf(ny) > 1e-6f) {
      float p1 = (-r - nx * px) / ny, p2 = (r - nx * px) / ny;
      float plo = min(p1, p2), phi = max(p1, p2), qlo = -1e9f, qhi = 1e9f;
      if (fabsf(dy) > 1e-6f) {
        float q1 = -dx * px / dy, q2 = (L2 - dx * px) / dy;
        qlo = min(q1, q2); qhi = max(q1, q2);
      } else if (dx * px < 0 || dx * px > L2) {
        qlo = 1; qhi = 0;
      }
      float ilo = max(plo, qlo), ihi = min(phi, qhi);
      if (ilo <= ihi) { lo = min(lo, ay + ilo); hi = max(hi, ay + ihi); }
    } else if (fabsf(px) <= r) {
      lo = min(lo, min(ay, by)); hi = max(hi, max(ay, by));
    }
    if (lo > hi) continue;
    botCutAdd(c, x - c.x0, (int16_t)ceilf(lo - 0.5f), (int16_t)floorf(hi - 0.5f));
  }
}

// Filled polygon cut, scanned per column at pixel centres (even-odd)
static void botPolyCut(BotEyeCols &c, const float *px, const float *py, int n) {
  float minX = 1e9f, maxX = -1e9f;
  for (int i = 0; i < n; i++) { minX = min(minX, px[i]); maxX = max(maxX, px[i]); }
  int xa = max((int)c.x0, (int)floorf(minX)), xb = min((int)(c.x0 + c.n - 1), (int)ceilf(maxX));
  float ys[8];
  for (int x = xa; x <= xb; x++) {
    float xc = x + 0.5f;
    int m = 0;
    for (int i = 0; i < n && m < 8; i++) {
      int j = (i + 1 == n) ? 0 : i + 1;
      float ax = px[i], bx = px[j];
      if ((ax <= xc && bx > xc) || (bx <= xc && ax > xc)) {
        float y = py[i] + (xc - ax) * (py[j] - py[i]) / (bx - ax);
        int k = m++;
        while (k > 0 && ys[k - 1] > y) { ys[k] = ys[k - 1]; k--; }
        ys[k] = y;
      }
    }
    for (int k = 0; k + 1 < m; k += 2) {
      botCutAdd(c, x - c.x0, (int16_t)ceilf(ys[k] - 0.5f), (int16_t)floorf(ys[k + 1] - 0.5f));
    }
  }
}

// ============================================================================
// The model
// ============================================================================

struct BotEyeModel {
  // Springs: [0] = screen-left eye
  BotSpring eye[2][EK_COUNT];
  BotSpring browAng[2], browLen[2], browThick[2];
  BotSpring gx, gy;                 // gaze, x R
  BotSpring popS, popA, bStr;       // entry pop (scale, aspect), blink squash

  // Current face
  uint8_t expr;
  const BotEyeParams *P;
  int8_t browAngT[2];
  int16_t browLenT, browThickT;
  bool browVisT;
  unsigned long enteredAt;

  // Overlay and brows swap under a blink
  BotEyeOverlay overlay;
  bool brows;
  bool pending;
  BotEyeOverlay pendOverlay;
  bool pendBrows;

  // Blink: phase 0 open, 1 closing, 2 held shut, 3 opening
  uint8_t bPh;
  unsigned long bT, bNext, bLast;
  float b;                          // 0 open .. 1 shut
  struct { unsigned long at; uint8_t why; } bq[BOT_EYE_BLINK_QUEUE];
  uint8_t bqn;

  // Gaze
  float tx, ty, gxOut, gyOut;
  bool pursuit;
  unsigned long gNext;
  int8_t gSide;
  bool gUp, gMicro;

  // Idle fidgets and looks
  uint8_t fid, lastFid, look, lookPupil;
  bool fidForced;
  unsigned long fidT0, nextFid, nextLook, lookUntil;
  float fidP;

  // Glitch row bands per eye, star spin, spiral phase, entry pupil
  struct { float y0, h, dx; } band[2][3];
  bool bandSet[2];
  unsigned long bandNext[2];
  float spinA, spiralA;
  bool smallOn;
  unsigned long smallUntil;

  // Inputs, set by bot_mode before step()
  bool sleeping;                    // drive shut, no blinks, gaze centred
  bool pendant;                     // low power: ζ 1, slower idle, redraw only when moving
  bool idle;                        // auto blinks, gaze patterns, fidgets
  unsigned long lastInteractMs;     // the yawn waits for 5 quiet minutes
  float padX, padY;                 // tilt look input, -1..1 (pursuit while engaged)

  unsigned long lastStepMs;
  uint16_t frameMs;
  bool ready;

  // Spring peaks for the kicks, computed once
  float pkPopS, pkPopA, pkPopA1, pkPup, pkPup1;

  // ---------------------------------------------------------------- setup
  void init(unsigned long now) {
    botEyeInitTables();
    for (int e = 0; e < 2; e++) {
      for (int k = 0; k < EK_COUNT; k++) eye[e][k].init(0, 2, 0.85f);
      browAng[e].init(0, 2, 0.85f);
      browLen[e].init(0, 2, 0.85f);
      browThick[e].init(0, 2, 0.85f);
      bandSet[e] = false;
      bandNext[e] = now;
    }
    gx.init(0, 6, 1); gy.init(0, 6, 1);
    popS.init(0, BOT_EYE_F_POP, 1); popA.init(0, 2.2f, 0.45f); bStr.init(0, BOT_EYE_F_POP, 1);
    pkPopS = botSpringPeak(BOT_EYE_F_POP, 1);
    pkPopA = botSpringPeak(2.2f, 0.45f);
    pkPopA1 = botSpringPeak(2.2f, 1);
    pkPup = botSpringPeak(3, 0.6f);
    pkPup1 = botSpringPeak(3, 1);

    bPh = 0; b = 0; bqn = 0;
    bT = now; bLast = now - 100000UL;
    bNext = now + (unsigned long)eyeRnd(2600, 6800);
    tx = ty = gxOut = gyOut = 0;
    pursuit = false; gSide = 1; gUp = gMicro = false; gNext = now;
    fid = lastFid = FID_NONE; look = LOOK_NONE; lookPupil = PUP_NONE;
    fidForced = false; fidP = 0; fidT0 = now;
    spinA = spiralA = 0;
    sleeping = false; pendant = false; idle = true;
    lastInteractMs = now; padX = padY = 0;
    lastStepMs = now; frameMs = 50;
    expr = EXPR_NEUTRAL;
    setExpr(EXPR_NEUTRAL, now, true);
    ready = true;
  }

  bool padEngaged() const { return fabsf(padX) > 0.02f || fabsf(padY) > 0.02f; }
  float shutMin() const { return min(eye[0][EK_SHUT].x, eye[1][EK_SHUT].x); }

  // ---------------------------------------------------------------- blinks
  bool blinkAllowed(unsigned long now) const {
    if (sleeping) return false;
    if (shutMin() >= 0.5f) return false;
    if (P->noBlinkMs && now - enteredAt < P->noBlinkMs) return false;
    return true;
  }

  unsigned long nextAutoBlink(unsigned long now) const {
    return now + (unsigned long)(pendant ? eyeRnd(4000, 9000) : eyeRnd(2600, 6800) / P->blinkRate);
  }

  void queueBlink(unsigned long at, uint8_t why, bool front = false) {
    if (bqn == BOT_EYE_BLINK_QUEUE) return;
    if (front) {
      for (uint8_t i = bqn; i > 0; i--) bq[i] = bq[i - 1];
      bq[0].at = at; bq[0].why = why;
    } else {
      bq[bqn].at = at; bq[bqn].why = why;
    }
    bqn++;
  }

  bool blinkStart(unsigned long now, uint8_t why) {
    if (bPh != 0) return false;
    if (why != BW_SWAP && why != BW_EXIT && !blinkAllowed(now)) return false;
    bPh = 1; bT = now; bLast = now;
    // Any blink restarts the auto interval; auto blinks set it in updateBlink()
    if (why != BW_AUTO) bNext = nextAutoBlink(now);
    if (why == BW_AUTO && !pendant && eyeRnd01() < 0.18f) queueBlink(now + 320, BW_DOUBLE);
    return true;
  }

  void applySwap() {
    if (!pending) return;
    overlay = pendOverlay;
    brows = pendBrows;
    pending = false;
  }

  // Overlay and brow changes wait for the lids to be shut
  void requestSwap(unsigned long now, BotEyeOverlay ov, bool br) {
    if (ov == overlay && br == brows) { pending = false; return; }
    pending = true; pendOverlay = ov; pendBrows = br;
    if (shutMin() >= 0.5f || sleeping) { applySwap(); return; }
    if (bPh == 2) applySwap();
    else if (bPh == 3) queueBlink(now, BW_SWAP, true);
    else if (bPh == 0) blinkStart(now, BW_SWAP);
  }

  // 80 ms close, held one frame, 140 ms open
  void updateBlink(unsigned long now) {
    if (bPh == 0) {
      if (bqn && eyeReached(now, bq[0].at)) {
        uint8_t why = bq[0].why;
        for (uint8_t i = 1; i < bqn; i++) bq[i - 1] = bq[i];
        bqn--;
        blinkStart(now, why);
      } else if (idle && eyeReached(now, bNext)) {
        bNext = nextAutoBlink(now);
        blinkStart(now, BW_AUTO);
      }
    }
    if (bPh == 0 && pending && shutMin() >= 0.5f) applySwap();
    unsigned long hold = max(30UL, (unsigned long)frameMs + 1);
    for (int guard = 0; guard < 5; guard++) {
      unsigned long e = now - bT;
      if (bPh == 1) {
        if (e >= BOT_EYE_BLINK_CLOSE_MS) { bPh = 2; bT += BOT_EYE_BLINK_CLOSE_MS; applySwap(); continue; }
        b = eyeSmooth(e / (float)BOT_EYE_BLINK_CLOSE_MS);
        break;
      }
      if (bPh == 2) {
        if (e >= hold) { bPh = 3; bT += hold; bStr.v += 0.03f / pkPopS; continue; }
        b = 1;
        break;
      }
      if (bPh == 3) {
        if (e >= BOT_EYE_BLINK_OPEN_MS) { bPh = 0; b = 0; break; }
        b = 1 - eyeSmooth(e / (float)BOT_EYE_BLINK_OPEN_MS);
        break;
      }
      b = 0;
      break;
    }
  }

  // ---------------------------------------------------------------- gaze
  void newSacc(unsigned long now, float x, float y) {
    float d = hypotf(x - tx, y - ty);
    tx = x; ty = y; pursuit = false;
    if (d > 0.25f && now - bLast > 1500 && eyeRnd01() < 0.25f) blinkStart(now, BW_SACCADE);
  }

  void startFidget(unsigned long now, uint8_t id, bool forced) {
    fid = id; fidT0 = now; fidP = 0; fidForced = forced; lastFid = id;
    if (id == FID_TRIPLE) {
      queueBlink(now, BW_FIDGET);
      queueBlink(now + 370, BW_FIDGET);
      queueBlink(now + 740, BW_FIDGET);
    }
  }

  void updateIdle(unsigned long now) {
    if (fid) {
      float p = (float)(now - fidT0) / BOT_FIDGET_MS[fid];
      if (p >= 1) { fid = FID_NONE; nextFid = now + (unsigned long)eyeRnd(8000, 16000); }
      else fidP = p;
    }
    bool neutral = expr == EXPR_NEUTRAL && idle && !pendant && !sleeping && !padEngaged();
    if (!neutral) {
      if (fid && !fidForced) fid = FID_NONE;
      look = LOOK_NONE;
      return;
    }
    if (look && eyeReached(now, lookUntil)) {
      look = LOOK_NONE;
      nextLook = now + (unsigned long)eyeRnd(9000, 22000);
    }
    if (!fid && !look && eyeReached(now, nextFid)) {
      uint8_t names[5], n = 0;
      for (uint8_t f = FID_EYEROLL; f <= FID_TRIPLE; f++) if (f != lastFid) names[n++] = f;
      if (now - lastInteractMs > 300000UL && lastFid != FID_YAWN) names[n++] = FID_YAWN;
      startFidget(now, names[random(n)], false);
    }
    if (!fid && !look && eyeReached(now, nextLook)) {
      if (eyeRnd01() < 0.3f) {
        look = eyeRnd01() < 0.5f ? LOOK_CURIOUS : LOOK_BORED;
        lookUntil = now + 2600;
      } else {
        static const uint8_t pick[3] = { PUP_DOT, PUP_BIG, PUP_SMALL };
        look = LOOK_PUPIL;
        lookPupil = pick[random(3)];
        lookUntil = now + (unsigned long)eyeRnd(1800, 4500);
      }
    }
  }

  void updateGaze(unsigned long now) {
    float bx = P->gazeX, by = P->gazeY;
    if (padEngaged()) {
      tx = padX * BOT_EYE_GAZE_MAX_X; ty = padY * BOT_EYE_GAZE_MAX_Y; pursuit = true;
      return;
    }
    if (sleeping) { tx = ty = 0; pursuit = true; return; }
    if (fid) {
      BotFidgetOut o;
      botFidgetEval(fid, fidP, o);
      if (o.gaze) {
        if (!o.pursuit) {
          if (fabsf(o.gx - tx) > 0.005f || fabsf(o.gy - ty) > 0.005f) newSacc(now, o.gx, o.gy);
        } else {
          tx = o.gx; ty = o.gy; pursuit = true;
        }
        return;
      }
    }
    if (!idle) { tx = bx; ty = by; pursuit = false; return; }
    BotGazePattern pat = (pendant && P->gaze != GAZE_IDLE) ? GAZE_IDLE : P->gaze;
    bool due = eyeReached(now, gNext);
    switch (pat) {
      case GAZE_THINKING: {
        float w = TWO_PI * 0.15f * eyeSecs(now);
        tx = bx + 0.14f + 0.12f * cosf(w);
        ty = by - 0.12f + 0.08f * sinf(w);
        pursuit = true;
        break;
      }
      case GAZE_NERVOUS:
      case GAZE_WORRIED:
        if (due) {
          newSacc(now, bx + eyeRnd(-0.15f, 0.15f), by + eyeRnd(-0.1f, 0.1f));
          gNext = now + (unsigned long)(eyeRnd(260, 500) * (pat == GAZE_WORRIED ? 2 : 1));
        }
        break;
      case GAZE_SHY:
        if (due) {
          gUp = !gUp;
          if (gUp) { newSacc(now, bx - 0.08f, by - 0.06f); gNext = now + 450; }
          else { newSacc(now, bx - 0.2f, by + 0.18f); gNext = now + 3000; }
        }
        break;
      case GAZE_FOCUSED:
        if (due) {
          gMicro = !gMicro;
          float a = eyeRnd(0, TWO_PI);
          newSacc(now, gMicro ? bx + 0.05f * cosf(a) : bx, gMicro ? by + 0.05f * sinf(a) : by);
          gNext = now + (unsigned long)eyeRnd(6000, 10000);
        }
        break;
      case GAZE_ANNOYED:
        if (due) {
          gSide = -gSide;
          newSacc(now, bx + gSide * 0.28f, by);
          gNext = now + 4000;
        }
        break;
      case GAZE_SHOCKED:
        tx = bx; ty = by; pursuit = false;
        break;
      default:
        if (due) {
          bool centre = eyeRnd01() < 0.15f;
          newSacc(now, centre ? bx : bx + eyeRnd(-0.22f, 0.22f), centre ? by : by + eyeRnd(-0.14f, 0.14f));
          gNext = now + (unsigned long)(pendant ? eyeRnd(8000, 15000) : eyeRnd(2800, 6600));
        }
        break;
    }
  }

  // Per-eye targets: the face, then idle looks, fidgets, the entry pupil, sleep
  void targets(unsigned long now, float T[EK_COUNT][2]) {
    for (uint8_t k = 0; k < EK_COUNT; k++) {
      const float *p = botEyePair(*P, k);
      T[k][0] = p[0]; T[k][1] = p[1];
    }
    uint8_t pupil = PUP_NONE;
    if (look == LOOK_PUPIL) pupil = lookPupil;
    else if (look == LOOK_CURIOUS) {
      T[EK_LIDTOP][0] = 0; T[EK_LIDTOP][1] = 0.18f;
      T[EK_SCALE][0] = 1.06f; T[EK_SCALE][1] = 0.95f;
    } else if (look == LOOK_BORED) {
      T[EK_LIDTOP][0] = T[EK_LIDTOP][1] = 0.44f;
    }
    if (fid) {
      BotFidgetOut o;
      botFidgetEval(fid, fidP, o);
      for (uint8_t i = 0; i < o.n; i++) T[o.key[i]][0] = T[o.key[i]][1] = o.val[i];
      if (o.pupil) pupil = o.pupil;
    }
    if (smallOn && !eyeReached(now, smallUntil)) pupil = PUP_SMALL;
    if (pupil) {
      float v = BOT_PUPIL_PRESET[pupil];
      T[EK_PW][0] = T[EK_PW][1] = T[EK_PH][0] = T[EK_PH][1] = v;
    }
    if (sleeping) {
      T[EK_SHUT][0] = T[EK_SHUT][1] = 1;
      T[EK_CUP][0] = T[EK_CUP][1] = 0;
    }
  }

  // ---------------------------------------------------------------- face change
  void setExpr(uint8_t i, unsigned long now, bool instant) {
    if (i >= BOT_NUM_EXPRESSIONS) i = EXPR_NEUTRAL;
    uint8_t prev = expr;
    expr = i;
    P = &botEyeParams[i];
    enteredAt = now;
    BotExpression e;
    memcpy_P(&e, &botExpressions[i], sizeof(BotExpression));
    browAngT[0] = e.browAngleL; browAngT[1] = e.browAngleR;
    browLenT = e.browLength; browThickT = e.browThickness; browVisT = e.browVisible;

    look = LOOK_NONE;
    // An expired forced eyeroll must not leak into the next face
    if (!(fid && fidForced && now - fidT0 < BOT_FIDGET_MS[fid])) fid = FID_NONE;
    nextFid = now + (unsigned long)eyeRnd(8000, 16000);
    nextLook = now + (unsigned long)eyeRnd(9000, 22000);
    gNext = now + 250; gUp = false; gMicro = false;
    smallOn = (P->entry == EYE_ENTRY_SMALL);
    smallUntil = now + 150;

    if (instant) {
      float T[EK_COUNT][2];
      targets(now, T);
      for (int s = 0; s < 2; s++) {
        for (int k = 0; k < EK_COUNT; k++) eye[s][k].snap(T[k][s]);
        browAng[s].snap(browAngT[s]); browLen[s].snap(browLenT); browThick[s].snap(browThickT);
      }
      tx = P->gazeX; ty = P->gazeY;
      gx.snap(tx); gy.snap(ty);
      gxOut = eyeClamp(tx, -BOT_EYE_GAZE_MAX_X, BOT_EYE_GAZE_MAX_X);
      gyOut = eyeClamp(ty, -BOT_EYE_GAZE_MAX_Y, BOT_EYE_GAZE_MAX_Y);
      popS.snap(0); popA.snap(0); bStr.snap(0);
      overlay = P->overlay; brows = browVisT; pending = false;
      if (P->entry == EYE_ENTRY_EYEROLL) startFidget(now, FID_EYEROLL, true);
      return;
    }

    // Mood pop: a small scale/aspect bump and a pupil kick, scaled by the face's pop
    float pop = P->pop;
    popS.v += 0.06f * pop / pkPopS;
    popA.v += -0.04f * pop / (pendant ? pkPopA1 : pkPopA);
    float pk = pendant ? pkPup1 : pkPup;
    for (int s = 0; s < 2; s++) {
      eye[s][EK_PW].v += 0.15f * pop * P->pw[s] / pk;
      eye[s][EK_PH].v += 0.15f * pop * P->ph[s] / pk;
    }
    // A stare ends with a blink (r4 M5)
    if (botEyeParams[prev].exitBlink && prev != i && bPh == 0) blinkStart(now, BW_EXIT);
    requestSwap(now, P->overlay, browVisT);
    if (P->entry == EYE_ENTRY_EYEROLL) startFidget(now, FID_EYEROLL, true);
  }

  // A face at rest: no blink, pop, entry pupil or fidget (touch UI mood chips)
  void restAt(uint8_t i) {
    setExpr(i, lastStepMs, true);
    smallOn = false;
    fid = FID_NONE;
    for (int s = 0; s < 2; s++) { eye[s][EK_PW].snap(P->pw[s]); eye[s][EK_PH].snap(P->ph[s]); }
  }

  // ---------------------------------------------------------------- per frame
  void step(unsigned long now) {
    if (!ready) return;
    unsigned long ms = now - lastStepMs;
    lastStepMs = now;
    if (ms > 0) frameMs = (uint16_t)min(ms, 1000UL);
    float dt = min(ms * 0.001f, 0.05f);
    bool lp = pendant;

    updateIdle(now);
    updateGaze(now);
    updateBlink(now);

    float T[EK_COUNT][2];
    targets(now, T);
    float fS = 0.75f / (P->transitionMs / 1000.0f), zS = lp ? 1 : 0.85f, zP = lp ? 1 : 0.6f;
    for (int s = 0; s < 2; s++) {
      for (int k = 0; k < EK_SHAPE_COUNT; k++) { eye[s][k].f = fS; eye[s][k].z = zS; eye[s][k].step(T[k][s], dt); }
      for (int k = EK_SHAPE_COUNT; k < EK_COUNT; k++) { eye[s][k].f = 3; eye[s][k].z = zP; eye[s][k].step(T[k][s], dt); }
      BotSpring *bw[3] = { &browAng[s], &browLen[s], &browThick[s] };
      for (int j = 0; j < 3; j++) { bw[j]->f = fS; bw[j]->z = zS; }
      browAng[s].step(browAngT[s], dt);
      browLen[s].step(browLenT, dt);
      browThick[s].step(browThickT, dt);
    }
    // Saccades 6 Hz critically damped; pursuit 2 Hz
    BotSpring *gs[2] = { &gx, &gy };
    for (int j = 0; j < 2; j++) {
      if (pursuit) { gs[j]->f = 2; gs[j]->z = lp ? 1 : 0.9f; }
      else { gs[j]->f = 6; gs[j]->z = 1; }
    }
    gx.step(eyeClamp(tx, -BOT_EYE_GAZE_MAX_X, BOT_EYE_GAZE_MAX_X), dt);
    gy.step(eyeClamp(ty, -BOT_EYE_GAZE_MAX_Y, BOT_EYE_GAZE_MAX_Y), dt);
    popS.step(0, dt);
    popA.z = lp ? 1 : 0.45f;
    popA.step(0, dt);
    bStr.step(0, dt);

    float ox = gx.x, oy = gy.x;
    if (P->gaze == GAZE_SHOCKED && !lp && idle && !padEngaged()) {
      ox += eyeRnd(-0.03f, 0.03f);
      oy += eyeRnd(-0.03f, 0.03f);
    }
    gxOut = eyeClamp(ox, -BOT_EYE_GAZE_MAX_X, BOT_EYE_GAZE_MAX_X);
    gyOut = eyeClamp(oy, -BOT_EYE_GAZE_MAX_Y, BOT_EYE_GAZE_MAX_Y);
    if (!lp) {
      if (max(eye[0][EK_STAR].x, eye[1][EK_STAR].x) > 0.5f) spinA = fmodf(spinA + BOT_EYE_STAR_SPIN * dt, TWO_PI);
      spiralA = fmodf(spiralA + TWO_PI * 0.5f * dt, TWO_PI);
    }
    for (int s = 0; s < 2; s++) {
      if (overlay == EYE_OVERLAY_GLITCH && eyeReached(now, bandNext[s]) && !(lp && bandSet[s])) {
        for (int j = 0; j < 3; j++) {
          band[s][j].y0 = eyeRnd(0, 0.8f);
          band[s][j].h = eyeRnd(0.1f, 0.28f);
          band[s][j].dx = (eyeRnd01() < 0.5f ? -1 : 1) * eyeRnd(0.1f, 0.2f);
        }
        bandSet[s] = true;
        bandNext[s] = now + (unsigned long)eyeRnd(120, 250);
      }
    }
  }

  // True when nothing on the eyes would visibly move: pendant mode skips the redraw
  bool atRest() const {
    if (!ready) return true;
    if (bPh != 0 || pending || bqn) return false;
    const float R = BOT_EYE_R;
    auto still = [R](const BotSpring &s) { return fabsf(s.t - s.x) * R <= 0.25f && fabsf(s.v) * R <= 0.5f; };
    if (!still(gx) || !still(gy) || !still(popS) || !still(popA) || !still(bStr)) return false;
    for (int s = 0; s < 2; s++) {
      for (int k = 0; k < EK_COUNT; k++) if (!still(eye[s][k])) return false;
      if (!still(browAng[s]) || !still(browLen[s]) || !still(browThick[s])) return false;
    }
    if ((overlay == EYE_OVERLAY_SPIRAL || P->pulse) && !pendant) return false;
    return true;
  }

  // ---------------------------------------------------------------- raster
  // The one closing path for blinks and shut: the lids meet 58 % down, then the
  // eye morphs into the shut arc, so the held frame is a lid line, never empty.
  void geom(int e, float k, float cx, float cy, unsigned long now, BotEyeGeom &g) const {
    const BotSpring *S = eye[e];
    g.R = BOT_EYE_R * k;
    g.side = e ? 1 : -1;
    g.inS = e ? -1 : 1;
    float shut = eyeClamp(S[EK_SHUT].x, 0, 1);
    g.cup = eyeClamp(S[EK_CUP].x, 0, 1);
    float bE = b * eyeClamp(1 - (shut - 0.35f) / 0.3f, 0, 1);
    float sc = max(0.5f, S[EK_SCALE].x) * (1 + popS.x);
    g.Rb = g.R * sc;
    g.Rw = g.Rb * (1 + 0.04f * bE);
    float a0 = S[EK_ASPECT].x;
    g.Hh = g.Rb * max(0.3f, a0 + popA.x + a0 * bStr.x);
    float dist = g.Rw + BOT_EYE_GAP * g.R / 2;
    float bob = (sleeping && !pendant) ? 0.03f * g.R * sinf(TWO_PI * 0.2f * eyeSecs(now)) : 0;
    g.ex = cx + g.side * dist;
    g.ey = cy + bob;
    float lt = eyeClamp(S[EK_LIDTOP].x, 0, 1), lb = eyeClamp(S[EK_LIDBOT].x, 0, 1);
    float close = max(bE, shut);
    float lidK = eyeSmooth(eyeBump(close, 0, 0.7f)), arcK = eyeSmooth(eyeBump(close, 0.4f, 1));
    g.ltRaw = lt;
    g.lt = lt < 0.58f ? lt + (0.58f - lt) * lidK : lt;
    g.lb = lb + (max(lb, 0.42f) - lb) * lidK;
    g.sl = S[EK_SLANT].x;
    g.sag = max(0.0f, S[EK_SAG].x);
    g.bow = eyeClamp(S[EK_BOW].x, 0, 1);
    g.shut = arcK;
    g.pup = 1 - max(lidK, arcK);
  }

  // One span per column: ellipse, cut by the lids, morphed toward the shut arc
  static void rasterEye(BotEyeCols &c, const BotEyeGeom &g) {
    float Hf = 2 * g.Hh, dir = 1 - 2 * g.cup, arcHalf = 0.5f * BOT_EYE_ARC_W * g.Rb;
    int x0 = max(0, (int)floorf(g.ex - g.Rw)), x1 = min(LCD_WIDTH - 1, (int)ceilf(g.ex + g.Rw));
    if (x1 - x0 + 1 > BOT_EYE_MAX_COLS) x1 = x0 + BOT_EYE_MAX_COLS - 1;
    c.x0 = x0;
    c.n = max(0, x1 - x0 + 1);
    for (int i = 0; i < c.n; i++) {
      c.cn[i] = 0; c.top[i] = 1; c.bot[i] = 0;
      float u = (x0 + i + 0.5f - g.ex) / g.Rw;
      if (u <= -1 || u >= 1) continue;
      float uu = u * u, w = sqrtf(1 - uu);   // the one sqrt per column
      float eT = g.ey - g.Hh * w, eB = g.ey + g.Hh * w, inner = g.inS * u;
      float tL = g.ey - g.Hh + g.lt * Hf + g.sl * g.Hh * inner + g.sag * Hf * (1 - uu);
      float bL = g.ey + g.Hh - g.lb * Hf * ((1 - g.bow) + g.bow * 1.1f * w);
      float top = max(eT, tL), bot = min(eB, bL);
      if (top > bot) { top = bot = eyeClamp((tL + bL) / 2, eT, eB); }
      if (g.shut > 0.001f) {
        float ua = u / 0.92f, aT, aB;
        if (ua > -1 && ua < 1) {
          float q = 1 - ua * ua, ht = arcHalf * (0.55f + 0.45f * q);
          float yc = g.ey + g.Hh * (0.05f + dir * (0.32f * q - 0.16f));
          aT = yc - ht; aB = yc + ht;
        } else {
          aT = aB = g.ey + g.Hh * (0.05f - dir * 0.16f);
        }
        top = eyeLerp(top, aT, g.shut);
        bot = eyeLerp(bot, aB, g.shut);
      }
      c.top[i] = (int16_t)max(0.0f, ceilf(top - 0.5f));
      c.bot[i] = (int16_t)min((float)(LCD_HEIGHT - 1), floorf(bot - 0.5f));
    }
  }

  // Pupil (heart/star morph polygon) or the X / spiral overlay, as cut-outs
  void pupilCuts(int e, const BotEyeGeom &g, unsigned long now) const {
    BotEyeCols &c = botEyeCols[e];
    const BotSpring *S = eye[e];
    float heart = eyeClamp(S[EK_HEART].x, 0, 1), star = eyeClamp(S[EK_STAR].x, 0, 1 - heart);
    float pulse = (P->pulse && !pendant) ? 1 + 0.06f * sinf(7 * eyeSecs(now)) : 1;
    // Pupils shrink as the lids close
    float rx = max(0.0f, S[EK_PW].x) * g.Rb * g.pup * pulse, ry = max(0.0f, S[EK_PH].x) * g.Rb * g.pup * pulse;
    bool glyph = (overlay == EYE_OVERLAY_X || overlay == EYE_OVERLAY_SPIRAL);
    if (glyph) rx = ry = 0.42f * g.Rb;
    float qx = g.inS * S[EK_INSET].x * g.Rb + gxOut * g.Rb, qy = S[EK_PDY].x * g.Rb + gyOut * g.Rb;
    float ext = 1 + 0.15f * star;
    float ax = max(1.0f, g.Rb - rx * ext - 0.06f * g.R), ay = max(1.0f, g.Hh - ry * ext - 0.06f * g.R);
    float kk = hypotf(qx / ax, qy / ay);
    if (kk > 1) { qx /= kk; qy /= kk; }
    float pcx = g.ex + qx, pcy = g.ey + qy;
    if (glyph && g.pup < 0.3f) return;   // glyphs close with the lids, like pupils (r4 O2)
    if (overlay == EYE_OVERLAY_X) {
      float a = 0.42f * g.Rb * g.pup / M_SQRT2, r = 0.07f * g.Rb;
      botCapsuleCut(c, pcx - a, pcy - a, pcx + a, pcy + a, r);
      botCapsuleCut(c, pcx - a, pcy + a, pcx + a, pcy - a, r);
    } else if (overlay == EYE_OVERLAY_SPIRAL) {
      float sx = g.ex + gxOut * g.Rb * 0.5f, sy = g.ey + gyOut * g.Rb * 0.5f;
      float rmax = 0.7f * min(g.Rb, g.Hh) * g.pup, tmax = 1.75f * TWO_PI, ph = g.side * spiralA, r = 0.05f * g.Rb;
      float px = sx, py = sy;
      for (int i = 1; i * 0.12f <= tmax + 1e-6f; i++) {
        float th = i * 0.12f, rr = rmax * th / tmax, a = g.side * th + ph;
        float nx = sx + rr * cosf(a), ny = sy + rr * sinf(a);
        botCapsuleCut(c, px, py, nx, ny, r);
        px = nx; py = ny;
      }
    } else if (rx >= 0.5f && ry >= 0.5f) {
      float e0 = max(0.0f, 1 - heart - star), rc = cosf(spinA), rs = sinf(spinA);
      float px[BOT_EYE_POLY_N], py[BOT_EYE_POLY_N];
      for (int i = 0; i < BOT_EYE_POLY_N; i++) {
        float x = botPolyCos[i] * e0, y = botPolySin[i] * e0;
        if (heart > 0) { x += heart * botPolyHX[i]; y += heart * botPolyHY[i]; }
        if (star > 0) {
          float sx = botPolySX[i], sy = botPolySY[i];
          x += star * (sx * rc - sy * rs);
          y += star * (sx * rs + sy * rc);
        }
        px[i] = pcx + x * rx;
        py[i] = pcy + y * ry;
      }
      botPolyCut(c, px, py, BOT_EYE_POLY_N);
    }
  }

  // Glitch shifts row bands of the eye (and the pupil riding them), clipped so
  // the eyes never cross the centre gap. Otherwise one band, no shift.
  void layoutRows(int e, const BotEyeGeom &g, float cx) const {
    BotEyeOut &o = botEyeOut[e];
    const BotEyeCols &c = botEyeCols[e];
    o.x0 = c.x0;
    o.x1 = c.x0 + c.n - 1;
    o.nRows = 1;
    o.rows[0].r0 = 0; o.rows[0].r1 = LCD_HEIGHT - 1; o.rows[0].dx = 0;
    if (overlay != EYE_OVERLAY_GLITCH) return;
    int16_t ya[3], yb[3], dx[3], cut[8];
    uint8_t nc = 0, nb = bandSet[e] ? 3 : 0;
    cut[nc++] = 0;
    cut[nc++] = LCD_HEIGHT;
    for (int j = 0; j < nb; j++) {
      ya[j] = (int16_t)max(0L, lroundf(g.ey - g.Hh + band[e][j].y0 * 2 * g.Hh));
      yb[j] = (int16_t)min((long)(LCD_HEIGHT - 1), ya[j] + lroundf(band[e][j].h * 2 * g.Hh));
      dx[j] = (int16_t)lroundf(band[e][j].dx * g.R);
      cut[nc++] = ya[j];
      cut[nc++] = yb[j] + 1;
    }
    for (int i = 1; i < nc; i++) {   // sort the row breakpoints
      int16_t v = cut[i];
      int k = i;
      while (k > 0 && cut[k - 1] > v) { cut[k] = cut[k - 1]; k--; }
      cut[k] = v;
    }
    o.nRows = 0;
    int16_t lo = 0, hi = 0;
    for (int i = 0; i + 1 < nc; i++) {
      if (cut[i + 1] <= cut[i] || cut[i] >= LCD_HEIGHT) continue;
      BotEyeRows &r = o.rows[o.nRows++];
      r.r0 = cut[i]; r.r1 = cut[i + 1] - 1; r.dx = 0;
      for (int j = 0; j < nb; j++) if (ya[j] <= r.r0 && r.r1 <= yb[j]) r.dx += dx[j];   // overlapping bands add up
      lo = min(lo, r.dx); hi = max(hi, r.dx);
    }
    int lim = max(1L, lroundf(BOT_EYE_GAP * g.R / 2));
    o.x0 = c.x0 + lo;
    o.x1 = c.x0 + c.n - 1 + hi;
    if (e == 0) o.x1 = min((int)o.x1, (int)floorf(cx - lim - 0.5f));
    else o.x0 = max((int)o.x0, (int)ceilf(cx + lim - 0.5f));
    o.x0 = max((int)o.x0, 0);
    o.x1 = min((int)o.x1, LCD_WIDTH - 1);
  }

  // Draw one eye (fill + cut-outs), or with strokeOnly just record its extents
  static void drawEye(int e, bool strokeOnly, uint16_t fc) {
    const BotEyeOut &o = botEyeOut[e];
    const BotEyeCols &c = botEyeCols[e];
    for (int x = o.x0; x <= o.x1; x++) {
      for (uint8_t r = 0; r < o.nRows; r++) {
        int src = x - o.rows[r].dx - c.x0;
        if (src < 0 || src >= c.n) continue;
        int16_t a = max(o.rows[r].r0, c.top[src]), b = min(o.rows[r].r1, c.bot[src]);
        if (a > b) continue;
        if (strokeOnly) {
          botStrokeTop[x] = min(botStrokeTop[x], a);
          botStrokeBot[x] = max(botStrokeBot[x], b);
          continue;
        }
        botVSpan(x, a, b, fc);
        for (uint8_t q = 0; q < c.cn[src]; q++) {
          int16_t qa = max(c.ca[src][q], a), qb = min(c.cb[src][q], b);
          if (qa <= qb) botVSpan(x, qa, qb, BOT_COLOR_PUPIL);
        }
      }
    }
  }

  // Black stroke around the union of both eyes (ambient background only)
  static void strokeSpans(float k) {
    int rad = max(1L, lroundf(5 * k));
    if (rad > 8) rad = 8;
    int16_t ext[17];
    for (int d = -rad; d <= rad; d++) ext[d + rad] = (int16_t)floorf(sqrtf((float)(rad * rad - d * d)));
    for (int x = 0; x < LCD_WIDTH; x++) { botStrokeTop[x] = INT16_MAX; botStrokeBot[x] = -1; }
    drawEye(0, true, 0);
    drawEye(1, true, 0);
    for (int x = 0; x < LCD_WIDTH; x++) { botStrokeA[x] = INT16_MAX; botStrokeB[x] = -1; }
    for (int x = 0; x < LCD_WIDTH; x++) {
      if (botStrokeBot[x] < 0) continue;
      for (int d = -rad; d <= rad; d++) {
        int xx = x + d;
        if (xx < 0 || xx >= LCD_WIDTH) continue;
        botStrokeA[xx] = min(botStrokeA[xx], (int16_t)(botStrokeTop[x] - ext[d + rad]));
        botStrokeB[xx] = max(botStrokeB[xx], (int16_t)(botStrokeBot[x] + ext[d + rad]));
      }
    }
    for (int x = 0; x < LCD_WIDTH; x++) {
      if (botStrokeB[x] >= 0) botVSpan(x, botStrokeA[x], botStrokeB[x], BOT_COLOR_BG);
    }
  }

  // Brow as one span per column along its centre line
  static void browSpans(float x0, float y0, float x1, float y1, float half, uint16_t c) {
    float w = x1 - x0;
    if (fabsf(w) < 1e-6f) w = 1;
    for (int x = (int)ceilf(x0 - 0.5f); x <= (int)floorf(x1 - 0.5f); x++) {
      float t = eyeClamp((x + 0.5f - x0) / w, 0, 1), yc = eyeLerp(y0, y1, t);
      botVSpan(x, (int16_t)ceilf(yc - half - 0.5f), (int16_t)floorf(yc + half - 0.5f), c);
    }
  }

  // Draw both eyes and the brows centred on (cx, cy) at scale k (1 = R 48)
  void render(int16_t cx, int16_t cy, float k, uint16_t fc, bool stroke, unsigned long now,
              bool drawBrows = true) const {
    if (gfx == nullptr || !ready) return;
    BotEyeGeom G[2];
    for (int e = 0; e < 2; e++) {
      geom(e, k, cx, cy, now, G[e]);
      rasterEye(botEyeCols[e], G[e]);
      pupilCuts(e, G[e], now);
      layoutRows(e, G[e], cx);
    }
    if (stroke) strokeSpans(k);
    drawEye(0, false, fc);
    drawEye(1, false, fc);

    // Brows follow the un-blinked top lid, dip 0.04 R at full close, never vanish
    if (!brows || !drawBrows) return;
    for (int pass = stroke ? 0 : 1; pass < 2; pass++) {
      for (int e = 0; e < 2; e++) {
        const BotEyeGeom &g = G[e];
        float ang = (browAng[e].x + 20 * eye[e][EK_SLANT].x) * DEG_TO_RAD;
        float len = browLen[e].x * k, th = max(1.0f, browThick[e].x * k);
        float by = (g.ey - g.Hh) + g.ltRaw * g.Hh - 0.24f * g.R + 0.04f * g.R * b;
        float sa = sinf(ang) * len, y0, y1;
        if (e == 0) { y0 = by - sa; y1 = by + sa; }
        else { y0 = by + sa; y1 = by - sa; }
        float half = th / 2 / cosf(ang), sk = pass ? 0 : 5 * k;
        if (min(y0, y1) - half < 0) continue;   // a brow that would leave the screen is not drawn
        browSpans(g.ex - len - sk, y0, g.ex + len + sk, y1, half + sk, pass ? fc : BOT_COLOR_BG);
      }
    }
  }
};

BotEyeModel botEyes;

// Every expression change (BotFaceState::transitionTo) lands here
void botEyesSetExpression(uint8_t index) {
  if (!botEyes.ready || index == botEyes.expr) return;
  botEyes.setExpr(index, millis(), false);
}

#endif // BOT_EYE_MODEL_H
