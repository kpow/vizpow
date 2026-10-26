#ifndef PARTY_BLOB_H
#define PARTY_BLOB_H

#include <Arduino.h>
#include "config.h"
#include "bot_eye_model.h"

// ============================================================================
// Party mode — the face as a small ferrofluid blob in a corner
// ============================================================================
// The ambient effect gets the whole screen. The face shrinks into a black blob
// with a wobbling edge, a rim, a sheen, a floor shadow and the real eyes
// inside. Plan: docs/plan-party-mode.md. Motion numbers come from
// docs/party-mode-motion-spec.md, sizes and placement from
// docs/party-mode-layout-spec.md (the plan wins where they differ).
//
// Party mode is a flag on the ambient scene (botBackgroundStyle 4), not a
// style of its own. He picks his own corner; there is no corner setting.
//
// Turning it on, he blinks and hops from the middle of the screen into his
// corner as the outline around his eyes shrinks into the drop; turning it off
// runs that backwards. Shake him for spikes all round; tap for a spike fan,
// tap the blob and he dashes to another corner. On the CoreS3 / Stackchan the
// spikes pump to the music; the 1.69 has no mic, so it sends itself a pulse.
//
// The outline is a 120-point polygon whose radius is a function of angle,
// filled row by row. Sample angles start at the spike attractor, so every
// spike tip lands on a vertex and never flickers. Every value moves on a
// BotSpring stepped with real time. No per-frame heap.
// ============================================================================

extern GfxDevice *gfx;
extern uint8_t botBackgroundStyle;
extern uint16_t botFaceColor;
uint16_t* lcdCanvasPixels();

#define PARTY_N          120     // outline vertices, 3° apart
#define PARTY_SPIKES     9       // spike fan slots, 18° (6 vertices) apart
#define PARTY_MAX_X      16      // edge crossings per row (spikes make concave rows)
#define PARTY_EDGE_GAP   2       // spike envelope / shadow to the screen edge, px
#define PARTY_MORPH_R    78.5f   // drop radius at the start of the morph: with squash 0.31 it is the
#define PARTY_MORPH_J    0.31f   // 103 x 54 px ellipse of the ambient scene's 5 px stroke around the eyes

// Overlays move out of his way (layout spec §3)
#ifdef BOARD_ESP32S3_LCD_169
  #define PARTY_CLOCK_INSET 22   // clock pill x from the side edge, clear of the rounded corner
#else
  #define PARTY_CLOCK_INSET 8
#endif
#define PARTY_TOAST_LOW_Y (LCD_HEIGHT - 44)   // toast slot while he's up top (236 on the 1.69: clears the swipe handle)

bool    partyMode = false;       // NVS "party"
uint8_t partySize = 1;           // NVS "pSize": 0 S, 1 M, 2 L

// Pre-rounded pixels per size (layout spec §1, §6): eye scale, body radius,
// spike envelope (1.2 r), extent below centre (shadow), wander box.
struct PartyTier { float k; int16_t r, env, below, wanderW, wanderH; bool brows; };
static const PartyTier PARTY_TIERS[3] = {
  {0.30f, 37, 45, 47, 22, 17, false},   // S: legibility floor, X-eye strokes are 2 px here
  {0.36f, 44, 53, 55, 26, 20, true },   // M: same eye scale as the album-art corner
  {0.44f, 54, 65, 68, 32, 24, true },   // L
};

// Corners: 0 top right, 1 bottom right, 2 bottom left, 3 top left
static inline bool partyRight(uint8_t c) { return c == 0 || c == 1; }
static inline bool partyTop(uint8_t c)   { return c == 0 || c == 3; }

// Render cost for GET /state (blob + eyes, not the effect); reading starts a fresh window
// stageUs: summed per stage (step, outline, shadow, rim + body, sheen, eyes) to find the cost
#define PARTY_STAGES 6
struct PartyRenderStats { uint32_t sumUs, count, maxUs; uint32_t stageUs[PARTY_STAGES]; };
PartyRenderStats partyRenderStats = {0, 0, 0, {0}};

String partyRenderStatsJson() {
  PartyRenderStats &st = partyRenderStats;
  String json = "{\"avgUs\":" + String(st.count ? st.sumUs / st.count : 0) +
                ",\"maxUs\":" + String(st.maxUs) + ",\"renders\":" + String(st.count) + ",\"stagesUs\":[";
  for (int i = 0; i < PARTY_STAGES; i++) json += String(i ? "," : "") + String(st.count ? st.stageUs[i] / st.count : 0);
  json += "]}";
  st.sumUs = st.count = st.maxUs = 0;
  for (int i = 0; i < PARTY_STAGES; i++) st.stageUs[i] = 0;
  return json;
}

// ---- RGB565 helpers. The canvas stores pixels byte-swapped. ----------------
static inline uint16_t partySwap(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t partyDark75(uint16_t p) { return p - ((p >> 2) & 0x39E7); }
static inline uint16_t partyDark50(uint16_t p) { return (p >> 1) & 0x7BEF; }

// ---- Scanline fill ----------------------------------------------------------
// Each polygon edge drops its crossing into the rows it spans (x in 1/16 px),
// so a row only sorts the few crossings it has instead of testing every edge.
// Two tables: the rim (outer) and body (inner) outlines are filled in one pass.
static int16_t partyRowX[2][LCD_HEIGHT][PARTY_MAX_X];
static uint8_t partyRowN[2][LCD_HEIGHT];

// Crossings of the polygon at row centres y + 0.5 into table p; ya..yb = rows touched
static void partyScan(int p, const float *vx, const float *vy, int n, int &ya, int &yb) {
  memset(partyRowN[p], 0, LCD_HEIGHT);
  ya = LCD_HEIGHT; yb = -1;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float x0 = vx[j], y0 = vy[j], x1 = vx[i], y1 = vy[i];
    if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    int r0 = max(0, (int)ceilf(y0 - 0.5f)), r1 = min(LCD_HEIGHT - 1, (int)ceilf(y1 - 0.5f) - 1);   // y0 <= y+.5 < y1
    if (r1 < r0) continue;
    float dxdy = (x1 - x0) / (y1 - y0), x = (x0 + (r0 + 0.5f - y0) * dxdy) * 16, dx = dxdy * 16;
    for (int y = r0; y <= r1; y++, x += dx) {
      uint8_t &m = partyRowN[p][y];
      if (m < PARTY_MAX_X) partyRowX[p][y][m++] = (int16_t)lroundf(x);
    }
    ya = min(ya, r0); yb = max(yb, r1);
  }
}

