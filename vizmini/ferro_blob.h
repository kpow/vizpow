#ifndef FERRO_BLOB_H
#define FERRO_BLOB_H

#include <Arduino.h>
#include "config.h"
#include "gfx_mono.h"
#include "bot_eye_model.h"   // eyeRnd(), eyeRnd01()

// ============================================================================
// Ferro mode — vizMini as a black ferrofluid drop on a white screen
// ============================================================================
// Motion: docs/vizmini-ferro-motion-spec.md. Look:
// docs/vizmini-ferro-art-direction.md (its §11 wins where the two differ).
// Kevin's calls and the build phases: PLAN-v2.md stage 6.
//
// Round 2 (Kevin, 2026-11-03: "not locked to the bottom, typical ferrofluid
// patterns, symmetrical spikes from the magnetic fields, tentacles like the
// 1.69 and Stackchan, more dynamic"):
// - He floats and roams the whole screen: glides, darts that overshoot, and
//   bounces, inside a margin that grows with his spikes and tentacles.
// - Spikes live on a 24-slot ring around a magnet axis (15 deg apart). Tall
//   spikes use only even slots (30 deg apart, art direction §1.4); odd slots
//   carry short ones. Patterns (crowns, star, field, dipole, porcupine, split,
//   wave, breathe, bloom, carousel) set slot targets symmetric about the axis,
//   two can stack, and the axis itself turns on a spring.
// - Tentacles are 8-node spring chains chasing a pose curve, with a plump root
//   that rises out of a mound and a round bulb tip (party_blob.h's model):
//   reach, mirrored feeler pairs, flicks and wall-to-wall arms.
// - Body and tentacles fill as one union (non-zero winding, orientation
//   normalised so overlaps never punch holes).
//
// The C3 has no FPU (a soft-float op is ~0.4 us), so the outline, spikes,
// mounds and the fill are integer (Q16.16, a 256-entry Q15 sin table). The
// tentacles' few hundred spring updates and the scheduler are float, with
// table trig. No heap.
// ============================================================================

// ---- Fixed point -----------------------------------------------------------
typedef int32_t fx;                                  // Q16.16
#define FX_ONE        65536
#define FX(v)         ((fx)((v) * 65536.0f))
static inline fx fxMul(fx a, fx b) { return (fx)(((int64_t)a * b) >> 16); }
static inline float fxF(fx a) { return a * (1.0f / 65536.0f); }

// 256-entry Q15 sine: index 0..255 is one turn (1.40625 deg per step)
static int16_t ferroSin[256];
static inline int32_t fsin(uint8_t i) { return ferroSin[i]; }
static inline int32_t fcos(uint8_t i) { return ferroSin[(uint8_t)(i + 64)]; }
static inline fx fxMulQ15(fx a, int32_t q15) { return (fx)(((int64_t)a * q15) >> 15); }
// Table trig for float callers (radians in, ~1.4 deg steps): no soft-float sinf
static inline uint8_t radToBin(float a) { return (uint8_t)(int32_t)lroundf(a * (256.0f / TWO_PI)); }
static inline float tsin(float a) { return ferroSin[radToBin(a)] * (1.0f / 32767.0f); }
static inline float tcos(float a) { return ferroSin[(uint8_t)(radToBin(a) + 64)] * (1.0f / 32767.0f); }

// Damped spring in Q16.16, stepped at a fixed h = 1/128 s (party's 8 ms)
struct FxSpring {
  fx x = 0, v = 0, t = 0;
  fx A = 0, B = 0;        // w^2 h, 2 zeta w h  (Q16)
  void set(float f, float z) {
    float w = TWO_PI * f, h = 1.0f / 128.0f;
    A = FX(w * w * h);
    B = FX(2.0f * z * w * h);
  }
  void init(float x0, float f, float z) { x = t = FX(x0); v = 0; set(f, z); }
  void sub() { v += fxMul(A, t - x) - fxMul(B, v); x += v >> 7; }
  void kick(float dv) { v += FX(dv); }
};

#define FERRO_N         128         // outline vertices, 2 table steps (2.8 deg) apart
#define FERRO_SLOTS     24          // spike ring, 15 deg apart (even slots may go tall)
#define FERRO_TENTS     5
#define FERRO_NN        8           // nodes per tentacle; 0 and 1 ride the rim
#define FERRO_TP        (2 * (FERRO_NN - 1) + 9)   // tentacle polygon: two sides + 9-point bulb
#define FERRO_MAX_EDGES (FERRO_N + FERRO_TENTS * FERRO_TP)
#define FERRO_MAX_XR    32          // crossings per column

// Spike shape over +-4 vertices (+-11 deg): a 2 px flat tip, concave flanks
static const int32_t FERRO_SPK[5] = { 32767, 28180, 16384, 7209, 1638 };   // Q15: 1, .86, .50, .22, .05

enum FerroPattern : uint8_t {
  FP_NONE = 0, FP_CROWN3, FP_CROWN5, FP_STAR6, FP_FIELD12, FP_DIPOLE, FP_PORCUPINE,
  FP_SPLIT, FP_WAVE, FP_BREATHE, FP_BLOOM, FP_CAROUSEL, FP_NUM
};
static const char *const ferroPatternNames[FP_NUM] = {
  "none", "crown3", "crown5", "star6", "field12", "dipole", "porcupine",
  "split", "wave", "breathe", "bloom", "carousel"
};

enum FerroTentKind : uint8_t { FT_REACH, FT_FEELER, FT_FLICK, FT_ARM };
enum FerroTentPhase : uint8_t { FT_GROW, FT_HOLD, FT_RETRACT };