// Row y of table p as sorted, screen-clipped pixel spans; returns the span count
static int partySpans(int p, int y, int16_t *xa, int16_t *xb) {
  int16_t *xs = partyRowX[p][y];
  int m = partyRowN[p][y];
  for (int i = 1; i < m; i++) {
    int16_t v = xs[i];
    int j = i - 1;
    while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
    xs[j + 1] = v;
  }
  m &= ~1;
  int k = 0;
  for (int i = 0; i < m; i += 2) {
    int a = max(0, (xs[i] - 8 + 15) >> 4), b = min(LCD_WIDTH - 1, (xs[i + 1] - 8) >> 4);   // ceil / floor of x - 0.5
    if (b >= a) { xa[k] = a; xb[k] = b; k++; }
  }
  return k;
}

// Even-odd fill: span(y, xa, xb) for every inside run, clipped to the screen
template <typename Span>
static void partyFillPoly(const float *vx, const float *vy, int n, Span span) {
  int ya, yb;
  partyScan(0, vx, vy, n, ya, yb);
  int16_t xa[PARTY_MAX_X / 2], xb[PARTY_MAX_X / 2];
  for (int y = ya; y <= yb; y++) {
    int k = partySpans(0, y, xa, xb);
    for (int i = 0; i < k; i++) span(y, xa[i], xb[i]);
  }
}

static float partyVX[PARTY_N], partyVY[PARTY_N], partyRR[PARTY_N], partyC[PARTY_N], partyS[PARTY_N];
static float partyCos3[PARTY_N], partySin3[PARTY_N];   // cos/sin(i x 3°)
static float partyStepC[4], partyStepS[4];             // cos/sin(n x 3°) for the n = 3, 4, 5 edge waves and the n = 7 shiver
static const float PARTY_SPK_LUT[3] = {1.0f, 0.41f, 0.09f};   // (1 - j/3)^2.2: sharp tip, concave flanks

// Two ways to change corner. A poke is a startled dash: a short crouch, a fast
// spring, big kicks. A roam is his own idea: a glance at where he's going, then
// a stroll on a slower spring. The path bows 45 % toward the screen centre.
struct PartyRun { uint16_t leadMs; float f, z, jGo, lift, reach, jLand, land; };
static const PartyRun PARTY_POKE = {260, 1.5f, 0.85f, -0.14f, 0.12f, 0.45f, 0.16f, 0.7f};
static const PartyRun PARTY_ROAM = {700, 0.95f, 0.9f, -0.08f, 0.07f, 0.30f, 0.10f, 0.45f};
#define PARTY_ROAM_MIN_MS 25000   // he wanders to another corner every 25-60 s
#define PARTY_ROAM_MAX_MS 60000

struct PartyBlob {
  // Body
  BotSpring wx, wy;          // centre (wander), px
  BotSpring J, L, St;        // squash (cos 2θ), hop lift px (+ up), velocity stretch
  BotSpring al, F;           // spike attractor angle, field strength 0..1
  BotSpring spk[PARTY_SPIKES];
  BotSpring g, E;            // bulge toward gaze / attractor, agitation 0..1
  BotSpring ox, oy;          // eye offset inside the drop (trails the body)
  BotSpring D;               // poke dent depth, px (<= 0)
  BotSpring Q;               // corner run progress 0..1
  BotSpring P;               // enter / exit morph: 0 = the face, 1 = the drop in its corner
  BotSpring hP;              // porcupine spikes all round (shake), px
  BotSpring beatSm;          // smoothed beat envelope
  float pkL, pkJ, pkD;       // kick normalisers

  bool active, ready, haveCorner;
  uint8_t corner;
  unsigned long lastMs, fieldUntil, evAt, nextWander, nextPulse;
  float fieldT, alT, phiV, bulgeA, B, t;
  float wTx, wTy;
  float jit[PARTY_SPIKES], ph[6];
  float prevTx, prevTy;
  int16_t icx, icy, iox, ioy;   // shared integer centre and eye offset (0.6 px hysteresis)
  float sheen[3];               // low-passed show colour, RGB888
  float thD, tapT;              // dent angle; tap agitation, decays
  unsigned long nextRoam;
  // Corner run (poke dash or roam)
  const PartyRun *run;          // null when not running
  bool going;
  uint8_t runTo;
  unsigned long goAt;
  float fromX, fromY, dstX, dstY;
  // Morph, shake, fidgets, beat
  uint8_t morph;                // 0 none, 1 entering, 2 exiting
  unsigned long exitAt, porcUntil, shakeAt, shiverAt, nextFidget, padUntil, lastHop, lastPop;
  uint8_t lastFidget;
  float prevEnv;

  const PartyTier &tier() const { return PARTY_TIERS[partySize < 3 ? partySize : 1]; }

  // Wander box runs inward from the tuck point; home is its centre (layout spec §2)
  void box(uint8_t c, float &x0, float &x1, float &y0, float &y1) const {
    const PartyTier &T = tier();
    float tx = partyRight(c) ? LCD_WIDTH - PARTY_EDGE_GAP - T.env : PARTY_EDGE_GAP + T.env;
    float ty = partyTop(c) ? PARTY_EDGE_GAP + T.env : LCD_HEIGHT - PARTY_EDGE_GAP - T.below;
    x0 = tx; x1 = tx + (partyRight(c) ? -T.wanderW : T.wanderW);
    y0 = ty; y1 = ty + (partyTop(c) ? T.wanderH : -T.wanderH);
  }
  void home(uint8_t c, float &hx, float &hy) const {
    float x0, x1, y0, y1;
    box(c, x0, x1, y0, y1);
    hx = (x0 + x1) / 2; hy = (y0 + y1) / 2;
  }
  // Spikes only point into the show, within 60° of the line to the screen centre
  float alphaHome() const {
    float hx, hy;
    home(corner, hx, hy);
    return atan2f(LCD_HEIGHT / 2.0f - hy, LCD_WIDTH / 2.0f - hx);
  }
  static float wrapPi(float a) {
    a = fmodf(a + PI, TWO_PI);
    if (a < 0) a += TWO_PI;
    return a - PI;
  }
  float clampInward(float a) const {
    float h = alphaHome();
    return h + constrain(wrapPi(a - h), -60 * DEG_TO_RAD, 60 * DEG_TO_RAD);
  }