struct FSpring {                    // float spring for the few per-frame scalars
  float x, v, t, f, z;
  void init(float x0, float f0, float z0) { x = t = x0; v = 0; f = f0; z = z0; }
  void step(float dt) {
    float w = TWO_PI * f;
    int n = (int)(dt / 0.008f) + 1;
    float h = dt / n;
    for (int i = 0; i < n; i++) { v += (w * w * (t - x) - 2 * z * w * v) * h; x += v * h; }
  }
};

struct FerroTent {
  bool on = false;
  uint8_t kind = FT_REACH, phase = FT_GROW;
  float th;                         // root angle on the body (rad, screen: +y down)
  float aim, L, curl, swA, swF, swPh;
  FSpring lam;                      // grown share 0..1
  float nx[FERRO_NN], ny[FERRO_NN], vx[FERRO_NN], vy[FERRO_NN];
  float hw[FERRO_NN], ux[FERRO_NN], uy[FERRO_NN];
  float bulbR;
  unsigned long holdUntil;
};

struct FerroLayer {                 // one running pattern
  uint8_t pat = FP_NONE;
  float H;                          // tall spike height, px
  unsigned long t0, until;
};

class FerroBlob {
public:
  bool active = false;
  bool glintOn = true;              // art direction bench A/B (§2.1)
  uint32_t sumUs = 0, cnt = 0, maxUs = 0;   // render cost, read and reset by /state
  uint8_t forcedPattern = FP_NONE;  // web: run this next

  void begin() {
    for (int i = 0; i < 256; i++) ferroSin[i] = (int16_t)lroundf(sinf(i * TWO_PI / 256.0f) * 32767.0f);
    r = rNow = FERRO_R_M;
    px.init(64, 0.7f, 0.75f);
    py.init(32, 0.7f, 0.75f);
    axis.init(90, 0.6f, 0.7f);
    for (int k = 0; k < 6; k++) {
      static const float f[3] = { 2.0f, 3.0f, 3.8f }, z[3] = { 0.22f, 0.25f, 0.28f };
      mode[k].init(0, f[k / 2], z[k / 2]);
    }
    for (auto &s : slot) s.init(0, 2.6f, 0.32f);
  }

  // Enter ("the ink takes the eyes", motion spec §4.1): the face's black
  // background is the ferrofluid. It starts as a 76 px drop covering the
  // whole screen with the eyes inside (the same picture as the face), then
  // contracts like an iris opening, corners first, and swallows the eyes.
  void enter(unsigned long now) {
    active = true;
    morph = FM_ENTER;
    P.init(0, 1.4f, 0.82f); P.t = 1;
    px.x = px.t = 64; py.x = py.t = 32; px.v = py.v = 0;
    for (auto &m : mode) m.x = m.v = m.t = 0;
    for (auto &s : slot) s.x = s.v = s.t = 0;
    for (auto &t : tents) t.on = false;
    for (auto &l : layers) l.pat = FP_NONE;
    mode[2].kick(0.8f); mode[5].kick(-0.6f);   // the edge is liquid from frame one
    lastStep = now; accUs = 0;
    landed = false; exitDone = false;
    schedFrom = now + 60000;          // the idle scheduler waits for the landing
    nextTwitch = now + 300;
  }

  // Exit (§4.2): gather in, then swell back over the screen; the eyes reopen
  // as it fills and the white corners go last
  void requestExit(unsigned long now) {
    if (!active || morph == FM_EXIT) return;
    morph = FM_EXIT;
    gatherUntil = now + 250;
    for (auto &l : layers) l.pat = FP_NONE;
    for (auto &t : tents) if (t.on && t.phase != FT_RETRACT) { t.phase = FT_RETRACT; t.lam.t = 0; t.lam.f = 4.0f; t.lam.z = 0.8f; }
    mode[0].kick(1.0f);               // crouch
    px.f = py.f = 1.2f; px.z = py.z = 0.8f;
    px.t = 64; py.t = 32;
    P.f = 1.4f; P.z = 0.9f;
  }

  void leave() { active = false; morph = FM_NONE; }

  bool morphing() const { return morph != FM_NONE; }
  bool done() const { return exitDone; }
  float brightP() const { return morph == FM_NONE ? 1.0f : constrain(P.x, 0.0f, 1.0f); }

  // Where the eyes ride during a morph (vizmini.ino draws them over the drop)
  bool eyeDraw = false, eyesShut = false;
  float eyeK = 0, eyeCX = 64, eyeCY = 28;

  const char *patternName() const { return ferroPatternNames[layers[0].pat]; }
  int liveTents() const { int n = 0; for (auto &t : tents) n += t.on; return n; }

  // Web: start a pattern now (replaces the lead layer), or a tentacle move
  void startPattern(uint8_t p, unsigned long now) {
    if (p == FP_NONE || p >= FP_NUM) return;
    runPattern(0, p, now, big(), (unsigned long)random(5000, 9001));
  }
  void tentMove(uint8_t kind, unsigned long now) { spawnTentMove(kind, now); }