  void init() {
    for (int i = 0; i < PARTY_N; i++) { partyCos3[i] = cosf(i * 3 * DEG_TO_RAD); partySin3[i] = sinf(i * 3 * DEG_TO_RAD); }
    const int harm[4] = {3, 4, 5, 7};
    for (int h = 0; h < 4; h++) { partyStepC[h] = cosf(harm[h] * 3 * DEG_TO_RAD); partyStepS[h] = sinf(harm[h] * 3 * DEG_TO_RAD); }
    wx.init(0, 0.55f, 0.75f); wy.init(0, 0.55f, 0.75f);
    J.init(0.05f, 2.2f, 0.45f); L.init(0, 1.9f, 0.42f); St.init(0, 4, 0.8f);
    al.init(0, 1.2f, 0.9f); F.init(0, 3, 0.8f);
    for (int k = 0; k < PARTY_SPIKES; k++) spk[k].init(0, 2.6f, 0.55f);
    g.init(0, 1.5f, 0.9f); E.init(0, 1.2f, 1);
    ox.init(0, 2.4f, 0.5f); oy.init(0, 2.4f, 0.5f);
    D.init(0, 3.2f, 0.32f); Q.init(0, 1.5f, 0.85f);
    P.init(1, 1.4f, 0.82f); hP.init(0, 5, 0.6f); beatSm.init(0, 3, 0.7f);
    pkL = botSpringPeak(1.9f, 0.42f);
    pkJ = botSpringPeak(2.2f, 0.45f);
    pkD = botSpringPeak(3.2f, 0.32f);
    active = false;
    ready = true;
  }

  // First party frame. When party mode has just started he picks a corner and
  // morphs into it from the face; back from a sheet he is simply there again.
  void enter(unsigned long now) {
    if (!ready) init();
    bool fresh = !haveCorner;
    if (fresh) { corner = random(4); haveCorner = true; }
    float hx, hy;
    home(corner, hx, hy);
    wx.snap(hx); wy.snap(hy); wTx = hx; wTy = hy;
    J.snap(0.05f); L.snap(0); St.snap(0); F.snap(0); g.snap(0); E.snap(0); ox.snap(0); oy.snap(0); D.snap(0);
    run = nullptr; tapT = 0; thD = 0;
    nextRoam = now + (unsigned long)eyeRnd(PARTY_ROAM_MIN_MS, PARTY_ROAM_MAX_MS);
    for (int k = 0; k < PARTY_SPIKES; k++) spk[k].snap(0);
    alT = alphaHome(); al.snap(alT);
    for (int i = 0; i < 6; i++) ph[i] = eyeRnd(0, TWO_PI);
    fieldT = 0; fieldUntil = 0; evAt = 0; phiV = 0; bulgeA = alT;
    icx = (int16_t)lroundf(hx); icy = (int16_t)lroundf(hy); iox = ioy = 0;
    sheen[0] = 40; sheen[1] = 40; sheen[2] = 60;
    prevTx = botEyes.tx; prevTy = botEyes.ty;
    lastMs = now;
    nextWander = now + 1000;
    nextPulse = now + (unsigned long)eyeRnd(5000, 12000);
    nextFidget = now + (unsigned long)eyeRnd(12000, 25000);
    porcUntil = shakeAt = shiverAt = padUntil = lastHop = lastPop = 0;
    hP.snap(0); beatSm.snap(0); prevEnv = 0;
    botEyes.blinkStart(now, BW_SWAP);   // every swap between face and drop hides under a blink
    active = true;
    if (fresh) {
      morph = 1;
      P.snap(0); P.t = 1;
      J.snap(PARTY_MORPH_J);
      wx.snap(BOT_FACE_CX); wy.snap(BOT_FACE_CY);
      icx = BOT_FACE_CX; icy = BOT_FACE_CY;
    } else {
      morph = 0;
      P.snap(1);
      J.v += 0.14f / pkJ;              // land with a squash
      field(alT, 0.7f, 300, now);      // and a spike burst
    }
  }
  // Party mode turned off: a beat reaching for the middle, then the hop back
  void beginExit(unsigned long now) {
    if (morph == 2) return;
    if (run) { run = nullptr; }
    morph = 2; exitAt = now;
    field(alphaHome(), 0.6f, 200, now);
    J.v += 0.10f / pkJ;
    botEyes.padX = constrain((LCD_WIDTH / 2.0f - wx.x) / 40, -1.0f, 1.0f);
    botEyes.padY = constrain((LCD_HEIGHT / 2.0f - wy.x) / 40, -1.0f, 1.0f);
  }
  // Turned back on mid-exit: head for the corner again
  void cancelExit() {
    if (morph != 2) return;
    morph = 1; P.t = 1;
    botEyes.padX = botEyes.padY = 0;
  }
  bool busy() const { return run || morph; }
  void shake(unsigned long now) {
    if (morph) return;
    porcUntil = now + 450; shakeAt = now; tapT = 1;
    botEyes.blinkStart(now, BW_SWAP);
  }
  // Morph shapes: radius, eye scale, and how much of the corner behaviour is on
  float morphR() const {
    float e = constrain(P.x, 0.0f, 1.0f);
    return PARTY_MORPH_R + (tier().r - PARTY_MORPH_R) * eyeSmooth(min(1.0f, 2 * e));
  }
  float morphK() const {
    float e = constrain(P.x, 0.0f, 1.0f);
    return 1 + (tier().k - 1) * eyeSmooth(min(1.0f, 1.3f * e));
  }
  float morphOn() const { return eyeSmooth(min(1.0f, 2 * constrain(P.x, 0.0f, 1.0f))); }
  // keepCorner: party is only paused (a sheet is open), so he comes back to the same corner
  void leave(bool keepCorner) {
    if (active && (run || morph || padUntil)) { botEyes.padX = botEyes.padY = 0; }
    active = false; run = nullptr; morph = 0;
    if (!keepCorner) haveCorner = false;
  }