  // ---- step -----------------------------------------------------------------
  void step(unsigned long now) {
    uint32_t dtUs = (now - lastStep) * 1000UL;
    lastStep = now;
    if (dtUs > 100000) dtUs = 100000;
    float dt = dtUs * 1e-6f;
    tSec = (now % 3600000UL) * 0.001f;

    stepMorph(now, dt);
    if (morph == FM_NONE && (long)(now - schedFrom) >= 0) schedule(now);
    stepPatterns(now);

    // Keep the whole silhouette on the glass: the margin grows with the spikes and tentacles
    float reach = (morph ? rNow : r) * 1.15f + maxSlot() + 2;
    if (morph) reach = 0;             // the morph owns his position
    float mx = morph ? 0 : constrain(reach, 18.0f, 60.0f), my = morph ? 0 : constrain(reach, 18.0f, 32.0f);
    px.t = constrain(px.t, mx, OLED_WIDTH - mx);
    py.t = constrain(py.t, my, OLED_HEIGHT - my);
    px.step(dt); py.step(dt); axis.step(dt);
    if (pattern(FP_CAROUSEL)) axis.t += 70.0f * dt;     // the field turns

    // Spikes can grow faster than the position spring moves him: ease him
    // back inside the box so nothing crops at the glass
    if (px.x < mx) px.x += (mx - px.x) * 0.25f;
    if (px.x > OLED_WIDTH - mx) px.x -= (px.x - (OLED_WIDTH - mx)) * 0.25f;
    if (py.x < my) py.x += (my - py.x) * 0.25f;
    if (py.x > OLED_HEIGHT - my) py.x -= (py.x - (OLED_HEIGHT - my)) * 0.25f;

    // Bounce off the walls of the allowed box (squash on the way)
    if (px.x < mx && px.v < 0) { px.v = -px.v * 0.6f; mode[0].kick(-0.8f); }
    if (px.x > OLED_WIDTH - mx && px.v > 0) { px.v = -px.v * 0.6f; mode[0].kick(-0.8f); }
    if (py.x < my && py.v < 0) { py.v = -py.v * 0.6f; mode[0].kick(0.8f); }
    if (py.x > OLED_HEIGHT - my && py.v > 0) { py.v = -py.v * 0.6f; mode[0].kick(0.8f); }

    accUs += dtUs;
    while (accUs >= 7812) {           // 1/128 s sub-steps for the integer springs
      accUs -= 7812;
      for (auto &m : mode) m.sub();
      for (auto &s : slot) s.sub();
    }
    static const float clampF[3] = { 0.35f, 0.16f, 0.10f };
    for (int k = 0; k < 6; k++) {
      fx c = FX(clampF[k / 2]);
      if (mode[k].x > c) { mode[k].x = c; if (mode[k].v > 0) mode[k].v = 0; }
      if (mode[k].x < -c) { mode[k].x = -c; if (mode[k].v < 0) mode[k].v = 0; }
    }
    for (auto &s : slot) if (s.x < 0) { s.x = 0; if (s.v < 0) s.v = 0; }

    for (int i = 0; i < FERRO_TENTS; i++) if (tents[i].on) stepTent(tents[i], now, dt);
  }

  // ---- render ---------------------------------------------------------------
  void render(unsigned long now) {
    uint32_t t0 = micros();
    uint8_t *buf = u8g2.getBufferPtr();
    memset(buf, 0xFF, OLED_WIDTH * OLED_HEIGHT / 8);   // white field

    buildOutline();
    ne = 0;
    addPoly(vx, vy, FERRO_N);
    static fx tx[FERRO_TP], ty[FERRO_TP];
    for (auto &t : tents) {
      if (!t.on) continue;
      prepTent(t);
      int n = buildTent(t, tx, ty);
      addPoly(tx, ty, n);
    }
    fillEdges(buf);
    if (glintOn && morph == FM_NONE && (long)(now - glintFrom) >= 0) drawGlint(buf);
    cornerMask(buf);

    uint32_t us = micros() - t0;
    sumUs += us; cnt++;
    if (us > maxUs) maxUs = us;
  }

private:
  float r, rNow;                    // body radius, and this frame's (the morph swells it)
  enum { FM_NONE = 0, FM_ENTER, FM_EXIT };
  uint8_t morph = FM_NONE;
  FSpring P;                        // morph progress: 0 = face, 1 = drop
  bool landed = false, exitDone = false;
  unsigned long gatherUntil = 0, schedFrom = 0, helloAt = 0, glintFrom = 0;

  static float smooth01(float x) { x = constrain(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }

  void stepMorph(unsigned long now, float dt) {
    if (morph == FM_ENTER) {
      P.step(dt);
      float p = constrain(P.x, 0.0f, 1.0f);
      rNow = 76 + (r - 76) * smooth01(1.6f * p);
      eyeDraw = p < 0.6f; eyesShut = p >= 0.45f;
      eyeK = 0.41f + (0.18f - 0.41f) * p;
      eyeCX = 64; eyeCY = 28 + 2 * p;
      if (P.x > 0.97f) {                                                   // landing
        morph = FM_NONE; landed = true; rNow = r; eyeDraw = false;
        mode[0].kick(2.2f); mode[4].kick(0.4f);
        runPattern(0, FP_STAR6, now, 6.0f, 800);                           // splash crown
        helloAt = now + 400;
        schedFrom = now + 1500;
        nextRoam = now + 1800; nextPattern = now + 2200; nextTent = now + 2600;
        glintFrom = now + 200;
      }
    } else if (morph == FM_EXIT) {
      if ((long)(now - gatherUntil) >= 0) P.t = 0;
      P.step(dt);
      float p = constrain(P.x, 0.0f, 1.0f);
      rNow = 76 + (r - 76) * smooth01(1.6f * p);
      eyeDraw = p < 0.5f; eyesShut = p > 0.35f;
      eyeK = 0.41f + (0.18f - 0.41f) * p;
      eyeCX = 64; eyeCY = 28 + 2 * p;
      for (auto &m : mode) m.x = m.x * 7 / 8;                             // stiffen: a clean edge by the end
      if ((long)(now - gatherUntil) >= 0 && P.x < 0.03f) { exitDone = true; eyeDraw = true; eyesShut = false; eyeK = 0.41f; eyeCY = 28; }
    } else {
      rNow = r;
      eyeDraw = false;
      if (helloAt && (long)(now - helloAt) >= 0) {                         // hello: one curling tentacle
        helloAt = 0;
        float a = -HALF_PI + eyeRnd(-0.9f, 0.9f);
        spawnTent(FT_FEELER, a, a, r * 1.2f, 1.0f, 1.4f, 0, 0.6f, 1800, 2.2f);
      }
    }
  }
  FSpring px, py, axis;             // centre (px), magnet axis (deg, 90 = up)
  FxSpring mode[6];                 // surface modes n = 2, 3, 4: cos, sin pairs (fractions of r)
  FxSpring slot[FERRO_SLOTS];       // spike heights, px
  FerroTent tents[FERRO_TENTS];
  FerroLayer layers[2];
  float stretchK = 0;               // dipole stretch along the axis (mode 2)
  unsigned long lastStep = 0, nextRoam = 0, nextPattern = 0, nextTent = 0, nextTwitch = 0;
  uint32_t accUs = 0;
  float tSec = 0;

  fx vx[FERRO_N], vy[FERRO_N];      // outline, Q16 px
  fx rad[FERRO_N];                  // radius per vertex, spikes included (glint reads it)
  float baseRad[FERRO_N];           // radius without spikes or mounds (tentacle roots)
  fx ocx, ocy;

  // ---- scheduler --------------------------------------------------------------
  float big() { return eyeRnd01() < 0.35f ? FERRO_SPIKE_B : FERRO_SPIKE_M; }

  void schedule(unsigned long now) {
    // Roam: glide, dart (overshoots) or a quick hop, a new spot every 1.5-4 s
    if ((long)(now - nextRoam) >= 0) {
      int style = random(10);
      float f = style < 5 ? 0.7f : style < 8 ? 1.7f : 2.4f, z = style < 5 ? 0.75f : style < 8 ? 0.42f : 0.35f;
      px.f = py.f = f; px.z = py.z = z;
      px.t = random(20, OLED_WIDTH - 19);
      py.t = random(18, OLED_HEIGHT - 17);
      if (style >= 5) mode[0].kick(style >= 8 ? 1.4f : 0.9f);
      nextRoam = now + random(1500, 4001);
    }
    // Twitch: a small mode kick every 1-3 s keeps the surface alive
    if ((long)(now - nextTwitch) >= 0) {
      mode[random(6)].kick((random(2) ? 1 : -1) * 0.7f);
      nextTwitch = now + random(1000, 3001);
    }
    // Patterns: lead layer most of the time, a stacked second layer half the time
    if ((long)(now - nextPattern) >= 0) {
      uint8_t p = forcedPattern != FP_NONE ? forcedPattern : pickPattern();
      forcedPattern = FP_NONE;
      runPattern(0, p, now, big(), (unsigned long)random(4000, 8001));
      if (eyeRnd01() < 0.5f) {
        uint8_t q = pickPattern();
        if (q != p) runPattern(1, q, now + 600, FERRO_SPIKE_M * 0.8f, (unsigned long)random(3000, 6001));
      }
      if (eyeRnd01() < 0.55f) setAxis(90);                     // magnet above him, mostly
      else setAxis(axis.x + random(-150, 151));                // or the field swings round
      nextPattern = now + random(3500, 7001);
    }
    // Tentacles: a move every 1.5-4 s
    if ((long)(now - nextTent) >= 0) {
      int k = random(10);
      spawnTentMove(k < 4 ? FT_REACH : k < 7 ? FT_FEELER : k < 9 ? FT_FLICK : FT_ARM, now);
      nextTent = now + random(1500, 4001);
    }
  }

  uint8_t pickPattern() {
    static const uint8_t w[FP_NUM] = { 0, 10, 12, 10, 8, 10, 6, 8, 9, 8, 9, 10 };
    int total = 0;
    for (auto v : w) total += v;
    int rr = random(total);
    for (uint8_t i = 0; i < FP_NUM; i++) { if (rr < w[i]) return i; rr -= w[i]; }
    return FP_CROWN5;
  }

  // Aim the magnet axis the short way round (the carousel winds it up)
  void setAxis(float deg) {
    axis.x = fmodf(axis.x, 360.0f);
    axis.t = deg + 360.0f * lroundf((axis.x - deg) / 360.0f);
  }

  bool pattern(uint8_t p) const { return layers[0].pat == p || layers[1].pat == p; }

  void runPattern(int li, uint8_t p, unsigned long now, float H, unsigned long ms) {
    layers[li].pat = p; layers[li].H = H; layers[li].t0 = now; layers[li].until = now + ms;
    mode[0].kick(p == FP_DIPOLE ? 1.5f : -0.8f);                 // the field grabs him
    if (p == FP_CAROUSEL) for (auto &s : slot) s.set(2.6f, 0.32f);
  }

  // Slot targets from the running layers (max of the two), relative to the axis
  void stepPatterns(unsigned long now) {
    float tgt[FERRO_SLOTS] = { 0 };
    stretchK = 0;
    for (auto &l : layers) {
      if (l.pat == FP_NONE) continue;
      if ((long)(now - l.until) >= 0) { l.pat = FP_NONE; continue; }
      if ((long)(now - l.t0) < 0) continue;
      float T = (now - l.t0) * 0.001f, H = l.H;
      float in = fminf(1.0f, T / 0.35f), out = fminf(1.0f, (l.until - now) * 0.001f / 0.4f);
      float g = in * out;
      float h[FERRO_SLOTS] = { 0 };
      auto even = [&](int j, float v) { h[(j % 24 + 24) % 24] = fmaxf(h[(j % 24 + 24) % 24], v); };
      switch (l.pat) {
        case FP_CROWN3:   even(0, H); even(2, 0.8f * H); even(-2, 0.8f * H); break;
        case FP_CROWN5:   even(0, H); even(2, 0.9f * H); even(-2, 0.9f * H); even(4, 0.6f * H); even(-4, 0.6f * H); break;
        case FP_STAR6:    for (int j = 0; j < 24; j += 4) even(j, 0.85f * H); break;
        case FP_FIELD12:  for (int j = 0; j < 24; j += 2) even(j, 0.6f * H * (0.8f + 0.2f * tsin(TWO_PI * 0.7f * T + j))); break;
        case FP_DIPOLE:   even(0, H); even(12, H); even(2, 0.45f * H); even(-2, 0.45f * H);
                          even(10, 0.45f * H); even(14, 0.45f * H); stretchK = fmaxf(stretchK, 0.22f * g); break;
        case FP_PORCUPINE:for (int j = 0; j < 24; j += 2) even(j, 6.5f); break;   // 24 short ones read as fuzz
        case FP_SPLIT:    even(0, H); even(2, 0.7f * H); even(-2, 0.7f * H);
                          even(12, H); even(10, 0.7f * H); even(14, 0.7f * H); break;
        case FP_WAVE:     for (int j = 0; j < 24; j += 2) {
                            float c = tcos(j * (TWO_PI / 24) - TWO_PI * 0.45f * T);
                            even(j, c > 0 ? H * c * c * c : 0);
                          } break;
        case FP_BREATHE: {
          float b = 0.5f + 0.5f * tsin(TWO_PI * 0.55f * T - HALF_PI);
          even(0, H * b); even(2, 0.85f * H * b); even(-2, 0.85f * H * b); even(4, 0.55f * H * b); even(-4, 0.55f * H * b);
        } break;
        case FP_BLOOM: {                       // pairs open out from the axis, then close
          float k = fminf(T / 0.45f, 6.0f);
          for (int p = 0; p <= 5; p++) if (k > p) { float v = H * (1.0f - 0.1f * p); even(2 * p, v); even(-2 * p, v); }
        } break;
        case FP_CAROUSEL: for (int j = 0; j < 24; j += 6) even(j, H); for (int j = 3; j < 24; j += 6) even(j, 0.45f * H); break;
      }
      for (int j = 0; j < FERRO_SLOTS; j++) tgt[j] = fmaxf(tgt[j], h[j] * g);
    }
    for (int j = 0; j < FERRO_SLOTS; j++) slot[j].t = FX(tgt[j]);
  }

  float maxSlot() const {
    fx m = 0;
    for (auto &s : slot) if (s.x > m) m = s.x;
    return fxF(m);
  }

  // ---- body -------------------------------------------------------------------
  void buildOutline() {
    float breath = 0.06f * tsin(TWO_PI * 0.25f * tSec);
    float a3 = 0.10f * (0.7f + 0.3f * tsin(TWO_PI * 0.050f * tSec));
    float a4 = 0.08f * (0.7f + 0.3f * tsin(TWO_PI * 0.071f * tSec));
    uint8_t ph3 = (uint8_t)(int32_t)(tSec * 0.13f * 256.0f);
    uint8_t ph4 = (uint8_t)(int32_t)(-tSec * 0.19f * 256.0f);
    // Velocity stretch along travel, plus the dipole stretch along the axis
    float vxs = px.v, vys = py.v, sp = sqrtf(vxs * vxs + vys * vys);
    float st = 0.22f * fminf(sp / (2.5f * r), 1.0f);
    uint8_t aV = sp > 1 ? radToBin(atan2f(-vys, vxs)) : 0;
    uint8_t aX = (uint8_t)(int32_t)lroundf(axis.x * (256.0f / 360.0f));

    fx fBreath = FX(breath), fA3 = FX(a3), fA4 = FX(a4), fSt = FX(st), fAx = FX(stretchK);
    fx R = FX(rNow);
    ocx = FX(px.x);
    ocy = FX(py.x);

    // Tentacle mounds: root binary angle + height (Q16 px)
    uint8_t mA[FERRO_TENTS]; fx mH[FERRO_TENTS]; int nm = 0;
    for (auto &t : tents) if (t.on) { mA[nm] = radToBin(-t.th); mH[nm] = FX(0.16f * fminf(fmaxf(t.lam.x, 0), 1.2f) * r); nm++; }

    for (int i = 0; i < FERRO_N; i++) {
      uint8_t a = (uint8_t)(i * 2);           // binary angle, 0 = right, 64 = up
      fx f = FX_ONE + fBreath;
      f += fxMulQ15(fA3, fcos((uint8_t)(3 * a - ph3)));
      f += fxMulQ15(fA4, fcos((uint8_t)(4 * a - ph4)));
      f += fxMulQ15(mode[0].x, fcos((uint8_t)(2 * a))) + fxMulQ15(mode[1].x, fsin((uint8_t)(2 * a)));
      f += fxMulQ15(mode[2].x, fcos((uint8_t)(3 * a))) + fxMulQ15(mode[3].x, fsin((uint8_t)(3 * a)));
      f += fxMulQ15(mode[4].x, fcos((uint8_t)(4 * a))) + fxMulQ15(mode[5].x, fsin((uint8_t)(4 * a)));
      f += fxMulQ15(fSt, fcos((uint8_t)(2 * (a - aV))));
      f += fxMulQ15(fAx, fcos((uint8_t)(2 * (a - aX))));
      fx rr = fxMul(R, f);
      baseRad[i] = fxF(rr);
      for (int j = 0; j < nm; j++) {          // a tentacle roots in a mound (q^8 bump)
        int32_t q = fcos((uint8_t)(a - mA[j]));
        if (q <= 0) continue;
        int64_t q2 = ((int64_t)q * q) >> 15, q4 = (q2 * q2) >> 15, q8 = (q4 * q4) >> 15;
        rr += (fx)(((int64_t)mH[j] * q8) >> 15);
      }
      rad[i] = rr;
    }
    // Spikes: each slot adds the 9-vertex profile at axis + 15 deg * j
    for (int j = 0; j < FERRO_SLOTS; j++) {
      fx h = slot[j].x;
      if (h < FX(0.4f)) continue;
      int c = (((int)aX * 256 + j * 2731 + 128) >> 9) & (FERRO_N - 1);   // (aX + j * 10.667) / 2, rounded
      for (int d = -4; d <= 4; d++) rad[(c + d) & (FERRO_N - 1)] += fxMulQ15(h, FERRO_SPK[abs(d)]);
    }
    for (int i = 0; i < FERRO_N; i++) {
      uint8_t a = (uint8_t)(i * 2);
      vx[i] = ocx + fxMulQ15(rad[i], fcos(a));
      vy[i] = ocy - fxMulQ15(rad[i], fsin(a));   // screen y grows down
    }
  }

  float radiusAt(float th) const {                // base radius at screen angle th
    int i = ((radToBin(-th) + 1) >> 1) & (FERRO_N - 1);
    return baseRad[i];
  }

  // ---- tentacles ----------------------------------------------------------------
  int freeTent() { for (int i = 0; i < FERRO_TENTS; i++) if (!tents[i].on) return i; return -1; }

  // Room from the body toward screen angle a, to the glass (px)
  float roomToward(float a) const {
    float c = tcos(a), s = tsin(a), best = 200;
    if (c > 0.05f) best = fminf(best, (OLED_WIDTH - 8 - px.x) / c);
    if (c < -0.05f) best = fminf(best, (7 - px.x) / c);
    if (s > 0.05f) best = fminf(best, (OLED_HEIGHT - 8 - py.x) / s);
    if (s < -0.05f) best = fminf(best, (7 - py.x) / s);
    return best;
  }

  // Aim where there's room, mostly toward the middle of the screen
  float roomyAngle() {
    float toMid = atan2f(32 - py.x, 64 - px.x), best = toMid, bestRoom = 0;
    for (int k = 0; k < 6; k++) {
      float a = toMid + eyeRnd(-1.6f, 1.6f), rm = roomToward(a);
      if (rm > bestRoom) { bestRoom = rm; best = a; }
    }
    return best;
  }

  void spawnTent(uint8_t kind, float th, float aim, float L, float swA, float swF, float ph, float curl, unsigned long hold, float growF) {
    int idx = freeTent();
    if (idx < 0) return;
    FerroTent &t = tents[idx];
    t.on = true; t.kind = kind; t.phase = FT_GROW;
    t.th = th; t.aim = aim;
    t.L = fminf(L, roomToward(aim) - r - 2);
    if (t.L < 0.6f * r) { t.on = false; return; }
    t.swA = swA; t.swF = swF; t.swPh = ph; t.curl = curl;
    t.lam.init(0, growF, 0.6f); t.lam.t = 1;
    t.holdUntil = hold;               // ms, turned into a deadline when it arrives
    float rb = radiusAt(th), c = tcos(th), s = tsin(th);
    for (int i = 0; i < FERRO_NN; i++) { t.nx[i] = px.x + rb * c; t.ny[i] = py.x + rb * s; t.vx[i] = t.vy[i] = 0; }
    mode[0].kick(0.5f);
  }

  void spawnTentMove(uint8_t kind, unsigned long now) {
    float bigReach = eyeRnd01() < 0.6f ? eyeRnd(1.8f, 2.4f) : eyeRnd(1.1f, 1.5f);
    switch (kind) {
      case FT_REACH: {
        float a = roomyAngle();
        spawnTent(FT_REACH, a, a, r * bigReach, 0.35f, 0.6f, eyeRnd(0, TWO_PI), eyeRnd(-0.5f, 0.5f), random(1200, 2600), 2.0f);
      } break;
      case FT_FEELER: {                  // a mirrored pair either side of the axis
        float ax = -axis.x * DEG_TO_RAD, off = eyeRnd(0.6f, 0.9f), L = r * eyeRnd(0.9f, 1.4f), ph = eyeRnd(0, TWO_PI);
        spawnTent(FT_FEELER, ax + off, ax + off * 1.3f, L, 0.55f, 1.1f, ph, 0.4f, random(2000, 3200), 2.2f);
        spawnTent(FT_FEELER, ax - off, ax - off * 1.3f, L, -0.55f, 1.1f, ph, -0.4f, random(2000, 3200), 2.2f);
      } break;
      case FT_FLICK: {
        float a = roomyAngle();
        spawnTent(FT_FLICK, a, a, r * 1.5f, 0.15f, 0.3f, 0, eyeRnd(-0.8f, 0.8f), 120, 4.0f);
      } break;
      case FT_ARM: {                     // reach for both walls at once
        spawnTent(FT_ARM, 0, 0, 120, 0.2f, 0.5f, 0, -0.3f, random(1500, 2500), 1.6f);
        spawnTent(FT_ARM, PI, PI, 120, -0.2f, 0.5f, 0, 0.3f, random(1500, 2500), 1.6f);
      } break;
    }
  }

  void stepTent(FerroTent &t, unsigned long now, float dt) {
    t.lam.step(dt);
    if (t.phase == FT_GROW && t.lam.x > 0.9f) { t.phase = FT_HOLD; t.holdUntil = now + t.holdUntil; mode[0].kick(-0.4f); }
    if (t.phase == FT_HOLD && (long)(now - t.holdUntil) >= 0) {
      t.phase = FT_RETRACT; t.lam.t = 0; t.lam.f = t.kind == FT_FLICK ? 4.0f : 2.4f; t.lam.z = 0.75f; t.curl *= 2.0f;
    }
    if (t.phase == FT_RETRACT && t.lam.x < 0.06f) { t.on = false; mode[0].kick(-0.6f); return; }

    float rb = radiusAt(t.th), ca = tcos(t.th), sa = tsin(t.th);
    t.nx[0] = px.x + 0.8f * rb * ca; t.ny[0] = py.x + 0.8f * rb * sa;
    t.nx[1] = px.x + rb * ca;        t.ny[1] = py.x + rb * sa;
    float Lc = fmaxf(0, t.lam.x) * t.L, seg = Lc / (FERRO_NN - 2);
    float swA = t.phase == FT_RETRACT ? 0 : t.swA;
    float qx = t.nx[1], qy = t.ny[1];
    int n = (int)(dt / 0.008f) + 1;
    float h = dt / n;
    for (int i = 2; i < FERRO_NN; i++) {
      float s = (i - 1.5f) / (FERRO_NN - 2);
      float psi = t.aim + t.curl * s * s + swA * s * tsin(TWO_PI * t.swF * tSec - 2.5f * s + t.swPh);
      qx += seg * tcos(psi); qy += seg * tsin(psi);
      float u = i / (float)(FERRO_NN - 1);
      float f = 3.2f * (1 - 0.5f * u), z = 0.6f - 0.25f * u, w = TWO_PI * f;
      for (int k = 0; k < n; k++) {
        t.vx[i] += (w * w * (qx - t.nx[i]) - 2 * z * w * t.vx[i]) * h; t.nx[i] += t.vx[i] * h;
        t.vy[i] += (w * w * (qy - t.ny[i]) - 2 * z * w * t.vy[i]) * h; t.ny[i] += t.vy[i] * h;
      }
    }
    for (int i = 2; i < FERRO_NN; i++) {   // length pass, then stay on the glass
      float dx = t.nx[i] - t.nx[i - 1], dy = t.ny[i] - t.ny[i - 1], d = fmaxf(1e-3f, sqrtf(dx * dx + dy * dy));
      float dd = constrain(d, 0.85f * seg, 1.15f * seg);
      if (dd != d) { t.nx[i] = t.nx[i - 1] + dx / d * dd; t.ny[i] = t.ny[i - 1] + dy / d * dd; }
      t.nx[i] = constrain(t.nx[i], 7.0f, OLED_WIDTH - 8.0f);    // bulb (r 4.3) + 2 px off the glass
      t.ny[i] = constrain(t.ny[i], 7.0f, OLED_HEIGHT - 8.0f);
    }
  }

  // Width (art direction §1.5): plump root flaring into its mound, >= 4 px
  // core, a round 7-8 px bulb on a 4-5 px neck; short tentacles are thinner
  void prepTent(FerroTent &t) {
    float Lc = fmaxf(0, t.lam.x) * t.L;
    float w0 = (t.kind == FT_FLICK ? 0.28f : 0.34f) * r, wTip = 2.6f;   // half-widths: core >= 5 px
    float wS = 0.65f + 0.35f * constrain(Lc / (0.5f * r), 0.0f, 1.0f);
    float b = 4.3f;                                                     // bulb ~8.5 px across
    for (int i = 0; i < FERRO_NN; i++) {
      float s = fmaxf(0.0f, (i - 1) / (float)(FERRO_NN - 2)), u = 1 - s;
      float hw = wTip + (w0 - wTip) * u * sqrtf(u) + (s < 0.18f ? 0.35f * w0 * (1 - s / 0.18f) * (1 - s / 0.18f) : 0);
      if (i == 0) hw = w0 * 1.3f;
      if (i == FERRO_NN - 2) hw = fminf(hw, 0.8f * b);
      t.hw[i] = hw * wS;
      int a = max(0, i - 1), c = min(FERRO_NN - 1, i + 1);
      float dx = t.nx[c] - t.nx[a], dy = t.ny[c] - t.ny[a], d = sqrtf(dx * dx + dy * dy);
      if (d < 1e-3f) { t.ux[i] = tcos(t.th); t.uy[i] = tsin(t.th); } else { t.ux[i] = dx / d; t.uy[i] = dy / d; }
    }
    t.bulbR = b * wS;
  }

  // Polygon around the chain: left side, bulb, right side back (Q16)
  int buildTent(const FerroTent &t, fx *ox, fx *oy) {
    int k = 0;
    for (int i = 0; i < FERRO_NN - 1; i++) {
      ox[k] = FX(t.nx[i] - t.uy[i] * t.hw[i]); oy[k] = FX(t.ny[i] + t.ux[i] * t.hw[i]); k++;
    }
    const int tip = FERRO_NN - 1;
    float br = t.bulbR, bx = t.nx[tip], by = t.ny[tip];
    float c = -t.uy[tip], s = t.ux[tip];       // tangent turned +90 deg, then 8 steps of -22.5 deg
    const float sc = 0.92388f, ss = -0.38268f;
    for (int j = 0; j <= 8; j++) {
      ox[k] = FX(bx + br * c); oy[k] = FX(by + br * s); k++;
      float c2 = c * sc - s * ss; s = c * ss + s * sc; c = c2;
    }
    for (int i = FERRO_NN - 2; i >= 0; i--) {
      ox[k] = FX(t.nx[i] + t.uy[i] * t.hw[i]); oy[k] = FX(t.ny[i] - t.ux[i] * t.hw[i]); k++;
    }
    return k;
  }

  // ---- union fill -----------------------------------------------------------------
  struct Edge { fx x0, x1, y0, slope; int8_t dir; };
  Edge e[FERRO_MAX_EDGES];
  int ne = 0;
  fx fillMinX, fillMaxX;

  // Add a closed polygon; its orientation is normalised so every shape winds
  // the same way and overlaps add up instead of cancelling into holes
  void addPoly(const fx *x, const fx *y, int n) {
    int64_t area = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += (int64_t)(x[i] >> 4) * (y[j] >> 4) - (int64_t)(x[j] >> 4) * (y[i] >> 4); }
    int8_t flip = area < 0 ? -1 : 1;
    if (ne == 0) { fillMinX = x[0]; fillMaxX = x[0]; }
    for (int i = 0; i < n && ne < FERRO_MAX_EDGES; i++) {
      int j = (i + 1) % n;
      fx ax = x[i], ay = y[i], bx = x[j], by = y[j];
      if (ax < fillMinX) fillMinX = ax;
      if (ax > fillMaxX) fillMaxX = ax;
      if (ax == bx) continue;
      int8_t dir = flip;
      if (ax > bx) { fx t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; dir = -dir; }
      e[ne].x0 = ax; e[ne].x1 = bx; e[ne].y0 = ay; e[ne].dir = dir;
      e[ne].slope = (fx)(((int64_t)(by - ay) << 16) / (bx - ax));
      ne++;
    }
  }