  bool contains(int16_t x, int16_t y) const { return hypotf(x - icx, y - icy) < tier().r * 1.05f; }
  bool running() const { return run != nullptr; }

  // Tap on the blob: dent where the finger lands, then dash to another corner
  void poke(int16_t x, int16_t y, unsigned long now) {
    if (busy()) return;
    thD = atan2f(y - icy, x - icx);
    D.v += -0.18f * tier().r / pkD;
    J.v += 0.16f / pkJ;
    tapT = 1;
    botEyes.blinkStart(now, BW_SWAP);
    startRun((corner + 1 + random(3)) % 4, PARTY_POKE, now);
  }
  // Tap elsewhere: a spike fan reaches for the finger, and he hops
  void tapAt(int16_t x, int16_t y, unsigned long now) {
    if (busy()) return;
    field(atan2f(y - icy, x - icx), 1.0f, 500, now);
    L.v += 0.22f * tier().r / pkL;
    J.v -= 0.10f / pkJ;
    tapT = 1;
    nextWander = now + 1200;
  }
  // Roaming mostly picks a neighbouring corner (a walk along one edge); one move in four crosses the diagonal
  void roam(unsigned long now) {
    uint8_t to = random(4) ? (corner + (random(2) ? 1 : 3)) % 4 : (corner + 2) % 4;
    startRun(to, PARTY_ROAM, now);
  }
  void startRun(uint8_t to, const PartyRun &m, unsigned long now) {
    run = &m; going = false; runTo = to; goAt = now + m.leadMs;
    home(to, dstX, dstY);
    // Look where he's going for the whole trip (the look pad is free: BOT_IMU_GAZE is off)
    botEyes.padX = constrain((dstX - wx.x) / 30, -1.0f, 1.0f);
    botEyes.padY = constrain((dstY - wy.x) / 30, -1.0f, 1.0f);
  }

  // An event pulls a spike fan toward dir, at strength F, for holdMs
  void field(float dir, float f, unsigned long holdMs, unsigned long now, bool free = false, bool slow = false) {
    alT = free ? dir : clampInward(dir); fieldT = f; fieldUntil = now + holdMs; evAt = now;
    F.f = slow ? 1.2f : 3;
    for (int k = 0; k < PARTY_SPIKES; k++) jit[k] = eyeRnd(0.85f, 1.15f);
  }

  // ---- Motion ---------------------------------------------------------------
  void step(unsigned long now) {
    float dt = min(0.05f, (now - lastMs) / 1000.0f);
    lastMs = now;
    if (dt <= 0) return;
    t = eyeSecs(now);
    if (morph) stepMorph(now, dt);
    const float R = morphR();
    bool free = !run && !morph;   // settled in his corner

    // Corner run: crouch for the lead-in, then follow an arc to the new corner's home
    if (run) stepRun(now, dt);
    else if (free && eyeReached(now, nextRoam)) roam(now);
    free = !run && !morph;

    if (padUntil && eyeReached(now, padUntil)) { padUntil = 0; if (!run && !morph) botEyes.padX = botEyes.padY = 0; }
    if (free && eyeReached(now, nextFidget)) fidget(now);

    // Wander: a new spot in the corner box every 3.5-8 s; half the long moves are a scoot hop
    if (free && eyeReached(now, nextWander)) {
      float x0, x1, y0, y1;
      box(corner, x0, x1, y0, y1);
      float nx = eyeRnd(min(x0, x1), max(x0, x1)), ny = eyeRnd(min(y0, y1), max(y0, y1));
      if (hypotf(nx - wTx, ny - wTy) > 0.15f * R && random(2)) { L.v += 0.06f * R / pkL; J.v -= 0.05f / pkJ; }
      wTx = nx; wTy = ny;
      nextWander = now + (unsigned long)eyeRnd(3500, 8000);
    }
    if (!morph && (!run || !going)) { wx.step(wTx, dt); wy.step(wTy, dt); }

    // Music: on the CoreS3 / Stackchan the spikes pump on the beat (smoothed, so
    // fast tempos pump instead of strobing). Without a mic, a spike fan every 5-12 s.
    #ifdef TARGET_CORES3
    bool heard = audioSpectrum.alive;
    float env = heard ? audioSpectrum.beatEnv : 0, bass = heard ? constrain(audioSpectrum.bass, 0.0f, 1.0f) : 0;
    #else
    bool heard = false;
    float env = 0, bass = 0;
    #endif
    beatSm.step(env, dt);
    if (heard) {
      if (free && env > 0.9f && prevEnv < 0.5f) {
        field(alphaHome(), 0.3f + 0.5f * bass, 120, now);
        if (bass > 0.6f && now - lastHop > 450) { L.v += 0.06f * R / pkL; lastHop = now; }
        if (bass > 0.7f && now - lastPop > 600) { botEyes.popS.v += 0.03f / botEyes.pkPopS; lastPop = now; }
      }
    } else if (free && eyeReached(now, nextPulse)) {
      field(alphaHome() + eyeRnd(-40, 40) * DEG_TO_RAD, 0.5f, (unsigned long)eyeRnd(500, 900), now);
      nextPulse = now + (unsigned long)eyeRnd(5000, 12000);
    }
    prevEnv = env;

    // Gaze reach: 40 % of big saccades send a small fan where he looks
    if (free && hypotf(botEyes.tx - prevTx, botEyes.ty - prevTy) > 0.07f && eyeReached(now, fieldUntil) && random(10) < 4)
      field(atan2f(botEyes.ty, botEyes.tx), 0.25f, 400, now);
    prevTx = botEyes.tx; prevTy = botEyes.ty;

    // Field, attractor, spikes. Spike k starts |k-4| x 35 ms late so the fan opens outward.
    if (eyeReached(now, fieldUntil)) fieldT = 0;
    F.step(fieldT, dt);
    al.step(al.x + wrapPi(alT - al.x), dt);
    for (int k = 0; k < PARTY_SPIKES; k++) {
      BotSpring &s = spk[k];
      bool late = (long)(now - evAt) < abs(k - 4) * 35;
      float d = (k - 4) / 2.2f;
      float target = late ? s.t : F.x * 0.42f * R * expf(-d * d) * jit[k];
      if (target > s.x) { s.f = 2.6f; s.z = 0.55f; } else { s.f = 4.5f; s.z = 0.38f; }   // pop up, snap back
      s.step(target, dt);
      if (s.x < -0.05f * R) { s.x = -0.05f * R; s.v = 0; }   // the snap-back dimple stays shallow
    }

    // Porcupine: spikes all round for 450 ms after a shake, then the snap back
    bool porc = !eyeReached(now, porcUntil);
    hP.f = porc ? 5 : 4.5f; hP.z = porc ? 0.6f : 0.38f;
    hP.step(porc ? 0.22f * R : 0, dt);

    // Squash, hop, stretch. The lift spring's undershoot is the landing splat.
    // Mid-morph the drop is the wide ellipse of the stroke around his eyes.
    float jT = 0.05f + (PARTY_MORPH_J - 0.05f) * (1 - morphOn());
    if (L.x < 0) jT += -L.x / R * 0.9f;
    J.step(jT, dt);
    L.step(0, dt);
    float vx = wx.v, vy = wy.v - L.v, speed = hypotf(vx, vy);
    if (speed > 0.2f * R) phiV = atan2f(vy, vx);   // freeze the stretch axis when slow
    St.step(0.10f * constrain(speed / (2.5f * R), 0.0f, 1.0f), dt);
    tapT *= expf(-dt / 0.6f);
    D.step(0, dt);
    E.step((run && going) || morph ? 0.8f : constrain(speed / (3 * R) + 0.6f * tapT + 0.25f * beatSm.x, 0.0f, 1.0f), dt);

    // Bulge toward the gaze, or toward the spikes while a field is up
    float gx = botEyes.gxOut, gy = botEyes.gyOut;
    float gG = 0.07f * min(1.0f, hypotf(gx / 0.35f, gy / 0.25f));
    bulgeA = F.x > 0.05f ? al.x : atan2f(gy, gx);
    g.step(max(gG, 0.10f * F.x), dt);

    // Eyes trail the body, look with the whole face, ride the squash and the breath
    B = 1 + 0.04f * sinf(TWO_PI * 0.21f * t);
    // (mid-morph they stay centred, where the face's eyes are)
    float on = morphOn();
    ox.step(0.20f * R * gx / 0.35f * on, dt);
    oy.step((0.15f * R * gy / 0.25f + 0.6f * J.x * R - 0.3f * (B - 1) * R) * on, dt);
  }