  // Column-major non-zero winding fill: U8g2 keeps 8 vertical pixels per byte,
  // so each span is a few masked byte writes. Pixel centres are sampled.
  void fillEdges(uint8_t *buf) {
    int c0 = max(0, (int)(fillMinX >> 16)), c1 = min(OLED_WIDTH - 1, (int)(fillMaxX >> 16) + 1);
    for (int x = c0; x <= c1; x++) {
      fx sx = (x << 16) + (FX_ONE >> 1);
      fx ys[FERRO_MAX_XR]; int8_t ds[FERRO_MAX_XR]; int n = 0;
      for (int k = 0; k < ne && n < FERRO_MAX_XR; k++) {
        if (sx < e[k].x0 || sx >= e[k].x1) continue;
        fx y = e[k].y0 + fxMul(sx - e[k].x0, e[k].slope);
        int m = n++;
        while (m > 0 && ys[m - 1] > y) { ys[m] = ys[m - 1]; ds[m] = ds[m - 1]; m--; }
        ys[m] = y; ds[m] = e[k].dir;
      }
      int wind = 0;
      for (int k = 0; k + 1 < n; k++) {
        wind += ds[k];
        if (wind == 0) continue;
        int ya = (int)((ys[k] + (FX_ONE >> 1) - 1) >> 16);
        int yb = (int)((ys[k + 1] + (FX_ONE >> 1) - 1) >> 16);
        blackVSpan(buf, x, ya, yb);
      }
    }
  }