  // The hop between the face's place and his corner, along an arc that rises a
  // quarter of the way, while the outline shrinks (or grows) and the eyes scale
  void stepMorph(unsigned long now, float dt) {
    if (morph == 2 && now - exitAt > 200 && P.t != 0) {   // anticipation over: launch
      J.v += -0.12f / pkJ; L.v += 0.10f * tier().r / pkL;
      P.t = 0;
    }
    P.step(P.t, dt);
    float hx, hy;
    home(corner, hx, hy);
    float p = constrain(P.x, 0.0f, 1.03f), cx = BOT_FACE_CX, cy = BOT_FACE_CY;
    float mx = (cx + hx) / 2, my = (cy + hy) / 2 - 0.25f * hypotf(hx - cx, hy - cy);
    float x = (1 - p) * (1 - p) * cx + 2 * (1 - p) * p * mx + p * p * hx;
    float y = (1 - p) * (1 - p) * cy + 2 * (1 - p) * p * my + p * p * hy;
    wx.v = (x - wx.x) / dt; wy.v = (y - wy.x) / dt;
    wx.x = wx.t = x; wy.x = wy.t = y;
    wTx = hx; wTy = hy;
    if (morph == 1 && P.x > 0.97f) {   // landed in the corner
      morph = 0;
      J.v += 0.14f / pkJ;
      field(alphaHome(), 0.7f, 300, now);
      botEyes.blinkStart(now, BW_SWAP);
      nextWander = now + 1000;
    } else if (morph == 2 && P.t == 0 && P.x < 0.03f) {   // back to the face
      morph = 0;
      active = false; haveCorner = false;
      botEyes.padX = botEyes.padY = 0;
      botEyes.blinkStart(now, BW_SWAP);
    }
  }

  // Idle fidgets every 12-25 s, never the same one twice running
  void fidget(unsigned long now) {
    uint8_t f;
    do { f = random(3); } while (f == lastFidget);
    lastFidget = f;
    nextFidget = now + (unsigned long)eyeRnd(12000, 25000);
    if (f == 0) {   // probe: one slow spike reaches out, and he looks along it
      float a = alphaHome() + eyeRnd(-50, 50) * DEG_TO_RAD;
      field(a, 0.6f, 800, now, false, true);
      look(cosf(a), sinf(a), 1200, now);
    } else if (f == 1) {   // shiver: a quick n = 7 ripple round the rim
      shiverAt = now;
    } else {   // scoot: a hop to a new spot in his corner, with a glance that way
      float x0, x1, y0, y1;
      box(corner, x0, x1, y0, y1);
      wTx = eyeRnd(min(x0, x1), max(x0, x1)); wTy = eyeRnd(min(y0, y1), max(y0, y1));
      L.v += 0.10f * tier().r / pkL; J.v -= 0.06f / pkJ;
      look(wTx > wx.x ? 0.9f : -0.9f, 0, 700, now);
      nextWander = now + (unsigned long)eyeRnd(3500, 8000);
    }
  }
  void look(float x, float y, unsigned long ms, unsigned long now) {
    botEyes.padX = constrain(x, -1.0f, 1.0f); botEyes.padY = constrain(y, -1.0f, 1.0f);
    padUntil = now + ms;
  }