  static void blackVSpan(uint8_t *buf, int x, int y0, int y1) {   // rows [y0, y1)
    if (y0 < 0) y0 = 0;
    if (y1 > OLED_HEIGHT) y1 = OLED_HEIGHT;
    while (y0 < y1) {
      int page = y0 >> 3, bit0 = y0 & 7;
      int bit1 = min(8, bit0 + (y1 - y0));
      uint8_t mask = (uint8_t)(((1u << bit1) - 1) & ~((1u << bit0) - 1));
      buf[page * OLED_WIDTH + x] &= ~mask;
      y0 += bit1 - bit0;
    }
  }

  static inline void setPx(uint8_t *buf, int x, int y, bool white) {
    if ((unsigned)x >= OLED_WIDTH || (unsigned)y >= OLED_HEIGHT) return;
    uint8_t *b = &buf[(y >> 3) * OLED_WIDTH + x];
    if (white) *b |= (1 << (y & 7)); else *b &= ~(1 << (y & 7));
  }

  // 1 px white crescent at 10:30, r - 4.5 in from the rim, ~35 deg of arc,
  // 2 px thick in its middle third. Locked to the light (art direction §2.1).
  void drawGlint(uint8_t *buf) {
    if (r < 14) return;
    if (sqrtf(px.v * px.v + py.v * py.v) > 0.5f * 2.5f * r) return;   // fast move
    const int vi0 = 48;                                         // vertex at 135 deg (96 / 2)
    fx rest = FX(r);
    for (int d = -6; d <= 6; d += 3) if (abs(rad[vi0 + d] - rest) > FX(3.0f)) return;   // spike or mound there
    for (int s = -12; s <= 12; s++) {
      uint8_t a = (uint8_t)(96 + s);
      fx rg = rad[(a >> 1) & (FERRO_N - 1)] - FX(4.5f);
      setPx(buf, (int)((ocx + fxMulQ15(rg, fcos(a)) + (FX_ONE >> 1)) >> 16),
                 (int)((ocy - fxMulQ15(rg, fsin(a)) + (FX_ONE >> 1)) >> 16), true);
      if (s >= -4 && s <= 4) {
        fx ri = rg - FX_ONE;
        setPx(buf, (int)((ocx + fxMulQ15(ri, fcos(a)) + (FX_ONE >> 1)) >> 16),
                   (int)((ocy - fxMulQ15(ri, fsin(a)) + (FX_ONE >> 1)) >> 16), true);
      }
    }
  }

  static void cornerMask(uint8_t *buf) {        // 5 black pixels per corner
    static const int8_t p[5][2] = { {0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2} };
    for (auto &q : p) {
      setPx(buf, q[0], q[1], false);
      setPx(buf, OLED_WIDTH - 1 - q[0], q[1], false);
      setPx(buf, q[0], OLED_HEIGHT - 1 - q[1], false);
      setPx(buf, OLED_WIDTH - 1 - q[0], OLED_HEIGHT - 1 - q[1], false);
    }
  }
};

FerroBlob ferro;

#endif // FERRO_BLOB_H