  void stepRun(unsigned long now, float dt) {
    const PartyRun &m = *run;
    const float R = tier().r;
    if (!going) {
      if (!eyeReached(now, goAt)) return;
      going = true;
      fromX = wx.x; fromY = wy.x;   // he may have drifted during the lead-in
      corner = runTo; wTx = dstX; wTy = dstY;
      J.v += m.jGo / pkJ; L.v += m.lift * R / pkL;
      Q.snap(0); Q.f = m.f; Q.z = m.z;
      field(atan2f(dstY - fromY, dstX - fromX), m.reach, 220, now, true);   // reach toward where he's going
    }
    Q.step(1, dt);
    float p = constrain(Q.x, 0.0f, 1.03f), mx = (fromX + dstX) / 2, my = (fromY + dstY) / 2;
    float kx = mx + (LCD_WIDTH / 2.0f - mx) * 0.45f, ky = my + (LCD_HEIGHT / 2.0f - my) * 0.45f;
    float x = (1 - p) * (1 - p) * fromX + 2 * (1 - p) * p * kx + p * p * dstX;
    float y = (1 - p) * (1 - p) * fromY + 2 * (1 - p) * p * ky + p * p * dstY;
    // Move along the path but keep a velocity, so stretch and slosh see the motion
    wx.v = (x - wx.x) / dt; wy.v = (y - wy.x) / dt;
    wx.x = wx.t = x; wy.x = wy.t = y;
    if (Q.x > 0.97f) {   // landed: squash, spike burst, blink
      run = nullptr;
      J.v += m.jLand / pkJ;
      field(alphaHome(), m.land, 300, now);
      botEyes.blinkStart(now, BW_SWAP);
      botEyes.padX = botEyes.padY = 0;
      nextWander = now + 1200;
      nextRoam = now + (unsigned long)eyeRnd(PARTY_ROAM_MIN_MS, PARTY_ROAM_MAX_MS);
    }
  }

  // ---- Shape ----------------------------------------------------------------
  // Per-frame constants of the radius function
  float a3, a4, a5, w3, w4, w5, cPhi, sPhi, cBul, sBul, cDent, sDent;
  void prepShape(float R) {
    float A = (1 + 1.5f * E.x) * R * min(1.0f, 2 * constrain(P.x, 0.0f, 1.0f));   // no wobble while it's still the face's outline
    a3 = A * 0.045f * (0.6f + 0.4f * sinf(TWO_PI * 0.050f * t + ph[3]));   // edge waves n = 3, 4, 5, each
    a4 = A * 0.030f * (0.6f + 0.4f * sinf(TWO_PI * 0.071f * t + ph[4]));   // with a slow amplitude drift so
    a5 = A * 0.018f * (0.6f + 0.4f * sinf(TWO_PI * 0.037f * t + ph[5]));   // the pattern never repeats
    w3 = TWO_PI * 0.13f * t + ph[0]; w4 = -TWO_PI * 0.19f * t + ph[1]; w5 = TWO_PI * 0.29f * t + ph[2];
    cPhi = cosf(phiV); sPhi = sinf(phiV); cBul = cosf(bulgeA); sBul = sinf(bulgeA); cDent = cosf(thD); sDent = sinf(thD);
    float sp = (millis() - shiverAt) / 600.0f;   // shiver: n = 7 at 1.1 Hz under a 600 ms sin² envelope
    a7 = (shiverAt && sp < 1) ? 0.02f * R * sq(sinf(PI * sp)) : 0;
    w7 = TWO_PI * 1.1f * t;
    floorOn = morphOn() > 0.99f;
  }
  float a7, w7;
  bool floorOn;

  // Radius at angle th (c, s = its cos and sin), before the spike fan
  float radiusAt(float th, float c, float s, float R) const {
    return radiusWob(c, s, R, a3 * sinf(3 * th + w3) + a4 * sinf(4 * th + w4) + a5 * sinf(5 * th + w5));
  }
  float radiusWob(float c, float s, float R, float wob) const {
    float cv = c * cPhi + s * sPhi;
    float r = R * B * (1 + J.x * (2 * c * c - 1) + St.x * (2 * cv * cv - 1)) + wob;
    float cb = c * cBul + s * sBul;
    if (cb > 0) r += R * g.x * cb * cb * cb;
    // Poke dent: a soft dip about 0.6 rad each side of the finger (cos 0.6 = 0.825)
    float cd = c * cDent + s * sDent;
    if (cd > 0.825f && D.x < -0.05f) { float u = (cd - 0.825f) / 0.175f; r += D.x * u * u; }
    // Floor: the eyes reach 2.47 R on a mood pop, so the sides never pull in past 0.97 r
    // (mid-morph the drop is the stroke's flat ellipse, so no floor then)
    if (!floorOn) return r;
    float fl = fabsf(s) < 0.5736f ? 0.97f : 0.85f;   // within 35° of horizontal
    return max(r, fl * R);
  }

  // Radii for this frame (partyRR, with the unit vectors in partyC/partyS)
  // The edge waves step around the outline by rotation (sin(n(θ + 3°) + w) from
  // sin and cos at θ), so the whole outline costs 6 sinf/cosf, not 360.
  void buildOutline(float R) {
    float a0 = al.x, ca = cosf(a0), sa = sinf(a0);
    float ws[4], wc[4];
    const float amp[4] = {a3, a4, a5, a7}, w[4] = {w3, w4, w5, w7};
    const int harm[4] = {3, 4, 5, 7};
    int nh = a7 > 0 ? 4 : 3;
    for (int h = 0; h < nh; h++) { ws[h] = sinf(harm[h] * a0 + w[h]); wc[h] = cosf(harm[h] * a0 + w[h]); }
    for (int i = 0; i < PARTY_N; i++) {
      float c = ca * partyCos3[i] - sa * partySin3[i];
      float s = sa * partyCos3[i] + ca * partySin3[i];
      partyC[i] = c; partyS[i] = s;
      float wob = 0;
      for (int h = 0; h < nh; h++) {
        wob += amp[h] * ws[h];
        float s2 = ws[h] * partyStepC[h] + wc[h] * partyStepS[h];
        wc[h] = wc[h] * partyStepC[h] - ws[h] * partyStepS[h];
        ws[h] = s2;
      }
      partyRR[i] = radiusWob(c, s, R, wob);
    }
    // Spike fan: slots 6 vertices apart, centred on the attractor (vertex 0)
    for (int k = 0; k < PARTY_SPIKES; k++) {
      float hk = spk[k].x;
      if (fabsf(hk) < 0.05f) continue;
      int idx = ((k - 4) * 6 + PARTY_N) % PARTY_N;
      for (int d = -2; d <= 2; d++) partyRR[(idx + d + PARTY_N) % PARTY_N] += hk * PARTY_SPK_LUT[abs(d)];
    }
    // Porcupine: 12 spikes every 30°; the ones pointing at a screen edge stay short
    if (hP.x > 0.05f) {
      float h = alphaHome();
      for (int j = 0; j < 12; j++) {
        int idx = j * 10;
        float len = fabsf(wrapPi(a0 + idx * 3 * DEG_TO_RAD - h)) > 100 * DEG_TO_RAD ? 0.4f : 1.0f;
        for (int d = -2; d <= 2; d++) partyRR[(idx + d + PARTY_N) % PARTY_N] += hP.x * len * PARTY_SPK_LUT[abs(d)];
      }
    }
  }
  // The outline polygon around (cx, cy), grown outward by grow px
  void placeOutline(float cx, float cy, float grow) {
    for (int i = 0; i < PARTY_N; i++) {
      partyVX[i] = cx + (partyRR[i] + grow) * partyC[i];
      partyVY[i] = cy + (partyRR[i] + grow) * partyS[i];
    }
  }

  // ---- Render ---------------------------------------------------------------
  void render(unsigned long now) {
    if (gfx == nullptr) return;
    if (!active) enter(now);
    uint32_t t0 = micros(), ts = t0;
    auto stage = [&](int i) { uint32_t n = micros(); partyRenderStats.stageUs[i] += n - ts; ts = n; };
    step(now);
    stage(0);

    const PartyTier &T = tier();
    const float R = morphR(), on = morphOn();
    prepShape(R);
    uint16_t *buf = lcdCanvasPixels();   // null on an 8-bit canvas: no shadow or rim then

    // One integer centre for body, shadow and eyes, moved only past 0.6 px so nothing shimmers
    float fcx = wx.x, fcy = wy.x - L.x + J.x * R * on;   // planted bottom: squash keeps the base on the floor
    float st = (now - shakeAt) / 1000.0f;   // shake: a sideways jiggle at 5.7 Hz dying away
    if (shakeAt && st < 1.5f) fcx += 0.12f * R * expf(-5 * st) * sinf(TWO_PI * 5.7f * st);
    if (fabsf(fcx - icx) > 0.6f) icx = (int16_t)lroundf(fcx);
    if (fabsf(fcy - icy) > 0.6f) icy = (int16_t)lroundf(fcy);

    // Sheen colour: what the show looks like at the screen centre, low-passed
    if (buf) {
      uint16_t p = partySwap(buf[(LCD_HEIGHT / 2) * LCD_WIDTH + LCD_WIDTH / 2]);
      float target[3] = {(float)((p >> 11) & 31) * 8.2f, (float)((p >> 5) & 63) * 4.05f, (float)(p & 31) * 8.2f};
      for (int c = 0; c < 3; c++) sheen[c] += (target[c] - sheen[c]) * 0.15f;
    }

    buildOutline(R);
    stage(1);
    if (buf && P.x > 0.6f) drawShadow(buf, R);
    stage(2);
    if (buf) drawRimAndBody(buf);
    else {
      placeOutline(icx, icy, 0);
      partyFillPoly(partyVX, partyVY, PARTY_N, [&](int y, int xa, int xb) { gfx->drawFastHLine(xa, y, xb - xa + 1, BOT_COLOR_BG); });
    }
    stage(3);
    if (P.x > 0.8f) drawSheen(R);
    stage(4);

    // Eyes: the firmware model, no mouth, clamped inside the drop
    float k = morphK(), hw = 2.04f * 48 * k, hh = 1.22f * 48 * k;
    float ex = ox.x, ey = oy.x;
    float ax = max(1.0f, R * (1 + J.x) - hw - 3), ay = max(1.0f, R * (1 - J.x) - hh - 3), q = hypotf(ex / ax, ey / ay);
    if (q > 1 && on > 0.99f) { ex /= q; ey /= q; }
    if (fabsf(ex - iox) > 0.6f) iox = (int16_t)lroundf(ex);
    if (fabsf(ey - ioy) > 0.6f) ioy = (int16_t)lroundf(ey);
    botEyes.render(icx + iox, icy + ioy, k, botFaceColor, false, now, T.brows || P.x < 0.9f);
    stage(5);

    uint32_t us = micros() - t0;
    partyRenderStats.sumUs += us;
    partyRenderStats.count++;
    if (us > partyRenderStats.maxUs) partyRenderStats.maxUs = us;
  }

  // Rim and body in one pass. The rim outline is the body grown 1.5 px; each
  // rim row is split around the body's spans, so only the thin ring is read,
  // halved and written back, and the inside is cleared to black.
  // Rim colour: the show at half brightness, or a faint face-colour line on dark scenes.
  void drawRimAndBody(uint16_t *buf) {
    int ya, yb, yi0, yi1;
    placeOutline(icx, icy, 1.5f);
    partyScan(0, partyVX, partyVY, PARTY_N, ya, yb);
    placeOutline(icx, icy, 0);
    partyScan(1, partyVX, partyVY, PARTY_N, yi0, yi1);
    uint16_t fc = botFaceColor;
    uint16_t fr = (uint16_t)(((fc >> 11) & 31) * 0.22f), fg = (uint16_t)(((fc >> 5) & 63) * 0.22f), fb = (uint16_t)((fc & 31) * 0.22f);
    auto rim = [&](uint16_t *row, int a, int b) {
      for (int x = a; x <= b; x++) {
        uint16_t p = partySwap(row[x]);
        uint16_t r = max((uint16_t)(((p >> 11) & 31) >> 1), fr), gg = max((uint16_t)(((p >> 5) & 63) >> 1), fg), bb = max((uint16_t)((p & 31) >> 1), fb);
        row[x] = partySwap((r << 11) | (gg << 5) | bb);
      }
    };
    int16_t oa[PARTY_MAX_X / 2], ob[PARTY_MAX_X / 2], ia[PARTY_MAX_X / 2], ib[PARTY_MAX_X / 2];
    for (int y = ya; y <= yb; y++) {
      uint16_t *row = buf + y * LCD_WIDTH;
      int no = partySpans(0, y, oa, ob);
      int ni = (y >= yi0 && y <= yi1) ? partySpans(1, y, ia, ib) : 0;
      for (int s = 0; s < no; s++) {
        int cur = oa[s];
        for (int j = 0; j < ni; j++) {
          int c = max((int)ia[j], (int)oa[s]), d = min((int)ib[j], (int)ob[s]);
          if (d < c) continue;
          if (c > cur) rim(row, cur, c - 1);
          memset(row + c, 0, (d - c + 1) * 2);
          cur = d + 1;
        }
        if (cur <= ob[s]) rim(row, cur, ob[s]);
      }
    }
  }

  // Floor shadow: two darkening tones with a screen-anchored dither seam. It
  // follows the wander but not the hop, and shrinks while he's in the air.
  void drawShadow(uint16_t *buf, float R) {
    float hN = constrain(L.x / (0.25f * R), 0.0f, 1.0f), sh = 1 - 0.45f * hN;
    float ecx = wx.x + 0.08f * R, ecy = wy.x + 0.95f * R, a = 0.90f * R * sh, b = 0.30f * R * sh;
    int ya = max(0, (int)floorf(ecy - b)), yb = min(LCD_HEIGHT - 1, (int)ceilf(ecy + b));
    for (int y = ya; y <= yb; y++) {
      float v = (y + 0.5f - ecy) / b;
      if (v * v >= 1) continue;
      float hw = a * sqrtf(1 - v * v);
      float vc = (y + 0.5f - ecy) / (0.6f * b), cw = vc * vc < 1 ? 0.7f * a * sqrtf(1 - vc * vc) : -1;
      float vs = (y + 0.5f - ecy) / (0.68f * b), sw = vs * vs < 1 ? 0.79f * a * sqrtf(1 - vs * vs) : -1;
      // Pixel x is inside a band of half-width w when |x + 0.5 - ecx| < w
      auto lo = [&](float w) { return w < 0 ? LCD_WIDTH : max(0, (int)floorf(ecx - w - 0.5f) + 1); };
      auto hi = [&](float w) { return w < 0 ? -1 : min(LCD_WIDTH - 1, (int)ceilf(ecx + w - 0.5f) - 1); };
      bool lifted = hN > 0.6f;   // a lifted drop casts only the lighter tone
      int xa = lo(hw), xb = hi(hw), sa = lifted ? LCD_WIDTH : lo(sw), sb = lifted ? -1 : hi(sw), ca = lifted ? LCD_WIDTH : lo(cw), cb = lifted ? -1 : hi(cw);
      uint16_t *row = buf + y * LCD_WIDTH;
      for (int x = xa; x <= xb; x++) {
        uint16_t p = partySwap(row[x]);
        if (x >= ca && x <= cb) p = partyDark50(p);
        else if (x >= sa && x <= sb) p = ((x + y) & 1) ? partyDark50(p) : partyDark75(p);
        else p = partyDark75(p);
        row[x] = partySwap(p);
      }
    }
  }

  // Sheen: one flat crescent inside the upper-left rim (200°-250°), in the show's
  // colour at 45 %, never darker than 18 % face colour. It rides every wobble.
  void drawSheen(float R) {
    uint16_t fc = botFaceColor;
    float minC[3] = {((fc >> 11) & 31) * 8.2f * 0.18f, ((fc >> 5) & 63) * 4.05f * 0.18f, (fc & 31) * 8.2f * 0.18f};
    uint8_t r8 = (uint8_t)max(sheen[0] * 0.45f, minC[0]), g8 = (uint8_t)max(sheen[1] * 0.45f, minC[1]), b8 = (uint8_t)max(sheen[2] * 0.45f, minC[2]);
    uint16_t col = ((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3);
    float sx[24], sy[24];
    for (int i = 0; i < 12; i++) {
      float u = i / 11.0f, th = (200 + 50 * u) * DEG_TO_RAD, c = cosf(th), s = sinf(th);
      float r = radiusAt(th, c, s, R) - 2, w = 0.08f * R * sinf(PI * u);
      sx[i] = icx + r * c; sy[i] = icy + r * s;
      sx[23 - i] = icx + (r - w) * c; sy[23 - i] = icy + (r - w) * s;
    }
    partyFillPoly(sx, sy, 24, [&](int y, int xa, int xb) { gfx->drawFastHLine(xa, y, xb - xa + 1, col); });
    if (fabsf(St.x) < 0.05f && E.x < 0.6f) {   // specular dot, hidden in fast motion
      float th = 225 * DEG_TO_RAD, c = cosf(th), s = sinf(th), r = radiusAt(th, c, s, R) - 2 - 0.04f * R;
      gfx->fillRect((int)lroundf(icx + r * c) - 1, (int)lroundf(icy + r * s) - 1, 3, 2, 0xBDF7);
    }
  }
};

PartyBlob partyBlob;

// Party mode only takes effect on the ambient scene
static inline bool partyWanted() { return partyMode && botBackgroundStyle == 4; }

static inline const char *partyCornerName(uint8_t c) {
  static const char *names[4] = {"tr", "br", "bl", "tl"};
  return names[c & 3];
}

#endif // PARTY_BLOB_H
