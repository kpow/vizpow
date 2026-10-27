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
// inside. Plan: docs/plan-party-mode.md. Round 2 (tentacles, droplets, a
// jigglier body): docs/party2-motion-spec.md and docs/party2-art-direction.md,
// with the values Kevin tuned in the Tentacle Lab (spec §6a).
//
// Party mode is a flag on the ambient scene (botBackgroundStyle 4), not a
// style of its own. He picks his own corner; there is no corner setting.
//
// Turning it on, he blinks and hops from the middle of the screen into his
// corner as the outline around his eyes shrinks into the drop, then stretches
// two tentacles toward the middle; turning it off runs that backwards. Now and
// then a tentacle feels its way out (often a big one), or a flick throws a
// droplet that flows back in. Shake him for spikes all round and a splash of
// droplets; tap the show and a tentacle reaches for the finger; tap the blob
// and he flinches, peeks, and slingshots to another corner. On the CoreS3 /
// Stackchan tentacles drum the beat; the 1.69 has no mic, so it sends itself
// a pulse.
//
// The body is a 120-point polygon whose radius is a function of angle: surface
// modes n = 2..5 on underdamped springs (the jiggle), edge waves, a spike fan,
// and a mound where each tentacle roots. Tentacles are 10-node spring chains
// that chase a pose curve; droplets are small ellipses with a bridge back to
// the rim. Everything is filled as one union (non-zero winding), so overlaps
// read as one liquid. Every value moves on a spring stepped with real time.
// No per-frame heap.
// ============================================================================

extern GfxDevice *gfx;
extern uint8_t botBackgroundStyle;
extern uint16_t botFaceColor;
uint16_t* lcdCanvasPixels();

#define PARTY_N          120     // outline vertices, 3° apart
#define PARTY_SPIKES     9       // spike fan slots, 18° (6 vertices) apart
#define PARTY_EDGE_GAP   2       // spike envelope / shadow to the screen edge, px
#define PARTY_MORPH_R    78.5f   // drop radius at the start of the morph: with squash 0.31 it is the
#define PARTY_MORPH_J    0.31f   // 103 x 54 px ellipse of the ambient scene's 5 px stroke around the eyes

// Overlays move out of his way (layout spec §3)
#ifdef BOARD_ESP32S3_LCD_169
  #define PARTY_CLOCK_INSET 22   // clock pill x from the side edge, clear of the rounded corner
  #define PARTY_GLASS_R     40   // the panel's rounded corners: tentacles and droplets stay inside
#else
  #define PARTY_CLOCK_INSET 8
  #define PARTY_GLASS_R     0
#endif
#define PARTY_TOAST_LOW_Y (LCD_HEIGHT - 44)   // toast slot while he's up top (236 on the 1.69: clears the swipe handle)

// Round 2 tier per board (motion spec §1.3): A 5 tentacles, 6 droplets, modes n = 2..5;
// B 3 tentacles, 2 droplets, n = 2..4; C the v3.8.0 body with the jiggle only.
#ifndef PARTY_TIER
  #ifdef BOARD_ESP32S3_LCD_169
    #define PARTY_TIER 0
  #else
    #define PARTY_TIER 1
  #endif
#endif
#if PARTY_TIER == 0
  #define PARTY_TENTS 5
  #define PARTY_DROPS 6
  #define PARTY_MODES 4
#elif PARTY_TIER == 1
  #define PARTY_TENTS 3
  #define PARTY_DROPS 2
  #define PARTY_MODES 3
#else
  #define PARTY_TENTS 0
  #define PARTY_DROPS 0
  #define PARTY_MODES 3
#endif

// Kevin's Tentacle Lab tune (motion spec §6a)
#define PARTY_REACH  1.20f   // tentacle length
#define PARTY_JIGGLE 1.70f   // surface-mode kicks
#define PARTY_CHUNK  1.30f   // tentacle width
#define PARTY_BULB   1.30f   // tip bulb
#define PARTY_BIG    0.60f   // share of random reaches that go big

bool    partyMode = false;       // NVS "party"
uint8_t partySize = 1;           // NVS "pSize": 0 S, 1 M, 2 L
volatile uint8_t partyHeroReq = 0;   // GET /bot/party?hero= sets it, the render loop plays it

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
// stageUs: summed per stage (step, outline, shadows, rim + body, sheen, eyes, code backdrop) to find the cost
#define PARTY_STAGES 7
struct PartyRenderStats { uint32_t sumUs, count, maxUs; uint32_t stageUs[PARTY_STAGES]; uint8_t tentsMax; };
PartyRenderStats partyRenderStats = {0, 0, 0, {0}, 0};
uint8_t partyTentCap = PARTY_TENTS;   // the cost guard drops it to 3 while frames run long

String partyRenderStatsJson() {
  PartyRenderStats &st = partyRenderStats;
  String json = "{\"avgUs\":" + String(st.count ? st.sumUs / st.count : 0) +
                ",\"maxUs\":" + String(st.maxUs) + ",\"renders\":" + String(st.count) +
                ",\"tentsMax\":" + String(st.tentsMax) + ",\"tentCap\":" + String(partyTentCap) + ",\"stagesUs\":[";
  for (int i = 0; i < PARTY_STAGES; i++) json += String(i ? "," : "") + String(st.count ? st.stageUs[i] / st.count : 0);
  json += "]}";
  st.sumUs = st.count = st.maxUs = 0;
  st.tentsMax = 0;
  for (int i = 0; i < PARTY_STAGES; i++) st.stageUs[i] = 0;
  return json;
}

// ---- RGB565 helpers. The canvas stores pixels byte-swapped. ----------------
static inline uint16_t partySwap(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t partyDark75(uint16_t p) { return p - ((p >> 2) & 0x39E7); }
static inline uint16_t partyDark50(uint16_t p) { return (p >> 1) & 0x7BEF; }

// ---- Scanline fill ----------------------------------------------------------
// Any number of polygons go into one edge list and fill as their union
// (non-zero winding), so a tentacle over the body, or one folded over itself,
// stays one solid shape. Edges are bucketed by first row; a row walks only the
// edges that span it and sorts their crossings (x in 1/16 px).
#define PARTY_MAX_EDGES 420      // body 120 + 5 tentacles x 27 + 6 droplets x 18
#define PARTY_MAX_XR    48       // crossings per row
#define PARTY_MAX_SPANS (PARTY_MAX_XR / 2)

struct PartyEdge { float x, dx; int16_t y0, y1; int8_t dir; };

struct PartyRaster {
  PartyEdge e[PARTY_MAX_EDGES];
  uint16_t ord[PARTY_MAX_EDGES], act[PARTY_MAX_XR];
  uint16_t n, nAct, next;
  int16_t ya, yb;

  void clear() { n = 0; ya = LCD_HEIGHT; yb = -1; }
  // Edges of polygon (vx, vy) shifted by (ox, oy); row y is crossed at its centre y + 0.5.
  // Every polygon counts as winding the same way, whatever order its points came
  // in, so overlaps add up instead of cancelling into a see-through hole.
  void add(const float *vx, const float *vy, int cnt, float ox = 0, float oy = 0) {
    float area = 0;
    for (int i = 0, j = cnt - 1; i < cnt; j = i++) area += (vx[j] - vx[i]) * (vy[j] + vy[i]);
    int8_t sense = area < 0 ? -1 : 1;
    for (int i = 0, j = cnt - 1; i < cnt; j = i++) {
      float x0 = vx[j] + ox, y0 = vy[j] + oy, x1 = vx[i] + ox, y1 = vy[i] + oy;
      int8_t dir = sense;
      if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -sense; }
      int r0 = max(0, (int)ceilf(y0 - 0.5f)), r1 = min(LCD_HEIGHT - 1, (int)ceilf(y1 - 0.5f) - 1);   // y0 <= y+.5 < y1
      if (r1 < r0 || n >= PARTY_MAX_EDGES) continue;
      float dxdy = (x1 - x0) / (y1 - y0);
      PartyEdge &E = e[n++];
      E.x = x0 + (r0 + 0.5f - y0) * dxdy; E.dx = dxdy; E.y0 = r0; E.y1 = r1; E.dir = dir;
      if (r0 < ya) ya = r0;
      if (r1 > yb) yb = r1;
    }
  }
  // Order the edges by first row; then call spans() with rising y
  void begin() {
    static uint16_t cnt[LCD_HEIGHT + 1];
    memset(cnt, 0, sizeof(cnt));
    for (int i = 0; i < n; i++) cnt[e[i].y0 + 1]++;
    for (int y = 1; y <= LCD_HEIGHT; y++) cnt[y] += cnt[y - 1];
    for (int i = 0; i < n; i++) ord[cnt[e[i].y0]++] = i;
    nAct = next = 0;
  }
  // Row y as sorted, screen-clipped pixel spans; returns the span count
  int spans(int y, int16_t *xa, int16_t *xb) {
    while (next < n && e[ord[next]].y0 <= y) {
      uint16_t k = ord[next++];
      if (e[k].y1 >= y && nAct < PARTY_MAX_XR) act[nAct++] = k;
    }
    int16_t xs[PARTY_MAX_XR];
    int8_t ds[PARTY_MAX_XR];
    int m = 0;
    for (int i = 0; i < nAct;) {
      const PartyEdge &E = e[act[i]];
      if (E.y1 < y) { act[i] = act[--nAct]; continue; }
      int16_t x = (int16_t)lroundf((E.x + (y - E.y0) * E.dx) * 16);
      int j = m++ - 1;
      while (j >= 0 && xs[j] > x) { xs[j + 1] = xs[j]; ds[j + 1] = ds[j]; j--; }
      xs[j + 1] = x; ds[j + 1] = E.dir;
      i++;
    }
    int k = 0, w = 0, x0 = 0;
    for (int i = 0; i < m; i++) {
      int p = w;
      w += ds[i];
      if (p == 0 && w != 0) x0 = xs[i];
      else if (p != 0 && w == 0 && k < PARTY_MAX_SPANS) {
        int a = max(0, (x0 - 8 + 15) >> 4), b = min(LCD_WIDTH - 1, (xs[i] - 8) >> 4);   // ceil / floor of x - 0.5
        if (b >= a) { xa[k] = a; xb[k] = b; k++; }
      }
    }
    return k;
  }
};
static PartyRaster partyRA, partyRB;

// One polygon: span(y, xa, xb) for every inside run, clipped to the screen
template <typename Span>
static void partyFillPoly(const float *vx, const float *vy, int n, Span span) {
  PartyRaster &R = partyRA;
  R.clear();
  R.add(vx, vy, n);
  R.begin();
  int16_t xa[PARTY_MAX_SPANS], xb[PARTY_MAX_SPANS];
  for (int y = R.ya; y <= R.yb; y++) {
    int k = R.spans(y, xa, xb);
    for (int i = 0; i < k; i++) span(y, xa[i], xb[i]);
  }
}

static float partyVX[PARTY_N], partyVY[PARTY_N], partyRR[PARTY_N], partyC[PARTY_N], partyS[PARTY_N];
static float partyTT[PARTY_N];   // sideways offset per vertex: a pattern spike's lean
static float partyCos3[PARTY_N], partySin3[PARTY_N];   // cos/sin(i x 3°)
static float partyStepC[4], partyStepS[4];             // cos/sin(n x 3°) for the n = 3, 4, 5 edge waves and the n = 7 shiver
static float partyModeStepC[4], partyModeStepS[4];     // cos/sin(n x 3°) for the n = 2..5 surface modes
static const float PARTY_SPK_LUT[3] = {1.0f, 0.41f, 0.09f};   // (1 - j/3)^2.2: sharp tip, concave flanks

// Surface modes n = 2..5 (motion spec §3.1): frequency, damping, clamp. Mode 2 is
// the squash and the velocity stretch, so a run that stops overshoots into a squash.
struct PartyModeDef { float f, z, cl; };
static const PartyModeDef PARTY_MODE_DEF[4] = {{2.0f, 0.22f, 0.28f}, {3.0f, 0.25f, 0.14f}, {3.8f, 0.28f, 0.09f}, {4.5f, 0.32f, 0.06f}};

// ---- Tentacles, droplets, timed events -------------------------------------
#define PARTY_NN 10                         // nodes per tentacle; 0 and 1 ride the body
#define PARTY_TP (2 * (PARTY_NN - 1) + 9)   // tentacle polygon: both sides and a 9-point bulb
enum { PT_REACH, PT_FEELER, PT_FLICK, PT_STREAMER, PT_GRAB, PT_PEEK, PT_BEAT, PT_SEQ };   // PT_SEQ: owned by a pattern, never reused
enum { PT_GROW, PT_HOLD, PT_RETRACT };

struct PartyTentOpt {
  float dir = 0;              // direction from the body centre, or
  bool track = false;         // grow toward a point (tx, ty)
  float tx = 0, ty = 0;
  float L = 0;                // length past the rim
  uint8_t kind = PT_REACH;
  int32_t hold = 1500;        // ms after it arrives; -1 until retracted
  float gf = 1.6f, gz = 0.5f; // grow spring
  bool soft = false;          // floppy (streamers)
  bool free = false;          // may leave the 70° cone into the show
  float curl = 0.3f, swA = 0.25f, swF = 0.45f;
  uint8_t boops = 1;          // bulb pulses after it arrives
  bool mirror = false;        // sway mirrors with side (a symmetric pair)
  float side = 0;             // ±1, or 0 to take it from the angle
  float ph = -1;              // sway phase, or < 0 for random (a pair shares one)
};

struct PartyTent {
  bool on, track, free, soft, pinching, pinchArmed, mirror;
  uint8_t kind, phase, boops;
  uint16_t gen;
  float tx, ty, dirA, L, Lc, side, curlT, swA, swF, swPh, bulbR, pinchV, area;
  int32_t holdMs;
  unsigned long holdUntil, pinchAt, bulbless;
  BotSpring lam, th, aim, curl, bulb, neck;
  float nx[PARTY_NN], ny[PARTY_NN], vx[PARTY_NN], vy[PARTY_NN];
  float hw[PARTY_NN], ux[PARTY_NN], uy[PARTY_NN];   // half-width and unit tangent, once per frame
};

struct PartyDrop {
  bool on, sat, bridge;
  uint8_t phase;              // 0 flying, 1 flowing back, 2 being absorbed
  float x, y, vx, vy, r, th, gap, rim;
  unsigned long t0, flyMs;
  BotSpring rx, ry, rs;
};

enum { PE_BOOP, PE_KICK2, PE_LOOK, PE_GRAB, PE_PEEK, PE_REACH, PE_FEELER, PE_WAKE, PE_WAKE2, PE_WAKE_END,
       PE_DROP, PE_RETRACT, PE_BEAT, PE_BEAT_PINCH, PE_FEELER_GO };
struct PartyEv { unsigned long at; uint8_t kind; int8_t ref; uint16_t gen; float a, b, c; };
#define PARTY_EVQ 20

static float partyTX[PARTY_TP], partyTY[PARTY_TP];       // one tentacle polygon at a time
static float partyDX[12], partyDY[12], partyBX[6], partyBY[6];
static float partyCos30[12], partySin30[12];

// Two ways to change corner, both a slingshot (hero 6.4): a tentacle grabs a
// point along the path and he pulls himself after it. A poke is a startled
// dash: a short crouch, a fast spring, big kicks, two streamers trailing. A
// roam is his own idea: a glance at where he's going, then a stroll.
struct PartyRun { uint16_t leadMs; float f, z, jGo, lift, bow, jLand, land, gf, gz; uint8_t streamers; };
static const PartyRun PARTY_POKE = {260, 1.5f, 0.85f, -0.14f, 0.12f, 0.45f, 0.30f, 0.7f, 3.0f, 0.6f, 2};
static const PartyRun PARTY_ROAM = {700, 0.95f, 0.9f, -0.08f, 0.07f, 0.30f, 0.16f, 0.45f, 1.6f, 0.55f, 0};
static const PartyRun PARTY_CODE = {160, 2.2f, 0.85f, -0.10f, 0.06f, 0.15f, 0.16f, 0.5f, 3.0f, 0.6f, 2};   // into code mode and back
#define PARTY_ROAM_MIN_MS 25000   // he wanders to another corner every 25-60 s
#define PARTY_ROAM_MAX_MS 60000

// ---- Pattern library (docs/party2-pattern-library.md) ------------------------
// Symmetric ferrofluid sequences in three sizes, mirrored about the line from
// the blob to the screen centre so they read as a magnet, not a fidget. Every
// running pattern owns a bank of spike slots, so up to three can stack and their
// spikes add up. A scheduler picks them; GET /bot/party?seq= plays one on demand.
#define PARTY_BANKS 3
#define PARTY_SLOTS 9
#define PARTY_SEQS  3
struct PartySlot { bool on, popped; int8_t off, k; uint8_t wide; float env, Fon, Foff, hT, lean; BotSpring h; };
struct PartyBank { int8_t owner; float rf, rz, ff, fz; PartySlot s[PARTY_SLOTS]; };
static const float PARTY_LUT7[4] = {1.0f, 0.53f, 0.22f, 0.05f};   // (1 - |d|/4)^2.2: the 7-wide spike

// SEQ_COMPILE and on belong to code mode: the party scheduler never picks them (?seq= still plays them)
enum { SEQ_BREATH, SEQ_CALM, SEQ_CROWN, SEQ_CAROUSEL, SEQ_SPLIT, SEQ_WAVE, SEQ_MERGE, SEQ_BLOOM, SEQ_FEELERS, SEQ_LONG,
       SEQ_COMPILE, SEQ_AHA, SEQ_GRAND, SEQ_VOICE, SEQ_COUNT };
#define SEQ_PARTY_COUNT SEQ_COMPILE
enum { TIER_S, TIER_M, TIER_B };
struct PartySeqDef { const char *key; uint8_t tier, w; uint16_t cdS; bool spikes, tents, solo; };
static const PartySeqDef PARTY_SEQ_DEF[SEQ_COUNT] = {
  {"breath",   TIER_S, 17, 12, true,  false, false},   // Breathing Crown: five spikes breathe with him
  {"calm",     TIER_S,  8, 20, false, false, true },   // Calm Dome: field off, a glossy dome, one wobble (never stacks)
  {"crown",    TIER_M,  9, 30, true,  false, false},   // Rosensweig Crown: spikes nucleate centre-out, tremble, slump
  {"carousel", TIER_M,  7, 35, true,  false, false},   // Carousel: a ring of leaning spikes circles and reverses
  {"split",    TIER_M,  7, 30, true,  false, false},   // Peak Split: one peak splits into two, then three
  {"wave",     TIER_M,  7, 25, true,  false, false},   // Stadium Wave: a crest of spikes runs across and back
  {"merge",    TIER_M,  7, 30, true,  false, false},   // Spike Merge: nine small spikes merge into five, then three tall
  {"bloom",    TIER_M,  7, 30, false, true,  false},   // Bloom: a mirrored pair of arms around a small spike
  {"feelers",  TIER_M,  5, 20, false, true,  false},   // Feelers: a short group fanned across the cone
  {"long",     TIER_B,  3, 45, false, true,  false},   // Long Reach: the big feeler, now rare
  {"compile",  TIER_M,  0, 14, true,  false, false},   // Compile: a progress bar of spikes, outside in, centre last
  {"aha",      TIER_M,  0, 18, false, true,  false},   // Aha: one short finger raised up the axis
  {"grand",    TIER_B,  0, 45, true,  false, false},   // Grand Crown: the whole fan, for "found it"
  {"voice",    TIER_S,  0,  0, true,  false, true },   // Voice crown: five spikes ride a quip's speech
};
struct PartySeq {
  bool on, lead, driveAl;
  uint8_t id, bank, v;
  int8_t dir;
  unsigned long t0;
  float axis, g, kT, phi, hold;
  uint32_t f;                   // one-shot flags
  int8_t ti[3]; uint16_t tg[3]; uint8_t nt;   // its tentacles (slot, generation)
};
struct PartyHist { unsigned long t; uint8_t tier; bool tents; };
static const uint8_t PARTY_STACK_MOST[3] = {1, 2, 3};          // off, some, wild
static const float PARTY_STACK_CHANCE[3] = {0, 0.4f, 0.75f};   // chance to add a layer a beat after one starts
uint8_t partyStack = 1;              // GET /bot/party?stack=off|some|wild
volatile uint8_t partySeqReq = 0;    // GET /bot/party?seq= sets id + 1, the render loop plays it

// ---- Code mode "Ink Reader" (docs/vizself/code-mode-motion-spec.md) ---------
// While vizBot looks through his own code (vizbrain's vizself connector), he is
// a big ink drop in the middle of the screen, reading a dim scrolling code
// minimap with two mirrored tentacles while patterns stack on his head. Quips
// make the voice the magnet; "found it" is a full-reach grab and a Grand Crown.
// It is a flag on the party blob: with party mode off the face rounds into the
// drop where it is, with it on he flies in from his corner and back after.
#ifdef BOARD_ESP32S3_LCD_169
  #define CODE_CX 120
  #define CODE_CY 150
#else
  #define CODE_CX 160
  #define CODE_CY 118
#endif
static const PartyTier CODE_TIER = {0.55f, 66, 79, 82, 0, 0, true};
#define CODE_MORPH_F    2.0f     // enter / exit morph spring (party's is 1.4 Hz)
#define CODE_KAPPA      0.85f    // edge waves a bit calmer than party, so the patterns read
#define CODE_ROWS       24       // backdrop ring (21 rows on screen)
#define CODE_ROW_PITCH  12
#define CODE_BAR_H      6
#define CODE_SCROLL     8.0f     // px/s, x (1 + 0.4 effort)
#define CODE_READ_ROOT  (20 * DEG_TO_RAD)
#define CODE_READ_L0    0.4f     // reader tip, past the rim, x r: start and end of a line
#define CODE_READ_L1    1.3f
#define CODE_READ_LINE_MS 1400
#define CODE_READ_CR_MS 260
#define CODE_TIMEOUT_MS 75000    // no signal for this long: shrug and leave

enum { CP_OFF, CP_ENTER, CP_LOOP, CP_QUIP, CP_FOUND, CP_SHRUG };
enum { CR_NONE, CR_START, CR_FOUND, CR_FOUND_SMALL, CR_SHRUG, CR_CANCEL };
enum { CH_NONE, CH_QUIP, CH_FOUND, CH_SHRUG };
enum { RD_READ, RD_SKIM, RD_STARE };
enum { CL_NONE, CL_SCAN, CL_QUIP, CL_FOUND, CL_SHRUG };   // LED beats

// GET /bot/code sets these; the render loop acts on them
volatile uint8_t codeReq = CR_NONE;
volatile uint8_t codeHeroReq = CH_NONE;
volatile uint8_t codeSeqReq = 0;        // id + 1
volatile uint16_t codeQuipReq = 0;      // a quip is coming: about this many ms of audio
volatile uint8_t codeQuipExpr = 0xFF;
volatile uint32_t codeSignalMs = 0;     // the last /bot/code call
uint8_t codeStack = 1;                  // ?stack=off|some|wild
String codeWhy;                         // ?why= (shown in /state)

// The code playlist (spec §2.2): weights and cool-downs (s) for code mode
struct CodeSeqDef { uint8_t id, w, cdS; };
static const CodeSeqDef CODE_SEQ_DEF[] = {
  {SEQ_COMPILE, 16, 14}, {SEQ_CAROUSEL, 14, 14}, {SEQ_BREATH, 14, 5}, {SEQ_CROWN, 12, 12}, {SEQ_WAVE, 12, 10},
  {SEQ_MERGE, 10, 12}, {SEQ_SPLIT, 8, 14}, {SEQ_AHA, 8, 18}, {SEQ_CALM, 6, 20},
};
#define CODE_SEQS (sizeof(CODE_SEQ_DEF) / sizeof(CODE_SEQ_DEF[0]))

// The code minimap behind him: rows of rounded bars in four dim syntax hues,
// tinted toward the face colour, scrolling up. The row under a reader glows.
struct CodeRow { uint8_t n; int16_t x[10]; uint8_t w[10], hue[10]; float hl; };
struct CodeBackdrop {
  CodeRow rows[CODE_ROWS];
  int32_t made;       // rows generated (absolute index)
  float scroll;       // px since the start
  float alpha;        // fade in / out
  int8_t indent[2];   // two panes side by side, one per reader
  int32_t foundRow;   // -1, or the row that lit up ("found it")
  float fx0, fx1;     // the found line's ends (the reader tips)

  void reset() {
    made = 0; scroll = 0; indent[0] = indent[1] = 1; foundRow = -1;
    for (int i = 0; i < CODE_ROWS; i++) rows[i].hl = 0;
  }
  int32_t rowAt(float y) const { return (int32_t)floorf((y + scroll - 4) / CODE_ROW_PITCH); }
  float rowY(int32_t i) const { return 4 + i * CODE_ROW_PITCH - scroll; }   // top of the row's bars
  CodeRow &row(int32_t i) { make(i); return rows[i % CODE_ROWS]; }
  void make(int32_t upTo) {
    for (; made <= upTo; made++) {
      CodeRow &r = rows[made % CODE_ROWS];
      r.hl = 0; r.n = 0;
      for (int pane = 0; pane < 2; pane++) {
        int8_t &ind = indent[pane];
        int16_t x0 = pane ? LCD_WIDTH / 2 + 6 : 8, x1 = pane ? LCD_WIDTH - 8 : LCD_WIDTH / 2 - 6;
        if (random(100) < 15) { ind = constrain(ind + (random(2) ? -1 : 0), 0, 3); continue; }   // a blank line
        int16_t x = x0 + ind * 12;
        int n = 1 + random(4);
        for (int k = 0; k < n && x < x1 - 8 && r.n < 10; k++) {
          uint8_t w = min((int)(8 + random(37)), x1 - x);
          r.x[r.n] = x; r.w[r.n] = w;
          r.hue[r.n] = k == 0 ? (random(3) ? 0 : 3) : (uint8_t)(1 + random(3));   // keyword or comment first
          r.n++;
          x += w + 5;
        }
        int u = random(10);
        ind = constrain(ind + (u < 2 ? 1 : u < 4 ? -1 : 0), 0, 3);
      }
    }
  }
  void step(float dt, float speed, bool frozen, bool on) {
    alpha = constrain(alpha + (on ? dt / 0.45f : -dt / 0.30f), 0.0f, 1.0f);
    if (!frozen) scroll += speed * dt;
    for (int i = 0; i < CODE_ROWS; i++) rows[i].hl = max(0.0f, rows[i].hl - dt / 0.4f);
  }
  // Keyword, string, name, comment: mixed a third of the way to the face colour, at 18-28 % luma
  static uint16_t hue565(int h, uint16_t face, float gain) {
    static const uint8_t base[4][3] = {{190, 120, 255}, {120, 220, 140}, {0, 0, 0}, {150, 150, 160}};
    static const float luma[4] = {0.26f, 0.24f, 0.28f, 0.18f};
    float fr = ((face >> 11) & 31) * 8.2f, fg = ((face >> 5) & 63) * 4.05f, fb = (face & 31) * 8.2f;
    float r = h == 2 ? fr : base[h][0] + (fr - base[h][0]) * 0.33f;
    float g = h == 2 ? fg : base[h][1] + (fg - base[h][1]) * 0.33f;
    float b = h == 2 ? fb : base[h][2] + (fb - base[h][2]) * 0.33f;
    float l = max(1.0f, 0.3f * r + 0.59f * g + 0.11f * b), k = luma[h] * 255 / l * gain;
    uint8_t r8 = (uint8_t)min(255.0f, r * k), g8 = (uint8_t)min(255.0f, g * k), b8 = (uint8_t)min(255.0f, b * k);
    return ((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3);
  }
  // Straight into the canvas (PSRAM): a fillRect per bar costs more than its pixels
  static void bar(uint16_t *buf, int x, int y, int w, uint16_t c) {
    int x0 = max(0, x), x1 = min((int)LCD_WIDTH, x + w);
    if (x1 <= x0) return;
    uint16_t v = partySwap(c);
    for (int yy = max(0, y); yy < min((int)LCD_HEIGHT, y + CODE_BAR_H); yy++) {
      uint16_t *p = buf + yy * LCD_WIDTH + x0;
      for (int i = x1 - x0; i > 0; i--) *p++ = v;
    }
  }
  void draw(uint16_t face) {
    if (alpha <= 0) return;
    uint16_t *buf = lcdCanvasPixels();
    auto put = [&](int x, int y, int w, uint16_t c) { if (buf) bar(buf, x, y, w, c); else gfx->fillRect(x, y, w, CODE_BAR_H, c); };
    uint16_t col[4];
    for (int h = 0; h < 4; h++) col[h] = hue565(h, face, alpha);
    int32_t i0 = rowAt(0), i1 = rowAt(LCD_HEIGHT) + 1;
    make(i1);
    for (int32_t i = max((int32_t)0, i0); i <= i1; i++) {
      CodeRow &r = rows[i % CODE_ROWS];
      int y = (int)lroundf(rowY(i));
      if (y <= -CODE_BAR_H || y >= LCD_HEIGHT) continue;
      if (i == foundRow) {   // face colour at 60 %, between the reader tips
        int a = (int)lroundf(min(fx0, fx1)), b = (int)lroundf(max(fx0, fx1));
        if (b > a) put(a, y, b - a, hue565(2, face, 2.1f * alpha));
        continue;
      }
      bool lit = r.hl > 0.01f;
      for (int k = 0; k < r.n; k++) put(r.x[k], y, r.w[k], lit ? hue565(r.hue[k], face, alpha * (1 + 0.8f * r.hl)) : col[r.hue[k]]);
    }
  }
};
static CodeBackdrop codeBackdrop;

struct PartyBlob {
  // Body
  BotSpring wx, wy;          // centre (wander), px
  BotSpring L;               // hop lift px (+ up)
  BotSpring mc[PARTY_MODES], ms[PARTY_MODES];   // surface modes n = 2.., cos and sin parts
  BotSpring al, F;           // spike attractor angle, field strength 0..1
  BotSpring spk[PARTY_SPIKES];
  BotSpring g, E;            // bulge toward gaze / attractor, agitation 0..1
  BotSpring ox, oy;          // eye offset inside the drop (trails the body)
  BotSpring D;               // poke dent depth, px (<= 0)
  BotSpring Q;               // corner run progress 0..1
  BotSpring P;               // enter / exit morph: 0 = the face, 1 = the drop in its corner
  BotSpring hP;              // porcupine spikes all round (shake), px
  BotSpring beatSm;          // smoothed beat envelope
  float pkL, pkD, pkE, pkBulb, pkM[PARTY_MODES];   // kick normalisers

  bool active, ready, haveCorner;
  uint8_t corner;
  unsigned long lastMs, fieldUntil, evAt, nextWander, nextPulse, nextFeeler, nextBright, lastBeatGrab;
  float fieldT, alT, phiV, bulgeA, B, t, Reff, borrow;
  float wTx, wTy;
  float jit[PARTY_SPIKES], ph[6];
  int16_t icx, icy, iox, ioy;   // shared integer centre and eye offset (0.6 px hysteresis)
  float sheen[3];               // low-passed show colour, RGB888
  float thD, tapT, shakeT;      // dent angle; tap and shake agitation, decay
  bool haveBright;              // a bright spot in the show worth reaching for
  int16_t brightX, brightY;
  int8_t beatSide;
  unsigned long nextRoam;
  bool holdCorner;              // a voice turn or its bubble is up: no roams, a poke only flinches
  // Voice turn (set by bot_mode each frame): 0 none, 1 listening, 2 thinking, 3 speaking
  uint8_t talk, talkWas;
  float talkEnv, talkPrev;      // the reply's loudness 0..1, and last frame's
  BotSpring talkSm;             // smoothed loudness: the body swells with it
  float swC, swS;               // thinking swirl: the n = 3 mode's rotating target
  int8_t earI;                  // the listening tentacle (slot, generation)
  uint16_t earG;
  unsigned long lastSyl;
  // Corner run (poke dash or roam)
  const PartyRun *run;          // null when not running
  bool going;
  uint8_t runTo;
  unsigned long goAt;
  float fromX, fromY, dstX, dstY;
  int8_t grabI, strI[2];        // the run's grab tentacle and streamers (slot, generation)
  uint16_t grabG, strG[2];
  // Morph, shake, fidgets, beat
  uint8_t morph;                // 0 none, 1 entering, 2 exiting
  unsigned long exitAt, porcUntil, shakeAt, shiverAt, nextFidget, padUntil, lastHop, lastPop;
  uint8_t lastFidget;
  float prevEnv;
  // A face for bot_mode to show (0xFF none), and for how long
  uint8_t exprReq;
  uint16_t exprMs;
  // Pattern library
  PartyBank banks[PARTY_BANKS];
  PartySeq seqs[PARTY_SEQS];
  BotSpring kappa;              // calm gain: edge waves and agitation (Calm Dome turns it down)
  float seqG;                   // mound under a crown
  unsigned long schNextAt, schLayerAt, schCd[SEQ_COUNT], throbUntil;
  bool schLayerArmed, schBigInStack;
  int8_t schLast;
  uint8_t schSmallRun, schSpikeRun, schForceSmall, schHistN;
  PartyHist schHist[16];

  PartyTent tents[PARTY_TENTS ? PARTY_TENTS : 1];
  PartyDrop drops[PARTY_DROPS ? PARTY_DROPS : 1];
  PartyEv evq[PARTY_EVQ];
  uint8_t nEv;
  uint16_t tentGen;
  // cost guard
  unsigned long guardAt, calmSince;
  uint32_t guardMax;
  // Code mode (spec: docs/vizself/code-mode-motion-spec.md)
  bool codeOn;                  // wanted: from /bot/code?v=1 until the exit starts
  bool code;                    // centre layout: home and axis in the middle (kept through the exit morph)
  bool codeParty;               // flew in from his party corner, so he flies back
  bool codeVoice;               // a voice turn was on: its end closes code mode
  uint8_t cPhase, quipExpr, quipN, cFace;
  BotSpring cW;                 // 0 the party tier .. 1 the code tier (radius, eye scale)
  PartyTier curT = PARTY_TIERS[1];
  unsigned long codeT0, phaseT0, landedAt, quipAt, quipHeardAt, quipLoudAt, quipEndAt, lastQuipAt, quipPeriod;
  unsigned long foundWaitUntil, headAt, fakeUntil, tickAt, exprHold;
  uint16_t quipMs;
  bool quipAudio, foundWait;
  int8_t talkLvl;               // lip-sync level 0..12, or -1 with no reply audio (set by bot_mode)
  BotSpring lvlSm;
  float codeM2, codeE, codeKappa, codePump;
  int8_t rdI[2]; uint16_t rdG[2];   // the reader pair (slot, generation)
  uint8_t rdVar, rdStep, rdSide, rdSacc, rdLines, rdLeft, rdBoop, fdF;
  unsigned long cFaceAt, codeHeroAt;
  uint8_t codeHero;
  int32_t rdRow;
  unsigned long rdT0;
  float rdMs, rdStop;
  uint8_t ticksLeft;
  uint16_t tickGap;
  // Out to bot_mode, which owns the mouth, head, LEDs and synth
  bool mouthOn;
  int16_t mouthX, mouthY, mouthFloor;
  float mouthK;
  int8_t fakeLvl;               // hero=quip: a made-up lip-sync level, or -1
  bool headReq;
  int16_t headYaw, headPitch;   // degrees: yaw from centre, pitch from home
  uint16_t headMs;
  uint8_t ledBeat, ledPos, ledLvl, sndReq;
  unsigned long ledAt;

  const PartyTier &tier() const { return curT; }
  // Party size, or on the way to (and in) code mode a blend toward the code tier
  void syncTier() {
    const PartyTier &P0 = PARTY_TIERS[partySize < 3 ? partySize : 1];
    float w = constrain(cW.x, 0.0f, 1.0f);
    if (w < 0.002f) { curT = P0; return; }
    if (w > 0.998f) { curT = CODE_TIER; return; }
    auto mix = [w](float a, float b) { return a + (b - a) * w; };
    curT = {mix(P0.k, CODE_TIER.k), (int16_t)lroundf(mix(P0.r, CODE_TIER.r)), (int16_t)lroundf(mix(P0.env, CODE_TIER.env)),
            (int16_t)lroundf(mix(P0.below, CODE_TIER.below)), P0.wanderW, P0.wanderH, true};
  }

  // Wander box runs inward from the tuck point; home is its centre (layout spec §2)
  void box(uint8_t c, float &x0, float &x1, float &y0, float &y1) const {
    const PartyTier &T = tier();
    float tx = partyRight(c) ? LCD_WIDTH - PARTY_EDGE_GAP - T.env : PARTY_EDGE_GAP + T.env;
    float ty = partyTop(c) ? PARTY_EDGE_GAP + T.env : LCD_HEIGHT - PARTY_EDGE_GAP - T.below;
    x0 = tx; x1 = tx + (partyRight(c) ? -T.wanderW : T.wanderW);
    y0 = ty; y1 = ty + (partyTop(c) ? T.wanderH : -T.wanderH);
  }
  void home(uint8_t c, float &hx, float &hy) const {
    if (code) { hx = CODE_CX; hy = CODE_CY; return; }
    float x0, x1, y0, y1;
    box(c, x0, x1, y0, y1);
    hx = (x0 + x1) / 2; hy = (y0 + y1) / 2;
  }
  // Spikes and tentacles mostly point into the show, within a cone around the line to the screen centre
  float alphaHome() const {
    if (code) return -HALF_PI;   // code mode: every pattern sits on his head
    float hx, hy;
    home(corner, hx, hy);
    return atan2f(LCD_HEIGHT / 2.0f - hy, LCD_WIDTH / 2.0f - hx);
  }
  static float wrapPi(float a) {
    a = fmodf(a + PI, TWO_PI);
    if (a < 0) a += TWO_PI;
    return a - PI;
  }
  float clampCone(float a, float deg) const {
    float h = alphaHome();
    return h + constrain(wrapPi(a - h), -deg * DEG_TO_RAD, deg * DEG_TO_RAD);
  }
  float clampInward(float a) const { return clampCone(a, 60); }

  void init() {
    for (int i = 0; i < PARTY_N; i++) { partyCos3[i] = cosf(i * 3 * DEG_TO_RAD); partySin3[i] = sinf(i * 3 * DEG_TO_RAD); }
    const int harm[4] = {3, 4, 5, 7};
    for (int h = 0; h < 4; h++) { partyStepC[h] = cosf(harm[h] * 3 * DEG_TO_RAD); partyStepS[h] = sinf(harm[h] * 3 * DEG_TO_RAD); }
    for (int k = 0; k < 4; k++) { partyModeStepC[k] = cosf((k + 2) * 3 * DEG_TO_RAD); partyModeStepS[k] = sinf((k + 2) * 3 * DEG_TO_RAD); }
    for (int i = 0; i < 12; i++) { partyCos30[i] = cosf(i * 30 * DEG_TO_RAD); partySin30[i] = sinf(i * 30 * DEG_TO_RAD); }
    wx.init(0, 0.55f, 0.75f); wy.init(0, 0.55f, 0.75f);
    L.init(0, 1.9f, 0.42f);
    for (int k = 0; k < PARTY_MODES; k++) {
      const PartyModeDef &m = PARTY_MODE_DEF[k];
      mc[k].init(0, m.f, m.z); ms[k].init(0, m.f, m.z);
      pkM[k] = botSpringPeak(m.f, m.z);
    }
    al.init(0, 1.2f, 0.9f); F.init(0, 3, 0.8f);
    for (int k = 0; k < PARTY_SPIKES; k++) spk[k].init(0, 3.2f, 0.45f);
    g.init(0, 1.5f, 0.9f); E.init(0, 1.2f, 1);
    ox.init(0, 2.0f, 0.38f); oy.init(0, 2.0f, 0.38f);
    D.init(0, 3.2f, 0.28f); Q.init(0, 1.5f, 0.85f);
    P.init(1, 1.4f, 0.82f); hP.init(0, 5, 0.6f); beatSm.init(0, 3, 0.7f); talkSm.init(0, 5, 0.7f);
    pkL = botSpringPeak(1.9f, 0.42f);
    pkD = botSpringPeak(3.2f, 0.28f);
    pkE = botSpringPeak(1.2f, 1);
    pkBulb = botSpringPeak(2, 0.4f);
    exprReq = 0xFF;
    kappa.init(1, 1.6f, 1);
    for (int i = 0; i < PARTY_BANKS; i++) banks[i].owner = -1;
    schLast = -1;
    cW.init(0, 2.0f, 0.9f); lvlSm.init(0, 3, 0.8f);
    rdI[0] = rdI[1] = -1; talkLvl = fakeLvl = -1; codeKappa = 1;
    curT = PARTY_TIERS[1];
    active = false;
    ready = true;
    syncTier();
  }

  // A surface mode kick of peak amp (fraction of r) centred on angle th
  void modeKick(int n, float amp, float th) {
    int k = n - 2;
    if (k < 0 || k >= PARTY_MODES) return;
    amp *= PARTY_JIGGLE;
    mc[k].v += amp * cosf(n * th) / pkM[k];
    ms[k].v += amp * sinf(n * th) / pkM[k];
  }
  void expr(uint8_t e, uint16_t ms) { exprReq = e; exprMs = ms; exprHold = millis() + min((uint16_t)1500, ms); cFace = 0xFF; }

  void clearExtras() {
    for (int i = 0; i < PARTY_TENTS; i++) tents[i].on = false;
    for (int i = 0; i < PARTY_DROPS; i++) drops[i].on = false;
    nEv = 0; borrow = 0; grabI = strI[0] = strI[1] = -1;
    for (int i = 0; i < PARTY_SEQS; i++) { calmEnd(seqs[i]); seqs[i].on = false; }
    for (int i = 0; i < PARTY_BANKS; i++) { banks[i].owner = -1; for (int j = 0; j < PARTY_SLOTS; j++) { banks[i].s[j].on = false; banks[i].s[j].h.init(0, 3.2f, 0.45f); } }
    kappa.snap(1); seqG = 0; throbUntil = 0; schLayerArmed = false; schBigInStack = false;
  }

  // First party frame. When party mode has just started he picks a corner and
  // morphs into it from the face; back from a sheet he is simply there again.
  void enter(unsigned long now) {
    if (!ready) init();
    syncTier();
    bool fresh = !haveCorner;
    if (fresh) { corner = random(4); haveCorner = true; }
    float hx, hy;
    home(corner, hx, hy);
    wx.snap(hx); wy.snap(hy); wTx = hx; wTy = hy;
    L.snap(0); F.snap(0); g.snap(0); E.snap(0); ox.snap(0); oy.snap(0); D.snap(0);
    for (int k = 0; k < PARTY_MODES; k++) { mc[k].snap(0); ms[k].snap(0); }
    mc[0].snap(0.05f);
    clearExtras();
    run = nullptr; tapT = shakeT = 0; thD = 0; haveBright = false; beatSide = 1;
    nextRoam = now + (unsigned long)eyeRnd(PARTY_ROAM_MIN_MS, PARTY_ROAM_MAX_MS);
    for (int k = 0; k < PARTY_SPIKES; k++) spk[k].snap(0);
    alT = alphaHome(); al.snap(alT);
    for (int i = 0; i < 6; i++) ph[i] = eyeRnd(0, TWO_PI);
    fieldT = 0; fieldUntil = 0; evAt = 0; phiV = 0; bulgeA = alT;
    icx = (int16_t)lroundf(hx); icy = (int16_t)lroundf(hy); iox = ioy = 0;
    sheen[0] = 40; sheen[1] = 40; sheen[2] = 60;
    lastMs = now;
    nextWander = now + 1000;
    nextPulse = now + (unsigned long)eyeRnd(5000, 12000);
    nextFidget = now + (unsigned long)eyeRnd(12000, 25000);
    nextFeeler = now + (unsigned long)eyeRnd(5000, 9000);
    nextBright = now;
    schNextAt = now + (fresh ? 5000 : 2500);   // a fresh party lands and stretches first
    porcUntil = shakeAt = shiverAt = padUntil = lastHop = lastPop = lastBeatGrab = 0;
    guardAt = now; guardMax = 0; calmSince = 0;
    hP.snap(0); beatSm.snap(0); prevEnv = 0;
    talkWas = 0; talkPrev = 0; talkSm.snap(0); swC = swS = 0; earI = -1;
    botEyes.blinkStart(now, BW_SWAP);   // every swap between face and drop hides under a blink
    active = true;
    P.f = code ? CODE_MORPH_F : 1.4f; P.z = code ? 0.85f : 0.82f;   // code mode: the outline rounds into the drop in place
    if (code) schNextAt = now + 2500;
    if (fresh) {
      morph = 1;
      P.snap(0); P.t = 1;
      mc[0].snap(PARTY_MORPH_J);
      wx.snap(BOT_FACE_CX); wy.snap(BOT_FACE_CY);
      icx = BOT_FACE_CX; icy = BOT_FACE_CY;
    } else {
      morph = 0;
      P.snap(1);
      modeKick(2, 0.14f, 0);           // land with a squash
      field(alT, 0.7f, 300, now);      // and a spike burst
    }
    t = eyeSecs(now);
    Reff = morphR();
    prepShape(Reff);
  }
  // Party mode turned off: tentacles drain, a beat reaching for the middle, then the hop back
  void beginExit(unsigned long now) {
    if (morph == 2) return;
    abortSeqs(now);
    if (run) { run = nullptr; }
    morph = 2; exitAt = now;
    retractAll(true);
    for (int i = 0; i < PARTY_DROPS; i++) drops[i].flyMs = 0;
    field(alphaHome(), 0.6f, 200, now);
    modeKick(2, 0.10f, 0);
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
    abortSeqs(now);
    porcUntil = now + 450; shakeAt = now; shakeT = 1;
    retractAll(true);
    static const float amp[4] = {0.18f, 0.12f, 0.08f, 0.05f};
    for (int k = 0; k < PARTY_MODES; k++) modeKick(k + 2, amp[k], eyeRnd(0, TWO_PI));
    botEyes.blinkStart(now, BW_SWAP);
    int n = 3 + random(3);   // a splash of droplets into the show
    for (int i = 0; i < n; i++) later(now, i * 40, PE_DROP, -1, 0, clampCone(alphaHome() + eyeRnd(-70, 70) * DEG_TO_RAD, 75), eyeRnd(150, 260));
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
    clearExtras();
    if (!keepCorner) haveCorner = false;
  }

  bool contains(int16_t x, int16_t y) const { return hypotf(x - icx, y - icy) < tier().r * 1.05f; }
  bool running() const { return run != nullptr; }

  // Shy poke (hero 6.2): flinch where the finger lands, a beat of stillness,
  // one tiny peek at the finger, then the slingshot to another corner
  void poke(int16_t x, int16_t y, unsigned long now) {
    if (busy()) return;
    abortSeqs(now);
    thD = atan2f(y - icy, x - icx);
    D.v += -0.22f * Reff / pkD;
    if (code) { modeKick(2, -0.10f, thD); retractAll(true); tapT = 1; botEyes.blinkStart(now, BW_SWAP); return; }   // reading: a flinch, then the readers regrow
    modeKick(2, -0.10f, thD); modeKick(3, -0.07f, thD); modeKick(4, -0.05f, thD); modeKick(5, -0.03f, thD);
    later(now, 90, PE_KICK2, -1, 0, 0.30f);
    retractAll(true);
    for (int i = 0; i < PARTY_DROPS; i++) drops[i].flyMs = 0;
    tapT = 1;
    botEyes.blinkStart(now, BW_SWAP);
    later(now, 300, PE_PEEK, -1, 0, x, y);
    if (!holdCorner) startRun((corner + 1 + random(3)) % 4, PARTY_POKE, now, 820);   // mid-turn he stays by his bubble
  }
  // Tap on the show: a spike fan and a tentacle reach for the finger, boop, and let go
  void tapAt(int16_t x, int16_t y, unsigned long now) {
    if (busy() || code) return;
    abortSeqs(now);
    lookAt(x, y, 600, now);
    field(atan2f(y - icy, x - icx), 0.8f, 400, now);
    L.v += 0.12f * tier().r / pkL;
    modeKick(2, -0.06f, 0);
    tapT = 1;
    later(now, 110, PE_REACH, -1, 0, x, y);
    nextWander = now + 1200;
    nextFeeler = now + 6000;
  }
  // Roaming mostly picks a neighbouring corner (a walk along one edge); one move in four crosses the diagonal
  void roam(unsigned long now) {
    uint8_t to = random(4) ? (corner + (random(2) ? 1 : 3)) % 4 : (corner + 2) % 4;
    startRun(to, PARTY_ROAM, now, 0);
  }

  // Voice turn: the blob shows what the brain is doing, even with the sound off.
  // Listening: he perks up, an ear tentacle reaches into the room and he looks out.
  // Thinking: a slow swirl round the rim, a small spike fan circling, eyes up and away.
  // Speaking: the body swells with the reply's loudness, spikes puff on each syllable.
  void stepTalk(unsigned long now, float dt, float r, bool free) {
    if (talk != talkWas) {
      PartyTent *ear = tentOf(earI, earG);
      if (ear) retract(*ear, false);
      earI = -1;
      if (!talkWas && !code) abortSeqs(now);
      if (talk == 1 && free) {
        L.v += 0.10f * r / pkL; modeKick(2, -0.08f, 0);   // perk up
        PartyTentOpt o;
        o.track = true; o.tx = LCD_WIDTH / 2.0f; o.ty = LCD_HEIGHT / 2.0f; o.L = 0.9f * r;
        o.hold = -1; o.free = true; o.curl = 0.2f; o.swA = 0.35f; o.swF = 0.6f; o.boops = 2;
        if (PARTY_TENTS) { earI = spawnTent(o, now); earG = tentGen; }
      }
      talkWas = talk;
    }
    talkSm.step(talk == 3 ? talkEnv : 0, dt);
    swC = swS = 0;
    if (!talk || !free) { talkPrev = 0; return; }
    float toCx = partyRight(corner) ? -0.6f : 0.6f;   // eyes look toward the screen, away from the wall
    if (talk == 1) {
      lookAt(LCD_WIDTH / 2.0f, LCD_HEIGHT / 2.0f, 300, now);
    } else if (talk == 2) {
      float th = TWO_PI * 0.35f * t;
      swC = 0.07f * cosf(3 * th); swS = 0.07f * sinf(3 * th);
      alT = th; fieldT = 0.3f; fieldUntil = now + 200; F.f = 1.2f;
      look(toCx, -0.8f, 300, now);
    } else if (talk == 3) {
      if (talkEnv > 0.4f && talkPrev < 0.25f && now - lastSyl > 140) {
        field(alphaHome(), 0.3f + 0.35f * talkEnv, 160, now);
        modeKick(2, 0.04f, 0);
        lastSyl = now;
      }
    }
    talkPrev = talkEnv;
  }
  // The arc bows toward the screen centre; its middle is where the grab tentacle latches
  void bowPoint(const PartyRun &m, float &kx, float &ky) const {
    float mx = (fromX + dstX) / 2, my = (fromY + dstY) / 2;
    kx = mx + (LCD_WIDTH / 2.0f - mx) * m.bow; ky = my + (LCD_HEIGHT / 2.0f - my) * m.bow;
  }
  void startRun(uint8_t to, const PartyRun &m, unsigned long now, uint16_t leadMs) {
    uint16_t lead = leadMs ? leadMs : m.leadMs;
    run = &m; going = false; runTo = to; goAt = now + lead;
    home(to, dstX, dstY);
    fromX = wx.x; fromY = wy.x;
    if (&m == &PARTY_ROAM) lookAt(dstX, dstY, lead + 1400, now);
    else later(now, lead - 150, PE_LOOK, -1, 0, dstX, dstY, 1500);
    float kx, ky;
    bowPoint(m, kx, ky);
    later(now, lead > 240 ? lead - 240 : 0, PE_GRAB, -1, 0, 0.25f * fromX + 0.5f * kx + 0.25f * dstX, 0.25f * fromY + 0.5f * ky + 0.25f * dstY);
    grabI = strI[0] = strI[1] = -1;
  }

  // An event pulls a spike fan toward dir, at strength F, for holdMs
  void field(float dir, float f, unsigned long holdMs, unsigned long now, bool free = false, bool slow = false) {
    alT = free ? dir : clampInward(dir); fieldT = f; fieldUntil = now + holdMs; evAt = now;
    F.f = slow ? 1.2f : 3;
    for (int k = 0; k < PARTY_SPIKES; k++) jit[k] = eyeRnd(0.85f, 1.15f);
  }
  void look(float x, float y, unsigned long ms, unsigned long now) {
    botEyes.padX = constrain(x, -1.0f, 1.0f); botEyes.padY = constrain(y, -1.0f, 1.0f);
    padUntil = now + ms;
  }
  void lookAt(float x, float y, unsigned long ms, unsigned long now) { look((x - icx) / 40, (y - icy) / 40, ms, now); }

  // ---- Timed events -----------------------------------------------------------
  void later(unsigned long now, unsigned long ms, uint8_t kind, int8_t ref, uint16_t gen, float a = 0, float b = 0, float c = 0) {
    if (nEv >= PARTY_EVQ) return;
    PartyEv &e = evq[nEv++];
    e.at = now + ms; e.kind = kind; e.ref = ref; e.gen = gen; e.a = a; e.b = b; e.c = c;
  }
  void runEvents(unsigned long now) {
    for (int i = 0; i < nEv;) {
      if (!eyeReached(now, evq[i].at)) { i++; continue; }
      PartyEv e = evq[i];
      evq[i] = evq[--nEv];
      fire(e, now);
    }
  }
  PartyTent *tentOf(int8_t i, uint16_t gen) {
    if (i < 0 || i >= PARTY_TENTS || !tents[i].on || tents[i].gen != gen) return nullptr;
    return &tents[i];
  }
  void fire(const PartyEv &e, unsigned long now) {
    PartyTent *tt = tentOf(e.ref, e.gen);
    const float r = tier().r;
    PartyTentOpt o;
    switch (e.kind) {
      case PE_BOOP: if (tt && tt->phase == PT_HOLD) tt->bulb.v += 0.4f / pkBulb; break;
      case PE_KICK2: modeKick(2, e.a, 0); break;
      case PE_LOOK: lookAt(e.a, e.b, (unsigned long)e.c, now); break;
      case PE_GRAB:
        if (!run) break;
        o.track = true; o.tx = e.a; o.ty = e.b; o.L = capLen(); o.kind = PT_GRAB; o.hold = -1;
        o.gf = run->gf; o.gz = run->gz; o.curl = 0.1f; o.free = true; o.swA = 0; o.boops = 0;
        grabI = spawnTent(o, now); grabG = tentGen;
        break;
      case PE_PEEK:
        lookAt(e.a, e.b, 500, now);
        o.track = true; o.tx = e.a; o.ty = e.b; o.L = 0.3f * r; o.kind = PT_PEEK; o.hold = 120;
        o.gf = 3; o.gz = 0.6f; o.curl = 0.1f; o.free = true; o.boops = 0;
        spawnTent(o, now);
        break;
      case PE_REACH:
        o.track = true; o.tx = e.a; o.ty = e.b; o.L = capLen(); o.hold = 500;
        o.curl = 0.15f; o.free = true; o.swA = 0.15f;
        spawnTent(o, now);
        break;
      case PE_FEELER:   // ref bit 0: reaching for a bright spot; bit 1: a big free reach
        o.track = true; o.tx = e.a; o.ty = e.b; o.L = e.c; o.kind = PT_FEELER; o.hold = (int32_t)eyeRnd(2500, 4000);
        if (e.c > 1.6f * r) { o.gf = 1.1f; o.gz = 0.55f; }
        o.curl = 0.35f; o.swA = 0.35f; o.boops = (e.ref & 1) ? 2 : 1; o.free = (e.ref & 2) != 0;
        spawnTent(o, now);
        break;
      case PE_WAKE: wakeStretch(now); break;
      case PE_FEELER_GO: if (!busy()) feeler(now, false, e.a, false, e.b != 0); break;
      case PE_WAKE2: {
        float s = wrapPi(e.a - alphaHome()) > 0 ? -1 : 1;
        o.dir = e.a + s * 48 * DEG_TO_RAD; o.L = r * PARTY_REACH; o.hold = 700;
        o.gf = 1.1f; o.gz = 0.7f; o.curl = 0.4f * s; o.free = true; o.swA = 0.1f; o.swF = 0.3f; o.boops = 0;
        spawnTent(o, now);
        break;
      }
      case PE_WAKE_END:
        modeKick(2, 0.28f, 0); modeKick(4, 0.05f, 0);
        expr(EXPR_HAPPY, 1500);
        botEyes.blinkStart(now, BW_SWAP);
        break;
      case PE_DROP: {
        float r0 = Reff, c = cosf(e.a), s = sinf(e.a);
        spawnDrop(icx + r0 * c, icy + r0 * s, e.b * c, e.b * s, eyeRnd(0.07f, 0.11f) * r, now, false);
        break;
      }
      case PE_RETRACT: if (tt) retract(*tt, false); break;
      case PE_BEAT: {
        for (int i = 0; i < PARTY_TENTS; i++) {
          PartyTent &b = tents[i];
          if (b.on && b.kind == PT_BEAT && b.phase != PT_RETRACT) { retract(b, true); later(now, 160, PE_KICK2, -1, 0, 0.12f); }
        }
        float Lb = hypotf(e.a - icx, e.b - icy) - Reff;
        o.track = true; o.tx = e.a; o.ty = e.b; o.L = constrain(Lb, 0.6f * r, capLen()); o.kind = PT_BEAT; o.hold = 200;
        o.gf = 3.2f; o.gz = 0.55f; o.curl = 0.25f; o.swA = 0; o.free = true;
        spawnTent(o, now);
        break;
      }
      case PE_BEAT_PINCH:
        for (int i = 0; i < PARTY_TENTS; i++) {
          PartyTent &b = tents[i];
          if (b.on && b.kind == PT_BEAT && !b.pinchArmed) { b.pinchArmed = true; b.pinchAt = now; b.pinchV = 160; break; }
        }
        break;
    }
  }

  // ---- Hero moments -----------------------------------------------------------
  float capLen() const { return min(2.4f * tier().r * PARTY_REACH, 130.0f); }
  float bigL(float lo, float hi) const {
    float r = tier().r * PARTY_REACH;
    return eyeRnd01() < PARTY_BIG ? eyeRnd(1.8f, 2.4f) * r : eyeRnd(lo, hi) * r;
  }
  // A point Lrim past the rim, somewhere in the cone into the show
  void inCone(float Lrim, float &x, float &y) const {
    float a = clampCone(alphaHome() + eyeRnd(-55, 55) * DEG_TO_RAD, 60), d = Reff + Lrim;
    x = icx + d * cosf(a); y = icy + d * sinf(a);
  }
  int liveTents() const {
    int n = 0;
    for (int i = 0; i < PARTY_TENTS; i++) n += tents[i].on;
    return n;
  }
  // A whip out and back; often the tip pinches off as a droplet
  void flick(unsigned long now, float dropChance) {
    float a = clampCone(alphaHome() + eyeRnd(-45, 45) * DEG_TO_RAD, 60), px, py;
    inCone(eyeRnd(0.8f, 1.0f) * tier().r, px, py);
    lookAt(px, py, 700, now);
    PartyTentOpt o;
    o.dir = a; o.L = eyeRnd(0.8f, 1.0f) * tier().r * PARTY_REACH; o.kind = PT_FLICK; o.hold = 60;
    o.gf = 3.5f; o.gz = 0.5f; o.curl = 0.5f; o.boops = 0; o.swA = 0;
    int i = spawnTent(o, now);
    if (i >= 0 && eyeRnd01() < dropChance) { tents[i].pinchArmed = true; tents[i].pinchAt = now + 210; tents[i].pinchV = eyeRnd(170, 230); }
  }
  // A slow feel into the show, toward a bright spot when there is one; 60 % go big.
  // aimDeg: off the line to the screen centre (a group spreads out); 999 = anywhere in the cone.
  void feeler(unsigned long now, bool forceBig, float aimDeg = 999, bool mayBright = true, bool shortOnly = false) {
    const float r = tier().r;
    float Lf = forceBig ? eyeRnd(1.9f, 2.4f) * r * PARTY_REACH : shortOnly ? eyeRnd(0.8f, 1.3f) * r * PARTY_REACH : bigL(0.8f, 1.3f);
    Lf = min(Lf, capLen());
    bool bright = haveBright && !forceBig && mayBright;
    float px, py;
    if (bright) { px = brightX; py = brightY; }
    else if (aimDeg < 900) {
      float a = clampCone(alphaHome() + aimDeg * DEG_TO_RAD, 60);
      px = icx + (Reff + Lf) * cosf(a); py = icy + (Reff + Lf) * sinf(a);
    } else inCone(Lf, px, py);
    float dx = px - icx, dy = py - icy, d = max(1.0f, hypotf(dx, dy)), need = Reff + Lf;
    float tx = icx + dx / d * need, ty = icy + dy / d * need;
    lookAt(tx, ty, 1600, now);
    later(now, 120, PE_FEELER, (bright ? 1 : 0) | (forceBig ? 2 : 0), 0, tx, ty, Lf);
  }
  // A small bunch of feelers fanned across the cone, a beat apart (1 in 4 alone,
  // else 2 or 3), never more than the pool has room for. shortOnly: 0.8-1.3 r (pattern library)
  void feelerGroup(unsigned long now, bool shortOnly = false) {
    int room = partyTentCap - liveTents();
    int n = min(room, eyeRnd01() < 0.25f ? 1 : 2 + (int)random(2));
    if (n <= 0) return;
    float base = eyeRnd(-20, 20), gap = n == 2 ? 28 : 38;
    for (int i = 0; i < n; i++) {
      float deg = base + (i - (n - 1) / 2.0f) * gap;
      if (i == 0) feeler(now, false, deg, true, shortOnly);
      else later(now, i * (unsigned long)eyeRnd(220, 420), PE_FEELER_GO, -1, 0, deg, shortOnly);
    }
  }
  // Wake-up stretch (hero 6.5): one long slow reach to the middle of the screen,
  // a shorter one beside it, both drain back, a big contented jiggle
  void wakeStretch(unsigned long now) {
    const float r = tier().r, cx = LCD_WIDTH / 2.0f, cy = LCD_HEIGHT / 2.0f;
    float a = atan2f(cy - icy, cx - icx);
    expr(EXPR_CHILL, 2200);
    lookAt(cx, cy, 2000, now);
    PartyTentOpt o;
    o.dir = a; o.L = min(capLen(), max(1.6f * r, hypotf(cx - icx, cy - icy) - Reff - 10) * PARTY_REACH);
    o.hold = 900; o.gf = 0.9f; o.gz = 0.7f; o.curl = 0.2f; o.free = true; o.swA = 0.08f; o.swF = 0.3f; o.boops = 0;
    spawnTent(o, now);
    later(now, 380, PE_WAKE2, -1, 0, a);
    later(now, 2300, PE_WAKE_END, -1, 0);
  }
  // Beat grab (hero 6.1): tentacles drum the beat, alternating sides
  void beatGrab(unsigned long now, float bass) {
    beatSide = -beatSide;
    float a = clampCone(alphaHome() + beatSide * 35 * DEG_TO_RAD, 60), px, py;
    if (haveBright && eyeRnd01() < 0.5f) { px = brightX; py = brightY; }
    else { float Lb = bigL(1.0f, 1.4f); px = icx + (Reff + Lb) * cosf(a); py = icy + (Reff + Lb) * sinf(a); }
    lookAt(px, py, 420, now);
    later(now, 100, PE_BEAT, -1, 0, px, py);
    if (bass > 0.8f && eyeRnd01() < 0.35f) later(now, 260, PE_BEAT_PINCH, -1, 0);
    lastBeatGrab = now;
  }
  // GET /bot/party?hero= — play one on demand, to test and tune
  void heroRequest(unsigned long now) {
    uint8_t sq = partySeqReq;
    if (sq) { partySeqReq = 0; if (sq <= SEQ_COUNT) startSeq(sq - 1, now, partyStack > 0); }
    uint8_t h = partyHeroReq;
    if (!h) return;
    partyHeroReq = 0;
    abortSeqs(now);
    switch (h) {
      case 1: flick(now, 1); break;
      case 2: feelerGroup(now); break;
      case 3: feeler(now, true); break;
      case 4: wakeStretch(now); break;
      case 5: roam(now); break;
      case 6: shake(now); break;
      case 7: beatGrab(now, 0.9f); break;
    }
  }

  // ---- Pattern library: spike banks -----------------------------------------
  static float crownH(PartySlot &s, float F, float H) {   // Rosensweig threshold with hysteresis
    s.popped = F >= s.Fon || (s.popped && F > s.Foff);
    return s.popped ? F * H * s.env : 0;
  }
  static float winH(float x) { return max(0.0f, (cosf(x) - 0.42262f) / 0.57738f); }   // the show arc, ±65°
  static float ramp01(float t, float a, float b) { return constrain((t - a) / (b - a), 0.0f, 1.0f); }
  static void setSlot(PartyBank &b, int i, int8_t off, int8_t k, uint8_t wide = 5, float env = 1, float Fon = 0, float Foff = 0) {
    PartySlot &s = b.s[i];
    s.on = true; s.popped = false; s.off = off; s.k = k; s.wide = wide; s.env = env; s.Fon = Fon; s.Foff = Foff; s.hT = 0; s.lean = 0;
  }
  static void bankSprings(PartyBank &b, float rf, float rz, float ff, float fz) { b.rf = rf; b.rz = rz; b.ff = ff; b.fz = fz; }
  void claimBank(int qi) {
    int pick = -1;
    for (int i = 0; i < PARTY_BANKS && pick < 0; i++) {
      if (banks[i].owner >= 0) continue;
      bool drained = true;
      for (int j = 0; j < PARTY_SLOTS; j++) if (fabsf(banks[i].s[j].h.x) > 0.5f) drained = false;
      if (drained) pick = i;
    }
    for (int i = 0; i < PARTY_BANKS && pick < 0; i++) if (banks[i].owner < 0) pick = i;
    if (pick < 0) { pick = 0; releaseSeq(banks[0].owner); }
    PartyBank &b = banks[pick];
    b.owner = qi; seqs[qi].bank = pick;
    bankSprings(b, 3.2f, 0.45f, 4.5f, 0.38f);
    for (int j = 0; j < PARTY_SLOTS; j++) { b.s[j].on = false; b.s[j].hT = 0; b.s[j].lean = 0; b.s[j].h.init(0, 3.2f, 0.45f); }
  }
  void stepSlots(unsigned long now, float dt) {
    const float r = tier().r, throb = (long)(now - throbUntil) < 0 ? 0.06f * r : 0;
    for (int i = 0; i < PARTY_BANKS; i++) {
      PartyBank &b = banks[i];
      for (int j = 0; j < PARTY_SLOTS; j++) {
        PartySlot &s = b.s[j];
        if (!s.on && fabsf(s.h.x) < 0.05f && fabsf(s.h.v) < 0.5f) continue;
        float target = s.on ? s.hT + (s.hT > 1 ? throb : 0) : 0;
        if (target > s.h.x) { s.h.f = b.rf; s.h.z = b.rz; } else { s.h.f = b.ff; s.h.z = b.fz; }
        s.h.step(target, dt);
        if (s.h.x < -0.05f * r) { s.h.x = -0.05f * r; s.h.v = 0; }   // the snap-back dimple stays shallow
      }
    }
  }
  // The carousel's spikes move one slot along with the grid
  void shiftSlots(PartyBank &b, int n, int dir) {
    if (dir > 0) for (int i = n - 1; i > 0; i--) { b.s[i].h.x = b.s[i - 1].h.x; b.s[i].h.v = b.s[i - 1].h.v; }
    else for (int i = 0; i < n - 1; i++) { b.s[i].h.x = b.s[i + 1].h.x; b.s[i].h.v = b.s[i + 1].h.v; }
    (dir > 0 ? b.s[0] : b.s[n - 1]).h.snap(0);
  }

  // ---- Pattern library: sequences -------------------------------------------
  int nSeqs() const { int n = 0; for (int i = 0; i < PARTY_SEQS; i++) n += seqs[i].on; return n; }
  bool seqRunning(uint8_t id) const { for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on && seqs[i].id == id) return true; return false; }
  bool seqSolo() const { for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on && PARTY_SEQ_DEF[seqs[i].id].solo) return true; return false; }
  bool seqHolds() const { for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on && PARTY_SEQ_DEF[seqs[i].id].tier != TIER_S) return true; return false; }
  bool seqDrivesAl() const { for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on && seqs[i].driveAl) return true; return false; }
  const PartySeq *leadSeq() const { for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on && seqs[i].lead) return &seqs[i]; return nullptr; }
  bool anyFeeler() const { for (int i = 0; i < PARTY_TENTS; i++) if (tents[i].on && tents[i].kind == PT_FEELER) return true; return false; }
  static bool once(PartySeq &q, int bit, bool cond) {
    if (!cond || (q.f & (1u << bit))) return false;
    q.f |= 1u << bit;
    return true;
  }
  void seqLook(const PartySeq &q, float pad, unsigned long ms, unsigned long now) { if (q.lead) look(cosf(q.axis) * pad, sinf(q.axis) * pad, ms, now); }
  void seqFace(const PartySeq &q, uint8_t e, uint16_t ms) { if (q.lead) expr(code && e == EXPR_CHILL ? EXPR_THINKING : e, ms); }
  void seqBlink(const PartySeq &q, unsigned long now) { if (q.lead) botEyes.blinkStart(now, BW_SWAP); }
  void popS(float a) { botEyes.popS.v += a / botEyes.pkPopS; }
  void seqTent(PartySeq &q, int i) { if (i >= 0 && q.nt < 3) { q.ti[q.nt] = i; q.tg[q.nt] = tents[i].gen; q.nt++; } }
  void calmEnd(PartySeq &q) {
    if (q.kT >= 1) return;
    q.kT = 1; kappa.f = 1.0f;
    for (int k = 0; k < PARTY_MODES; k++) { mc[k].z = ms[k].z = PARTY_MODE_DEF[k].z; }
  }

  void startSeq(uint8_t id, unsigned long now, bool layer, uint16_t holdMs = 0) {
    const PartySeqDef &e = PARTY_SEQ_DEF[id];
    layer = layer && !e.solo && nSeqs() && !seqSolo();
    if (!layer) abortSeqs(now);
    else {
      for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on && seqs[i].id == id) abortOne(i);
      if (nSeqs() >= PARTY_SEQS) { int old = -1; for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on && (old < 0 || seqs[i].t0 < seqs[old].t0)) old = i; abortOne(old); }
      if (!PARTY_STACK_MOST[partyStack]) return;
    }
    int qi = -1;
    for (int i = 0; i < PARTY_SEQS; i++) if (!seqs[i].on) { qi = i; break; }
    if (qi < 0) return;
    if (!layer) { fieldT = 0; alT = alphaHome(); }
    const PartySeq *ld = leadSeq();
    PartySeq &q = seqs[qi];
    q.on = true; q.id = id; q.t0 = now; q.lead = !ld; q.axis = ld ? ld->axis : alphaHome();
    q.driveAl = false; q.f = 0; q.nt = 0; q.g = 0; q.kT = 1; q.phi = 0; q.dir = random(2) ? 1 : -1; q.v = 0; q.hold = 0;
    claimBank(qi);
    if (e.tier != TIER_S) home(corner, wTx, wTy);   // medium and big hold still so the axis can't drift
    PartyBank &b = banks[q.bank];
    switch (id) {
      case SEQ_BREATH:
        for (int k = -2; k <= 2; k++) { int a = abs(k); setSlot(b, k + 2, 6 * k, k, k == 0 ? 7 : 5, a == 0 ? 1 : a == 1 ? 0.85f : 0.6f, 0.32f + 0.06f * a, 0.19f + 0.06f * a); }
        bankSprings(b, 2.0f, 0.6f, 3.0f, 0.5f);
        seqFace(q, EXPR_CHILL, 6800); seqLook(q, 0.3f, 6800, now);
        break;
      case SEQ_CALM:
        q.hold = holdMs ? 600 + holdMs : 2100 + eyeRnd(0, 1000); q.kT = 0.12f; kappa.f = 1.6f;
        for (int k = 0; k < PARTY_MODES; k++) { mc[k].z = ms[k].z = 2 * PARTY_MODE_DEF[k].z; }
        fieldT = 0; seqFace(q, EXPR_CHILL, (uint16_t)q.hold);
        break;
      case SEQ_CROWN: {
        static const float env[5] = {1, 0.9f, 0.75f, 0.6f, 0.24f};
        int half = eyeRnd01() < 0.3f ? 4 : 3;
        for (int k = -half; k <= half; k++) { int a = abs(k); setSlot(b, k + half, 6 * k, k, k == 0 ? 7 : 5, env[a], 0.30f + 0.12f * a, 0.17f + 0.12f * a); }
        seqLook(q, 0.5f, 2800, now);
        break;
      }
      case SEQ_CAROUSEL:
        for (int k = -3; k <= 3; k++) setSlot(b, k + 3, 12 * k, k);   // every other slot: 36° apart
        bankSprings(b, 4, 0.7f, 4, 0.7f);
        q.driveAl = !seqDrivesAl();   // only one pattern turns the grid; a second carousel rides its turn
        seqLook(q, 0.3f, 400, now);
        break;
      case SEQ_SPLIT:
        setSlot(b, 0, 0, 0, 7); setSlot(b, 1, -3, 1); setSlot(b, 2, 3, 1); setSlot(b, 3, -6, 2); setSlot(b, 4, 0, 3); setSlot(b, 5, 6, 2);
        bankSprings(b, 2.4f, 0.55f, 4.5f, 0.38f);
        modeKick(2, -0.05f, 0); seqLook(q, 0.6f, 1100, now);
        break;
      case SEQ_WAVE:
        for (int k = -3; k <= 3; k++) setSlot(b, k + 3, 6 * k, k, 5, abs(k) == 3 ? 0.8f : 1);
        bankSprings(b, 5, 0.6f, 5, 0.6f);
        q.v = eyeRnd01() < 0.5f;   // a third, smaller pass
        break;
      case SEQ_MERGE:
        for (int k = -4; k <= 4; k++) setSlot(b, k + 4, 6 * k, k, k % 3 == 0 ? 7 : 5);
        bankSprings(b, 3.0f, 0.5f, 3.6f, 0.55f);
        seqLook(q, 0.5f, 3600, now); seqFace(q, EXPR_EXCITED, 3200);
        break;
      case SEQ_BLOOM: {
        float u = eyeRnd01();
        q.v = u < 0.25f ? 1 : u < 0.5f ? 2 : 0;   // 1 triple, 2 arch, 0 plain
        if (q.v != 1) setSlot(b, 0, 0, 0);
        modeKick(2, -0.04f, 0); seqLook(q, 0.4f, 2600, now); popS(0.02f);
        break;
      }
      case SEQ_FEELERS: feelerGroup(now, true); break;
      case SEQ_LONG: feeler(now, true); break;
      case SEQ_COMPILE:
        for (int k = -4; k <= 4; k++) setSlot(b, k + 4, 6 * k, k, k == 0 ? 7 : 5);
        seqLook(q, 0.4f, 2200, now);
        break;
      case SEQ_AHA: break;   // its finger grows on the first step
      case SEQ_GRAND: {
        static const float env[5] = {1, 0.9f, 0.78f, 0.62f, 0.45f};
        for (int k = -4; k <= 4; k++) { int a = abs(k); setSlot(b, k + 4, 6 * k, k, k == 0 ? 7 : 5, env[a], 0.20f + 0.06f * a, 0.10f + 0.06f * a); }
        bankSprings(b, 4, 0.4f, 4.5f, 0.38f);
        break;
      }
      case SEQ_VOICE:
        for (int k = -2; k <= 2; k++) { int a = abs(k); setSlot(b, k + 2, 6 * k, k, k == 0 ? 7 : 5, a == 0 ? 1 : a == 1 ? 0.85f : 0.6f, 0.32f + 0.06f * a, 0.19f + 0.06f * a); }
        bankSprings(b, 2.0f, 0.6f, 3.0f, 0.5f);
        q.v = holdMs;   // 1: a sharp face is up, so the body pumps instead of spiking
        break;
    }
    // history, cool-downs and the stacking window
    if (schHistN == 16) { memmove(schHist, schHist + 1, 15 * sizeof(PartyHist)); schHistN = 15; }
    schHist[schHistN++] = {now, e.tier, e.tents};
    schLayerAt = now + (unsigned long)eyeRnd(700, 2000); schLayerArmed = true;
    if (layer) return;
    schLast = id; schCd[id] = now + e.cdS * 1000UL;
    schSpikeRun = e.spikes ? schSpikeRun + 1 : 0;
    schSmallRun = e.tier == TIER_S ? schSmallRun + 1 : 0;
    if (e.tier == TIER_S && schForceSmall) schForceSmall--;
  }

  // One frame of sequence q at t ms; true when it's done
  bool stepSeq(PartySeq &q, unsigned long now, float t, float dt) {
    const float r = tier().r;
    PartyBank &b = banks[q.bank];
    PartySlot *s = b.s;
    switch (q.id) {
      case SEQ_BREATH: {
        float F; int br = -1;
        if (t < 500) F = 0.15f * t / 500;
        else if (t < 6500) {
          br = min(2, (int)((t - 500) / 2000));
          float u = fmodf(t - 500, 2000) / 2000, pk = br == 0 ? 0.70f : br == 1 ? 0.75f : 0.60f;
          F = 0.15f + (pk - 0.15f) * (0.5f - 0.5f * cosf(TWO_PI * u));
          if (once(q, br, u > 0.5f)) modeKick(2, 0.04f, 0);
        } else F = 0.15f * max(0.0f, 1 - (t - 6500) / 500);
        if (once(q, 4, t > 3500)) seqBlink(q, now);   // a slow blink on the out-breath
        for (int i = 0; i < 5; i++) s[i].hT = (br == 2 && abs(s[i].k) == 2) ? 0 : crownH(s[i], F, 0.34f * r);   // the last breath only pops three
        q.g = 0.12f * F;
        return t > 7000;
      }
      case SEQ_CALM:
        if (once(q, 0, t > q.hold * 0.6f)) botEyes.blinkStart(now, BW_SWAP);
        if (once(q, 1, t > q.hold)) { calmEnd(q); modeKick(2, 0.07f, 0); modeKick(4, 0.03f, q.axis); }
        return t > q.hold + 400;
      case SEQ_CROWN: {
        float F = t < 1600 ? 0.95f * t / 1600 : t < 2800 ? 0.95f : 0.95f * expf(-(t - 2800) / 60);
        if (once(q, 0, t > 1100)) popS(0.03f);
        if (once(q, 1, t > 2800)) seqBlink(q, now);
        if (once(q, 2, t > 2850)) { modeKick(2, 0.08f, 0); modeKick(3, 0.03f, q.axis); seqFace(q, EXPR_HAPPY, 900); }
        bool hold = t >= 1600 && t < 2800;
        for (int i = 0; i < PARTY_SLOTS; i++) if (s[i].on)
          s[i].hT = crownH(s[i], F, 0.38f * r) * (hold ? 1 + 0.06f * sinf(TWO_PI * 2.2f * t / 1000 + 1.3f * abs(s[i].k)) : 1);
        q.g = 0.12f * F;
        return t > 4000;
      }
      case SEQ_CAROUSEL: {
        float w = t < 400 ? 40 * t / 400 : t < 3400 ? 40 : t < 3800 ? 40 - 90 * (t - 3400) / 400 : t < 5600 ? -50 : t < 6200 ? -50 * (1 - (t - 5600) / 600) : 0;
        float F = t < 400 ? 0.8f * t / 400 : t < 5600 ? 0.8f : t < 6200 ? 0.8f * (1 - (t - 5600) / 600) : 0;
        w *= q.dir;
        // Rotate the vertex grid; every 36° hand each spike to the next slot, so the turn never jumps (spec §5.5)
        if (q.driveAl) {
          q.phi += w * DEG_TO_RAD * dt;
          while (q.phi >= 18 * DEG_TO_RAD) { q.phi -= 36 * DEG_TO_RAD; shiftSlots(b, 7, 1); }
          while (q.phi < -18 * DEG_TO_RAD) { q.phi += 36 * DEG_TO_RAD; shiftSlots(b, 7, -1); }
          if (t < 6200) { al.x = al.t = q.axis + q.phi; al.v = 0; } else q.driveAl = false;
        } else q.phi = wrapPi(al.x - q.axis);
        float lean = 14 * DEG_TO_RAD * constrain(w / 40, -1.0f, 1.0f);
        for (int i = 0; i < 7; i++) { s[i].hT = 0.34f * r * F * powf(winH(q.phi + s[i].off * 3 * DEG_TO_RAD), 0.7f); s[i].lean = lean; }
        float sg = w > 0 ? 1 : w < 0 ? -1 : 0, ca = cosf(q.axis), sa = sinf(q.axis);
        if (q.lead && sg != 0 && t < 6000) look(ca * 0.25f - sa * 0.4f * sg, sa * 0.25f + ca * 0.4f * sg, 300, now);   // the gaze pans with the travel
        if (once(q, 0, t > 3600)) { modeKick(3, 0.03f, q.axis); seqBlink(q, now); }
        if (once(q, 1, t > 6000)) seqFace(q, EXPR_HAPPY, 600);
        q.g = 0.08f * F;
        return t > 6500;
      }
      case SEQ_SPLIT: {
        float trem = 1 + 0.04f * sinf(TWO_PI * 3 * t / 1000);
        s[0].hT = t < 1100 ? 0.48f * r * trem : t < 2000 ? 0.14f * r : 0;
        s[1].hT = s[2].hT = t >= 1100 && t < 2000 ? 0.34f * r * trem : 0;
        s[3].hT = s[5].hT = t >= 2000 && t < 2900 ? 0.24f * r * trem : 0;
        s[4].hT = t >= 2000 && t < 2900 ? 0.20f * r * trem : 0;
        if (once(q, 0, t > 1100)) { modeKick(3, 0.03f, q.axis); seqFace(q, EXPR_SURPRISED, 500); popS(0.02f); }
        if (once(q, 1, t > 2000)) modeKick(3, 0.02f, q.axis);
        if (once(q, 2, t > 2900)) { modeKick(2, 0.06f, 0); seqFace(q, EXPR_HAPPY, 800); }
        q.g = t < 2900 ? 0.10f : 0;
        return t > 3600;
      }
      case SEQ_WAVE: {
        const float passT[3] = {200, 1400, 2600}, passH[3] = {0.32f, 0.32f, 0.24f};
        const int8_t passD[3] = {q.dir, (int8_t)-q.dir, q.dir};
        int np = q.v ? 3 : 2;
        for (int i = 0; i < 7; i++) { s[i].hT = 0; s[i].lean = 0; }
        float crest = 99;
        for (int p = 0; p < np; p++) {
          for (int i = 0; i < 7; i++) {
            int j = passD[p] > 0 ? s[i].k + 3 : 3 - s[i].k;
            float u = (t - passT[p] - j * 90) / 300;
            if (fabsf(u) < 1) {
              float h = passH[p] * r * s[i].env * (0.5f + 0.5f * cosf(PI * u));
              if (h > s[i].hT) { s[i].hT = h; s[i].lean = u < 0 ? 12 * DEG_TO_RAD * passD[p] : 0; }
            }
          }
          float c = (t - passT[p]) / 90 - 3;
          if (c > -3 && c < 3) crest = c * passD[p];
        }
        if (q.lead && crest < 90) {   // the eyes follow the crest
          float a = q.axis + crest * 18 * DEG_TO_RAD;
          look(cosf(a) * 0.5f, sinf(a) * 0.5f, 250, now);
        }
        if (once(q, 0, t > 470)) modeKick(2, 0.03f, 0);
        if (once(q, 1, t > 1670)) modeKick(2, 0.03f, 0);
        float end = q.v ? 3500 : 2300;
        if (once(q, 2, t > end)) seqFace(q, EXPR_HAPPY, 600);
        return t > end + 200;
      }
      case SEQ_MERGE: {
        float wob = 1 + 0.05f * sinf(TWO_PI * 2.4f * t / 1000);
        float s1 = ramp01(t, 0, 500) * (1 - ramp01(t, 1100, 1400)), s2 = ramp01(t, 1100, 1400) * (1 - ramp01(t, 2200, 2500)), s3 = ramp01(t, 2200, 2500) * (1 - ramp01(t, 3400, 3550));
        for (int i = 0; i < 9; i++) {
          int k = s[i].k, a = abs(k);
          float e1 = a == 4 ? 0.6f : 1, e2 = k % 2 == 0 ? (a == 4 ? 0.55f : 1) : 0, e3 = k % 3 == 0 ? (a == 3 ? 0.8f : 1) : 0;
          s[i].hT = max(max(0.15f * r * e1 * s1, 0.30f * r * e2 * s2), 0.46f * r * e3 * s3) * wob;
        }
        if (once(q, 0, t > 1250)) modeKick(2, -0.04f, 0);
        if (once(q, 1, t > 2350)) { modeKick(2, -0.05f, 0); popS(0.03f); }
        if (once(q, 2, t > 3450)) { modeKick(2, 0.08f, 0); modeKick(4, 0.03f, q.axis); seqFace(q, EXPR_HAPPY, 800); seqBlink(q, now); }
        q.g = 0.10f * max(s1, max(s2, s3));
        return t > 4100;
      }
      case SEQ_BLOOM: {
        bool arch = q.v == 2;
        if (once(q, 0, t > 100)) {   // the mirrored pair: same springs, same sway phase, mirrored sway
          float ph = eyeRnd(0, TWO_PI);
          for (int sd = 1; sd >= -1; sd -= 2) {
            PartyTentOpt o;
            o.dir = q.axis + sd * 32 * DEG_TO_RAD; o.L = (arch ? 0.95f : 0.85f) * r; o.kind = PT_SEQ; o.hold = -1;
            o.gf = 1.3f; o.gz = 0.55f; o.curl = arch ? -0.6f : 0.45f; o.swA = 0.12f; o.swF = 0.4f; o.boops = 2;
            o.free = true; o.mirror = true; o.side = sd; o.ph = ph;
            seqTent(q, spawnTent(o, now));
          }
          modeKick(2, 0.06f, q.axis + 32 * DEG_TO_RAD); modeKick(2, 0.06f, q.axis - 32 * DEG_TO_RAD);
        }
        if (q.v == 1 && once(q, 1, t > 250)) {   // triple: a shorter one in the middle instead of the spike
          PartyTentOpt o;
          o.dir = q.axis; o.L = 0.55f * r; o.kind = PT_SEQ; o.hold = -1; o.gf = 1.3f; o.gz = 0.55f; o.curl = 0;
          o.swA = 0.06f; o.swF = 0.4f; o.free = true; o.mirror = true; o.side = 1;
          seqTent(q, spawnTent(o, now));
        }
        if (s[0].on) s[0].hT = t > 500 && t < 2400 ? 0.24f * r * (1 + 0.05f * sinf(TWO_PI * 2.5f * t / 1000)) : 0;   // the stamen
        if (once(q, 2, t > 500)) seqFace(q, EXPR_HAPPY, 1800);
        if (arch && q.nt >= 2) {   // two field lines between poles: the tips never touch
          PartyTent *a = tentOf(q.ti[0], q.tg[0]), *c = tentOf(q.ti[1], q.tg[1]);
          const int tip = PARTY_NN - 1;
          if (a && c && hypotf(a->nx[tip] - c->nx[tip], a->ny[tip] - c->ny[tip]) < 12) {
            if (a->phase != PT_RETRACT) a->lam.t = 0.9f;
            if (c->phase != PT_RETRACT) c->lam.t = 0.9f;
          }
        }
        if (once(q, 3, t > 2300)) for (int i = 0; i < q.nt; i++) { PartyTent *x = tentOf(q.ti[i], q.tg[i]); if (x) retract(*x, false); }
        if (once(q, 4, t > 2700)) { modeKick(2, 0.08f, 0); seqBlink(q, now); seqFace(q, EXPR_HAPPY, 900); }
        return t > 2900;
      }
      case SEQ_FEELERS:
      case SEQ_LONG:
        return (t > 900 && !anyFeeler()) || t > 6500;
      case SEQ_COMPILE: {   // a progress bar: pairs pop from the outside in, the centre last ("built"), then all drop
        static const float H[5] = {0.40f, 0.28f, 0.22f, 0.18f, 0.14f};   // by |k|
        bool drop = t >= 1380, hold = t >= 880 && !drop;
        for (int i = 0; i < 9; i++) {
          int a = abs(s[i].k);
          s[i].hT = drop || t < (4 - a) * 220 ? 0 : H[a] * r * (hold ? 1 + 0.05f * sinf(TWO_PI * 2.2f * t / 1000) : 1);
        }
        for (int a = 4; a >= 0; a--) if (once(q, 4 - a, t >= (4 - a) * 220)) { codeTick(); if (!a) popS(0.03f); }
        if (once(q, 5, drop)) { modeKick(2, 0.07f, 0); seqFace(q, EXPR_HAPPY, 600); }
        q.g = drop ? 0 : 0.08f;
        return t > 2600;
      }
      case SEQ_AHA: {   // a third limb: one short finger up the axis, two boops, back in
        if (once(q, 0, true)) {
          if (liveTents() >= partyTentCap) return true;   // the readers have the pool
          PartyTentOpt o;
          o.dir = q.axis; o.L = 0.7f * r; o.kind = PT_SEQ; o.hold = -1; o.curl = 0; o.swA = 0.06f; o.swF = 0.4f;
          o.gf = 1.3f; o.gz = 0.55f; o.boops = 2; o.free = true; o.mirror = true; o.side = 1;
          seqTent(q, spawnTent(o, now));
          seqLook(q, 0.5f, 1800, now); seqFace(q, EXPR_SURPRISED, 400); popS(0.02f);
        }
        if (once(q, 1, t > 400)) seqFace(q, EXPR_HAPPY, 1200);
        if (once(q, 2, t > 2100)) for (int i = 0; i < q.nt; i++) { PartyTent *x = tentOf(q.ti[i], q.tg[i]); if (x) retract(*x, false); }
        return t > 2700;
      }
      case SEQ_GRAND: {   // pattern library §11: the whole fan nucleates centre-out, trembles, then snaps back
        float F = t < 850 ? min(1.0f, t / 80) : 0;
        bool hold = t >= 150 && t < 750;
        for (int i = 0; i < 9; i++) {
          float h = crownH(s[i], F, 0.55f * r);
          if (abs(s[i].k) == 2 && t >= 150 && t < 300) h *= 0.3f;   // the two that fling the droplets
          s[i].hT = h * (hold ? 1 + 0.05f * sinf(TWO_PI * 2.5f * t / 1000) : 1);
        }
        q.g = 0.14f * F;
        return t > 1300;
      }
      case SEQ_VOICE: {   // spikes ride the speech level (smoothed, so phrases, not syllables)
        float F = 0.15f + 0.065f * lvlSm.x;
        for (int i = 0; i < 5; i++) s[i].hT = q.v ? 0 : crownH(s[i], F, 0.30f * r);
        q.g = q.v ? 0 : 0.10f * F;
        return false;   // the quip beat ends it
      }
    }
    return true;
  }

  void stepSeqs(unsigned long now, float dt) {
    for (int i = 0; i < PARTY_SEQS; i++) {
      PartySeq &q = seqs[i];
      if (!q.on) continue;
      float t = now - q.t0;
      if (stepSeq(q, now, t, dt) || (t > 7000 && q.id != SEQ_VOICE)) endSeq(i, now);
    }
    seqG = 0;
    float kT = 1;
    for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on) { seqG = max(seqG, seqs[i].g); kT = min(kT, seqs[i].kT); }
    kappa.step(kT * (code ? codeKappa : 1), dt);
  }
  void releaseSeq(int qi) {
    if (qi < 0) return;
    PartySeq &q = seqs[qi];
    if (!q.on) return;
    calmEnd(q);
    PartyBank &b = banks[q.bank];
    if (b.owner == qi) { for (int j = 0; j < PARTY_SLOTS; j++) { b.s[j].on = false; b.s[j].hT = 0; b.s[j].lean = 0; } b.owner = -1; }
    q.on = false; q.driveAl = false;
    if (q.lead) for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on) { seqs[i].lead = true; break; }
    if (!nSeqs()) { seqG = 0; alT = alphaHome(); }
  }
  void endSeq(int qi, unsigned long now) {
    uint8_t tierEnded = PARTY_SEQ_DEF[seqs[qi].id].tier;
    if (tierEnded == TIER_B) schBigInStack = true;
    releaseSeq(qi);
    if (nSeqs()) return;   // the rest starts when the whole stack is done
    if (code) { schBigInStack = false; schNextAt = now + (unsigned long)eyeRnd(800, 2000); return; }   // code mode rests briefly
    bool big = schBigInStack;
    schBigInStack = false;
    if (big) tierEnded = TIER_B;
    static const uint16_t restLo[3] = {2000, 3000, 6000}, restHi[3] = {4000, 6000, 10000};
    schNextAt = now + (unsigned long)eyeRnd(restLo[tierEnded], restHi[tierEnded]);
    if (big) { schForceSmall = 2; schCd[SEQ_CALM] = 0; startSeq(SEQ_CALM, now, false, 1000); }   // after a big: a palate cleanser
  }
  void abortOne(int qi) {
    PartySeq &q = seqs[qi];
    for (int i = 0; i < q.nt; i++) { PartyTent *x = tentOf(q.ti[i], q.tg[i]); if (x) retract(*x, true); }
    releaseSeq(qi);
  }
  // A poke, tap, shake, run, exit or request cuts the whole stack short
  void abortSeqs(unsigned long now) {
    if (!nSeqs()) return;
    for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on) abortOne(i);
    schNextAt = now + 4000;
  }

  // ---- Pattern library: scheduler (spec §4) ---------------------------------
  bool eligible(uint8_t id, unsigned long now, bool layering, bool heard) {
    const PartySeqDef &e = PARTY_SEQ_DEF[id];
    if (seqRunning(id)) return false;
    if (e.tents && (!PARTY_TENTS || heard)) return false;   // with music, tentacles belong to the beat grab
    if (heard && !e.spikes) return false;
    int recentM = 0, recentT = 0;
    bool recentB = false;
    for (int i = 0; i < schHistN; i++) {
      if (now - schHist[i].t >= 60000) continue;
      recentM += schHist[i].tier == TIER_M; recentT += schHist[i].tents; recentB |= schHist[i].tier == TIER_B;
    }
    if (layering) return !e.solo && !(e.tents && liveTents() > 3) && !(e.tier == TIER_B && recentB);
    if (id == schLast || !eyeReached(now, schCd[id])) return false;
    if (e.spikes && schSpikeRun >= 2) return false;   // never three spike patterns running
    if (id == SEQ_CALM && liveTents()) return false;
    if (e.tents && (liveTents() > 2 || recentT >= 3)) return false;
    if (e.tier == TIER_M && recentM >= 4) return false;
    if (e.tier == TIER_B && recentB) return false;
    return true;
  }
  int pickSeq(unsigned long now, bool layering, bool heard) {
    float p[3] = {0.45f, 0.5f, 0.05f};   // mediums lead: smalls get lost
    if (layering) { p[0] = 0.1f; p[1] = 0.8f; p[2] = 0.1f; }
    else if (schSmallRun >= 2) p[1] *= 1.6f;
    bool recentM = false;
    for (int i = 0; i < schHistN; i++) recentM |= schHist[i].tier == TIER_M && now - schHist[i].t < 30000;
    if (!recentM) p[2] = 0;   // a big only after a build-up
    if (!layering && schForceSmall) p[1] = p[2] = 0;
    float u = eyeRnd01() * (p[0] + p[1] + p[2]);
    int first = 0;
    for (int k = 0; k < 3; k++) { if (p[k] > 0 && (u -= p[k]) <= 0) { first = k; break; } }
    for (int n = 0; n < 3; n++) {
      int tr = (first + n) % 3;
      if (p[tr] <= 0 && n) continue;
      int wsum = 0;
      for (int id = 0; id < SEQ_PARTY_COUNT; id++) if (PARTY_SEQ_DEF[id].tier == tr && eligible(id, now, layering, heard)) wsum += PARTY_SEQ_DEF[id].w;
      if (!wsum) continue;
      int v = random(wsum);
      for (int id = 0; id < SEQ_PARTY_COUNT; id++) {
        if (PARTY_SEQ_DEF[id].tier != tr || !eligible(id, now, layering, heard)) continue;
        if ((v -= PARTY_SEQ_DEF[id].w) < 0) return id;
      }
    }
    return -1;
  }
  // A new pattern when the stack is empty and the rest is over; with stacking on,
  // a chance to pile another on top a beat after each one starts
  void scheduleSeq(unsigned long now, bool heard) {
    int n = nSeqs();
    if (!n) {
      if (!eyeReached(now, schNextAt)) return;
      int id = pickSeq(now, false, heard);
      if (id >= 0) startSeq(id, now, false); else schNextAt = now + 1000;
    } else if (schLayerArmed && n < PARTY_STACK_MOST[partyStack] && eyeReached(now, schLayerAt) && !seqSolo()) {
      schLayerArmed = false;
      if (eyeRnd01() < PARTY_STACK_CHANCE[partyStack]) { int id = pickSeq(now, true, heard); if (id >= 0) startSeq(id, now, true); }
    }
  }
  // GET /state: what's playing, e.g. "crown+bloom"
  String seqNames() const {
    String s;
    for (int i = 0; i < PARTY_SEQS; i++) if (seqs[i].on) { if (s.length()) s += "+"; s += PARTY_SEQ_DEF[seqs[i].id].key; }
    return s;
  }

  // ---- Code mode "Ink Reader" (docs/vizself/code-mode-motion-spec.md) -------
  // GET /bot/code's requests. bot_mode calls this every frame, before it decides
  // whether the blob draws, so v=1 works with party mode off too.
  void codeRequests(unsigned long now) {
    if (!active && code && !codeOn) codeClear();   // the exit finished while he wasn't drawn
    uint8_t rq = codeReq, hero = codeHeroReq, sq = codeSeqReq;
    uint16_t qm = codeQuipReq;
    if (rq) codeReq = CR_NONE;
    if (hero) { codeHeroReq = CH_NONE; codeHero = hero; codeHeroAt = now; if (!codeOn) rq = CR_START; }
    if (rq == CR_START) codeStart(now);
    if (!codeOn) { codeQuipReq = 0; codeSeqReq = 0; return; }
    if (rq == CR_FOUND || rq == CR_FOUND_SMALL) startFound(now, rq == CR_FOUND_SMALL);
    else if (rq == CR_SHRUG) startShrug(now);
    else if (rq == CR_CANCEL) codeExit(now);
    if (qm) { codeQuipReq = 0; quipMs = qm; quipExpr = codeQuipExpr; if (cPhase == CP_LOOP) startQuip(now, false); }
    if (sq) { codeSeqReq = 0; if (sq <= SEQ_COUNT && cPhase == CP_LOOP) startSeq(sq - 1, now, codeStack > 0); }
  }
  // Party off: the face rounds into the drop where it is. Party on: he flies in from his corner.
  void codeStart(unsigned long now) {
    if (!ready) init();
    bool fromCorner = active && !code && morph != 2 && partyMode && botBackgroundStyle == 4;
    if (!codeOn) { codeT0 = now; codeBackdrop.reset(); quipN = 0; codeVoice = false; }
    codeOn = true;
    codeSignalMs = now;
    if (cPhase == CP_LOOP || cPhase == CP_QUIP || cPhase == CP_ENTER) return;   // already reading: keep going
    cPhase = CP_ENTER; phaseT0 = now; landedAt = 0;
    lastQuipAt = now; quipPeriod = 8000; quipEndAt = 0; quipAudio = false; foundWait = false; quipMs = 0; quipExpr = 0xFF;
    rdI[0] = rdI[1] = -1; rdStep = 0; rdLines = 0; rdRow = -1; rdLeft = 0;
    headAt = 0; ledBeat = CL_NONE; fakeLvl = -1; codeM2 = codeE = codePump = 0; codeKappa = CODE_KAPPA;
    cFace = 0xFF; cFaceAt = 0; ticksLeft = 0;
    schNextAt = now + 2500;
    if (code) { if (!morph && !run) landedAt = now; return; }   // back on mid-exit: bot_mode turns the morph around
    code = true;
    if (fromCorner) {   // the radius grows on the way in (cW)
      codeParty = true;
      abortSeqs(now); retractAll(true);
      run = nullptr;
      if (!morph) startRun(corner, PARTY_CODE, now, 0);
      schNextAt = now + 2500;
    } else {
      codeParty = false;
      if (active) leave(false);   // e.g. halfway through party's own exit: start over from the face
      cW.snap(1); syncTier();
    }
  }
  void codeLanded(unsigned long now) {   // the morph from the face landed: a squash and a thought popping up
    landedAt = now;
    modeKick(2, 0.12f, 0); modeKick(4, 0.03f, 0);
    field(-HALF_PI, 0.36f, 250, now, true);
    botEyes.blinkStart(now, BW_SWAP);
  }
  // Leave code mode. With party on he runs back to his corner; otherwise bot_mode
  // starts the morph back into the face and the centre layout stays until it lands.
  void codeExit(unsigned long now) {
    if (!codeOn) return;
    codeOn = false; cPhase = CP_OFF; foundWait = false;
    ledBeat = CL_NONE; fakeLvl = -1; codeM2 = codeE = codePump = 0; codeKappa = 1;
    abortSeqs(now); retractAll(true);
    rdI[0] = rdI[1] = -1;
    head(0, 0, 1200, now, true);
    if (codeParty && partyMode && botBackgroundStyle == 4 && active && !morph) {
      code = false;
      startRun(corner, PARTY_CODE, now, 0);
      schNextAt = now + 4000;
    }
  }
  void codeClear() { code = false; codeParty = false; codeOn = false; cW.snap(0); syncTier(); cPhase = CP_OFF; mouthOn = false; codeBackdrop.alpha = 0; }

  void codeFace(uint8_t e, unsigned long now) {   // the reading face; a pattern's own face wins while it's up
    if ((e == cFace && now - cFaceAt < 6000) || !eyeReached(now, exprHold)) return;
    exprReq = e; exprMs = 8000; cFace = e; cFaceAt = now;
  }
  void codeTick() { if (code && !mouthOn) sndReq = 1; }   // a woodblock tick (synth only)
  void head(int16_t yaw, int16_t pitch, uint16_t ms, unsigned long now, bool force) {
    if (!force && headAt && now - headAt < 2000) return;   // gentle: >= 2 s apart (brownout)
    headYaw = constrain(yaw, (int16_t)-8, (int16_t)8); headPitch = constrain(pitch, (int16_t)-2, (int16_t)5);
    headMs = ms; headReq = true; headAt = now;
  }
  bool slotUp() const {
    float lim = 0.30f * tier().r;
    for (int b = 0; b < PARTY_BANKS; b++) for (int j = 0; j < PARTY_SLOTS; j++) if (banks[b].s[j].h.x > lim) return true;
    return false;
  }

  void stepCode(unsigned long now, float dt) {
    float e = codeOn ? constrain((now - codeT0) / 45000.0f, 0.0f, 1.0f) : 0;   // effort ramp: the longer, the harder he works
    codeBackdrop.step(dt, CODE_SCROLL * (1 + 0.4f * e), cPhase == CP_FOUND, codeOn);
    if (eyeReached(now, fakeUntil)) fakeLvl = -1;
    else { float u = now / 1000.0f; fakeLvl = (int8_t)constrain(6 + 7 * sinf(TWO_PI * 3.1f * u) * sinf(TWO_PI * 0.7f * u), 0.0f, 12.0f); }
    int lvl = fakeLvl >= 0 ? fakeLvl : talkLvl;
    lvlSm.step(max(0, lvl), dt);
    if (lvl > 0) quipLoudAt = now;
    if (!code) { mouthOn = false; return; }
    // The mouth surfaces in the ink while he talks (party off only, no mouth in party mode)
    mouthOn = !codeParty && (cPhase == CP_QUIP || cPhase == CP_FOUND || !codeOn) && quipLoudAt && now - quipLoudAt < 300;
    if (!codeOn) { codeM2 = codeE = codePump = 0; return; }
    if (talk) codeVoice = true;
    bool timedOut = (long)(now - max((unsigned long)codeSignalMs, codeT0)) > CODE_TIMEOUT_MS;
    if (((codeVoice && !talk) || timedOut) && cPhase != CP_SHRUG && cPhase != CP_FOUND) startShrug(now);   // the turn ended without a result
    codeE = 0.10f + 0.25f * e; codeM2 = 0; codePump = 0; codeKappa = CODE_KAPPA;
    switch (cPhase) {
      case CP_ENTER: {
        if (now - phaseT0 > 200) codeFace(EXPR_FOCUSED, now);
        if (!morph && !run && !landedAt) landedAt = now;
        if (!landedAt) break;
        if (rdI[0] < 0 && now - phaseT0 >= 450 && now - landedAt >= 70) { readersSpawn(now); look(0.4f, -0.3f, 600, now); }
        if (rdI[0] >= 0 && now - phaseT0 >= 600) { cPhase = CP_LOOP; ledBeat = CL_SCAN; head(-6, 0, 1400, now, true); newLine(now, e); }
        break;
      }
      case CP_LOOP:
        stepReaders(now, e);
        if (cPhase != CP_LOOP) break;
        scheduleCode(now);
        if (lvl >= 2 && now - quipEndAt > 500) { quipMs = 0; quipExpr = 0xFF; startQuip(now, true); break; }   // speech without a signal
        if (codeHero && now - codeHeroAt > 400) {
          uint8_t h = codeHero; codeHero = CH_NONE;
          if (h == CH_QUIP) { fakeUntil = now + 300 + 2200; quipMs = 2200; quipExpr = 0xFF; startQuip(now, false); }
          else if (h == CH_FOUND) startFound(now, false);
          else if (h == CH_SHRUG) startShrug(now);
        }
        if (ticksLeft && eyeReached(now, tickAt)) { codeTick(); ticksLeft--; tickAt = now + tickGap; }
        break;
      case CP_QUIP: stepQuip(now, lvl, e); break;
      case CP_FOUND: stepFound(now, lvl); break;
      case CP_SHRUG: if (now - phaseT0 > 900) codeExit(now); break;
    }
    ledLvl = (uint8_t)max(0, lvl);
  }

  // ---- Code mode: the reader pair --------------------------------------------
  // A reader's tip target: d px past the rim on its side, on the row being read
  void readerTip(int sd, float d, float &x, float &y) {
    x = icx + sd * (Reff * cosf(CODE_READ_ROOT) + d);
    y = rdRow >= 0 ? codeBackdrop.rowY(rdRow) + CODE_BAR_H / 2.0f : icy - Reff * sinf(CODE_READ_ROOT);
    onGlass(x, y, 6);
  }
  int32_t nextRow(int32_t cur) {   // the next line down, kept within two rows of the reader roots
    int32_t mid = codeBackdrop.rowAt(icy - Reff * sinf(CODE_READ_ROOT));
    int32_t r = cur < 0 ? mid + (int32_t)random(-1, 2) : cur + 1;
    if (r < mid - 2 || r > mid + 2) r = mid - 1 + (int32_t)random(0, 3);
    for (int k = 0; k < 3 && codeBackdrop.row(r).n == 0; k++) r++;   // skip blank lines
    return max((int32_t)0, r);
  }
  void readersSpawn(unsigned long now) {
    if (rdRow < 0) rdRow = nextRow(-1);
    float ph = eyeRnd(0, TWO_PI);
    for (int s = 0; s < 2; s++) {
      PartyTentOpt o;
      int sd = s ? -1 : 1;
      o.track = true; readerTip(sd, CODE_READ_L0 * tier().r, o.tx, o.ty);
      o.L = capLen(); o.kind = PT_SEQ; o.hold = -1; o.gf = 1.6f; o.gz = 0.5f; o.curl = 0.25f; o.swA = 0; o.swF = 0.4f;
      o.boops = 0; o.free = true; o.mirror = true; o.side = sd; o.ph = ph;
      rdI[s] = spawnTent(o, now); rdG[s] = tentGen;
    }
  }
  PartyTent *reader(int s) { return tentOf(rdI[s], rdG[s]); }
  void readersLam(float lam, float sw) {
    for (int s = 0; s < 2; s++) { PartyTent *t = reader(s); if (t && t->phase != PT_RETRACT) { t->lam.t = lam; t->swA = sw; } }
  }
  // A new line: read (55 %), skim two lines fast (25 %) or stare at one token (20 %)
  void newLine(unsigned long now, float e) {
    if (!rdLeft) { float u = eyeRnd01(); rdVar = u < 0.55f ? RD_READ : u < 0.8f ? RD_SKIM : RD_STARE; rdLeft = rdVar == RD_SKIM ? 2 : 1; }
    rdLeft--;
    rdStep = 0; rdT0 = now; rdSacc = 0; rdBoop = 0; rdLines++;
    rdSide = rdLines & 1;   // the eyes read one side per line, alternating; the body stays symmetric
    rdMs = (rdVar == RD_SKIM ? 600 : rdVar == RD_STARE ? 2200 : CODE_READ_LINE_MS) * (1 - 0.25f * e);
    rdStop = eyeRnd(0.35f, 0.7f);
    readersLam(1, 0);
    if (eyeRnd01() < 0.5f) { ticksLeft = 3 + random(5); tickGap = (uint16_t)(1000 / eyeRnd(3, 6)); tickAt = now + 120; }
    if (rdLines % 2 == 0) head(rdSide ? 6 : -6, 0, 1400, now, false);   // toward the side being read
    codeFace(slotUp() ? EXPR_THINKING : EXPR_FOCUSED, now);
    ledPos = 0;
  }
  void stepReaders(unsigned long now, float e) {
    PartyTent *a = reader(0), *b = reader(1);
    if (!a || !b || a->phase == PT_RETRACT || b->phase == PT_RETRACT) {   // a poke or a shake took them: regrow
      if (liveTents() + 2 <= partyTentCap) { readersSpawn(now); newLine(now, e); }
      return;
    }
    const float r = tier().r;
    float u = (now - rdT0) / max(1.0f, rdMs), p;
    bool up = slotUp();
    if (up) { look(0, -0.4f, 300, now); codeFace(EXPR_THINKING, now); }   // eyes up at a crown
    if (rdStep == 0) {   // the sweep
      if (rdVar == RD_STARE) {   // to a token, hold it a second with two boops, then on
        if (u < 0.32f) p = rdStop * eyeSmooth(u / 0.32f);
        else if (u < 0.77f) {
          p = rdStop;
          if (rdBoop < 2 && u > 0.36f + 0.2f * rdBoop) { rdBoop++; a->bulb.v += 0.4f / pkBulb; b->bulb.v += 0.4f / pkBulb; }
        } else p = rdStop + (1 - rdStop) * eyeSmooth(min(1.0f, (u - 0.77f) / 0.23f));
      } else p = eyeSmooth(min(1.0f, u));
      // Reading saccades along the line on one side (a skim is one long one)
      int want = rdVar == RD_SKIM ? (u < 0.5f ? 1 : 3) : min(3, 1 + (int)(u * 3));
      if (want > rdSacc) {
        rdSacc = want;
        float pad = (0.15f + 0.2f * (rdSacc - 1)) * (rdSide ? -1 : 1);
        float py = constrain((codeBackdrop.rowY(rdRow) + 3 - icy) / 60.0f, -0.6f, 0.6f);
        if (!up) look(pad, py, (unsigned long)rdMs, now);
        ledPos = rdSacc;
      }
      if (u >= 1) {   // carriage return: pen up, back to the start, next row
        rdStep = 1; rdT0 = now;
        readersLam(0.55f, 0);
        if (!up) look(0.1f * (rdSide ? 1 : -1), -0.2f, CODE_READ_CR_MS + 200, now);
        if (eyeRnd01() < 0.3f) botEyes.blinkStart(now, BW_SWAP);
        ledPos = 0;
      }
    } else {
      p = 1 - eyeSmooth(min(1.0f, (now - rdT0) / (float)CODE_READ_CR_MS));
      if (now - rdT0 >= CODE_READ_CR_MS) { rdRow = nextRow(rdRow); newLine(now, e); p = 0; }
    }
    float d = (CODE_READ_L0 + (CODE_READ_L1 - CODE_READ_L0) * p) * r;
    readerTip(1, d, a->tx, a->ty);
    readerTip(-1, d, b->tx, b->ty);
    if (rdStep == 0) codeBackdrop.row(rdRow).hl = 1;   // the line being read glows (400 ms trail)
  }

  // ---- Code mode: the playlist (spec §2.2) -----------------------------------
  bool codeEligible(uint8_t id, unsigned long now, bool layering) const {
    const PartySeqDef &e = PARTY_SEQ_DEF[id];
    if (seqRunning(id) || !eyeReached(now, schCd[id])) return false;
    if (id == SEQ_AHA && liveTents() >= partyTentCap) return false;   // the readers have the pool
    if (layering) return !e.solo;
    if (id == schLast) return false;
    return !(e.spikes && schSpikeRun >= 2);   // never a third spike pattern in a row
  }
  int pickCode(unsigned long now, bool layering, bool quipSoon) {
    uint8_t tr = eyeRnd01() < 0.35f || quipSoon ? TIER_S : TIER_M;   // no medium start right before a quip
    for (int pass = 0; pass < 2; pass++, tr = tr == TIER_S ? TIER_M : TIER_S) {
      if (tr == TIER_M && quipSoon) continue;
      int wsum = 0;
      for (unsigned k = 0; k < CODE_SEQS; k++) { const CodeSeqDef &d = CODE_SEQ_DEF[k]; if (PARTY_SEQ_DEF[d.id].tier == tr && codeEligible(d.id, now, layering)) wsum += d.w; }
      if (!wsum) continue;
      int v = random(wsum);
      for (unsigned k = 0; k < CODE_SEQS; k++) {
        const CodeSeqDef &d = CODE_SEQ_DEF[k];
        if (PARTY_SEQ_DEF[d.id].tier != tr || !codeEligible(d.id, now, layering)) continue;
        if ((v -= d.w) < 0) return k;
      }
    }
    return -1;
  }
  void scheduleCode(unsigned long now) {
    unsigned long due = lastQuipAt + quipPeriod;   // the next quip, from the learned period
    bool quipSoon = (long)(now + 2500 - due) > 0 && (long)(now - due) < 1500;
    uint8_t most = codeStack == 0 ? 1 : (codeStack == 2 || now - codeT0 > 15000) ? 3 : 2;
    float chance = codeStack == 0 ? 0 : codeStack == 2 ? 0.75f : 0.6f;
    int n = nSeqs(), k = -1;
    bool layer = false;
    if (!n) {
      if (!eyeReached(now, schNextAt)) return;
      k = pickCode(now, false, quipSoon);
      if (k < 0) { schSpikeRun = 0; schNextAt = now + 800; return; }
    } else if (schLayerArmed && n < most && eyeReached(now, schLayerAt) && !seqSolo()) {
      schLayerArmed = false;
      if (eyeRnd01() >= chance || (k = pickCode(now, true, quipSoon)) < 0) return;
      layer = true;
    } else return;
    const CodeSeqDef &d = CODE_SEQ_DEF[k];
    startSeq(d.id, now, layer);
    schCd[d.id] = now + d.cdS * 1000UL;
  }

  // ---- Code mode: quips (spec §2.3) ------------------------------------------
  void startQuip(unsigned long now, bool heard) {
    cPhase = CP_QUIP; phaseT0 = quipAt = now; quipAudio = false;
    if (lastQuipAt && now - lastQuipAt > 3000) quipPeriod = constrain(now - lastQuipAt, 4000UL, 20000UL);
    lastQuipAt = now; quipN++;
    abortSeqs(now);   // the running patterns slump: he stops to tell you something
    readersLam(0.3f, 0.08f);   // pens down: short nubs that sway, mirrored
    uint8_t ex = quipExpr < BOT_NUM_EXPRESSIONS ? quipExpr : EXPR_HAPPY;
    botEyes.blinkStart(now, BW_SWAP);
    expr(ex, (uint16_t)(min((uint16_t)20000, quipMs ? quipMs : (uint16_t)3000) + 1500));
    look(0, -0.1f, 4000, now);
    head(0, 3, 1000, now, true);
    ledBeat = CL_QUIP;
    ticksLeft = 0;
    if (heard) quipOnset(now);
  }
  bool quipSharp() const { return quipExpr == EXPR_MISCHIEF || quipExpr == EXPR_SASSY || quipExpr == EXPR_DEVIOUS || quipExpr == EXPR_ANGRY; }
  void quipOnset(unsigned long now) {
    quipAudio = true; quipHeardAt = quipLoudAt = now;
    modeKick(2, 0.06f, 0);   // a breath in
    startSeq(SEQ_VOICE, now, false, quipSharp() ? 1 : 0);   // a sharp face gets a pump, never spikes
  }
  void stepQuip(unsigned long now, int lvl, float e) {
    codeKappa = 0.6f;
    if (!quipAudio) {
      if (lvl >= 2) quipOnset(now);
      else if (now - quipAt > 1500) { endQuip(now, false, e); return; }   // no audio came: back to reading quietly
      if (!quipAudio) return;
    }
    float L = lvlSm.x;
    codeM2 = max(-0.10f, -0.010f * L);   // taller when loud
    codeE += 0.02f * L;
    if (quipSharp()) codePump = 0.006f * L;
    bool quiet = now - quipLoudAt > 350 && now - quipHeardAt > 400;
    bool over = quipMs && now - quipHeardAt > quipMs + 600UL;
    if (quiet || over || (foundWait && eyeReached(now, foundWaitUntil))) endQuip(now, true, e);
  }
  void endQuip(unsigned long now, bool spoke, float e) {
    abortSeqs(now);   // the voice crown drops
    if (spoke) {   // ba-dum
      modeKick(2, 0.08f, 0); modeKick(4, 0.03f, 0);
      botEyes.blinkStart(now, BW_SWAP);
      if (quipN % 3 == 0) sndReq = 3;   // rimshot (synth only)
    }
    quipMs = 0; quipExpr = 0xFF; quipEndAt = now;
    cPhase = CP_LOOP; ledBeat = CL_SCAN;
    schNextAt = now + 1300;
    rdStep = 1; rdT0 = now;   // regrow onto the next row through a carriage return
    cFace = 0xFF;
    if (foundWait) startFound(now, false);
  }

  // ---- Code mode: "found it" (spec §2.4) and the shrug -----------------------
  void startFound(unsigned long now, bool small) {
    if (small) {   // "found one": a compile and a flash of the line; reading goes on
      if (cPhase == CP_LOOP) { startSeq(SEQ_COMPILE, now, false); schCd[SEQ_COMPILE] = now + 14000; if (rdRow >= 0) codeBackdrop.row(rdRow).hl = 2.5f; }
      return;
    }
    if (cPhase == CP_FOUND || cPhase == CP_SHRUG) return;
    if (cPhase == CP_QUIP && quipAudio && !foundWait) { foundWait = true; foundWaitUntil = now + 1500; return; }   // let the joke land
    foundWait = false;
    cPhase = CP_FOUND; phaseT0 = now; fdF = 0;
    abortSeqs(now);
    if (rdRow < 0) rdRow = nextRow(-1);
    codeBackdrop.foundRow = rdRow;
    for (int s = 0; s < 2; s++) {   // whip out to full reach on the grab spring
      PartyTent *t = reader(s);
      if (!t || t->phase == PT_RETRACT) continue;
      t->L = capLen(); t->lam.t = 1; t->lam.f = 3.0f; t->lam.z = 0.6f; t->swA = 0;
    }
    botEyes.blinkStart(now, BW_SWAP); expr(EXPR_SURPRISED, 900); popS(0.04f);
    head(0, 5, 500, now, true);   // perk up
    modeKick(2, -0.10f, 0);   // crouch
    ledBeat = CL_NONE; ticksLeft = 0;
  }
  void stepFound(unsigned long now, int lvl) {
    unsigned long t = now - phaseT0;
    PartyTent *a = reader(0), *b = reader(1);
    if (!(fdF & 4)) {   // the tips hold the line's two ends
      if (a) readerTip(1, capLen(), a->tx, a->ty);
      if (b) readerTip(-1, capLen(), b->tx, b->ty);
    }
    const int tip = PARTY_NN - 1;
    codeBackdrop.fx0 = a && a->on ? a->nx[tip] : icx;
    codeBackdrop.fx1 = b && b->on ? b->nx[tip] : icx;
    if (!(fdF & 1) && t >= 150) { fdF |= 1; startSeq(SEQ_GRAND, now, false); sndReq = 2; ledBeat = CL_FOUND; ledAt = now; }
    if (!(fdF & 2) && t >= 300) {   // a mirrored droplet pair flung from the tips
      fdF |= 2;
      for (int s = 0; s < 2; s++) {
        PartyTent *x = reader(s);
        if (!x) continue;
        float ang = atan2f(x->ny[tip] - x->ny[tip - 1], x->nx[tip] - x->nx[tip - 1]);
        spawnDrop(x->nx[tip], x->ny[tip], 180 * cosf(ang), 180 * sinf(ang), 0.09f * tier().r, now, false);
      }
    }
    if (!(fdF & 4) && t >= 900) { fdF |= 4; if (a) retract(*a, true); if (b) retract(*b, true); }   // the yank: the line goes in
    if (!(fdF & 8) && t >= 1000) { fdF |= 8; modeKick(2, 0.20f, 0); modeKick(4, 0.05f, 0); expr(EXPR_PROUD, 1200); }
    if (t >= 1400 || (t >= 600 && lvl >= 2)) codeExit(now);   // into the answer
  }
  void startShrug(unsigned long now) {   // no result: readers in, a calm dome, a confused look, out
    if (cPhase == CP_SHRUG) return;
    cPhase = CP_SHRUG; phaseT0 = now; foundWait = false;
    retractAll(true);
    rdI[0] = rdI[1] = -1;
    startSeq(SEQ_CALM, now, false, 1);
    expr(EXPR_CONFUSED, 800);
    ledBeat = CL_SHRUG; ledAt = now; ticksLeft = 0;
  }
  static const char *codePhaseName(uint8_t p) {
    static const char *names[] = {"off", "enter", "loop", "quip", "found", "shrug"};
    return p < 6 ? names[p] : "?";
  }
  // Where the readers' tips are, and the found line's ends (for tuning)
  String tipsJson() const {
    String j = ",\"tips\":[";
    for (int s = 0; s < 2; s++) {
      int8_t i = rdI[s];
      const PartyTent *t = i >= 0 && i < PARTY_TENTS && tents[i].on && tents[i].gen == rdG[s] ? &tents[i] : nullptr;
      j += String(s ? "," : "") + (t ? "[" + String((int)t->nx[PARTY_NN - 1]) + "," + String((int)t->ny[PARTY_NN - 1]) + "," + String(t->lam.x, 2) + "]" : "null");
    }
    return j + "],\"line\":[" + String((int)codeBackdrop.fx0) + "," + String((int)codeBackdrop.fx1) + "," + String(codeBackdrop.foundRow) + "]";
  }
  String codeJson(unsigned long now) const {
    static const char *vars[] = {"read", "skim", "stare"};
    bool exiting = !codeOn && code;
    return String("{\"on\":") + (codeOn ? "true" : "false") + ",\"phase\":\"" + (exiting ? "exit" : codePhaseName(cPhase)) + "\"" +
           ",\"t\":" + String(codeOn ? now - codeT0 : 0) + ",\"party\":" + (codeParty ? "true" : "false") +
           ",\"seqs\":\"" + seqNames() + "\",\"reader\":\"" + vars[rdVar % 3] + "\",\"quips\":" + String(quipN) +
           ",\"quipPeriod\":" + String(quipPeriod) + ",\"why\":\"" + codeWhy + "\"" + tipsJson() + "}";
  }

  // ---- Tentacles (motion spec §2) ---------------------------------------------
  // A pose curve says where each node wants to be; the nodes chase it on springs
  // that soften toward the tip, so motion travels outward along the tentacle.
  int spawnTent(const PartyTentOpt &o, unsigned long now) {
    int idx = -1;
    if (liveTents() < partyTentCap)
      for (int i = 0; i < PARTY_TENTS; i++) if (!tents[i].on) { idx = i; break; }
    if (idx < 0) {   // full: reuse the oldest feeler or streamer
      for (int i = 0; i < PARTY_TENTS; i++) {
        PartyTent &c = tents[i];
        if (c.on && (c.kind == PT_FEELER || c.kind == PT_STREAMER) && (idx < 0 || c.gen < tents[idx].gen)) idx = i;
      }
    }
    if (idx < 0) return -1;
    PartyTent &t = tents[idx];
    float cx = icx, cy = icy;
    float a = o.track ? atan2f(o.ty - cy, o.tx - cx) : o.dir;
    float th0 = o.free ? a : clampCone(a, 70);
    t.on = true; t.kind = o.kind; t.phase = PT_GROW; t.gen = ++tentGen;
    t.track = o.track; t.tx = o.tx; t.ty = o.ty; t.dirA = a; t.free = o.free;
    t.L = min(o.L > 0 ? o.L : (float)tier().r, capLen()); t.Lc = 0;
    t.holdMs = o.hold;
    t.lam.init(0, o.gf, o.gz); t.lam.t = 1;
    t.th.init(th0, 0.8f, 0.9f); t.aim.init(th0, 1.4f, 0.8f);
    t.side = o.side != 0 ? o.side : (wrapPi(a - alphaHome()) >= 0 ? 1 : -1);
    t.curlT = o.curl * t.side; t.curl.init(1.6f * t.side, 1.2f, 0.5f);
    t.swA = o.swA; t.swF = o.swF; t.swPh = o.ph >= 0 ? o.ph : eyeRnd(0, TWO_PI); t.mirror = o.mirror;
    t.soft = o.soft; t.boops = o.boops; t.pinching = t.pinchArmed = false; t.bulbless = now; t.bulbR = 4;
    t.bulb.init(1, 2, 0.4f); t.neck.init(1, 6, 1);
    float rb = radiusAt(th0, Reff), c = cosf(th0), s = sinf(th0);
    for (int i = 0; i < PARTY_NN; i++) { t.nx[i] = cx + rb * c; t.ny[i] = cy + rb * s; t.vx[i] = t.vy[i] = 0; }
    modeKick(2, 0.06f, th0);
    return idx;
  }
  void retract(PartyTent &t, bool fast) {
    if (!t.on || t.phase == PT_RETRACT) return;
    t.phase = PT_RETRACT;
    t.curlT = 2.2f * t.side;
    t.dirA = t.aim.x;
    t.L = max(1.0f, t.Lc / max(0.05f, t.lam.x));   // the length it has now, so it doesn't jump
    t.lam.snap(min(1.0f, t.lam.x)); t.lam.t = 0;
    t.lam.f = fast ? 4 : 2.4f; t.lam.z = fast ? 0.8f : 0.75f;
    t.track = false; t.free = true;
  }
  void retractAll(bool fast) { for (int i = 0; i < PARTY_TENTS; i++) retract(tents[i], fast); }

  // Keep a point on the glass: m px in from the edges and inside the rounded corners
  static void onGlass(float &x, float &y, float m) {
    x = constrain(x, m, LCD_WIDTH - m); y = constrain(y, m, LCD_HEIGHT - m);
    #if PARTY_GLASS_R > 0
    const float r = PARTY_GLASS_R;
    float cx = x < r ? r : (x > LCD_WIDTH - r ? LCD_WIDTH - r : -1), cy = y < r ? r : (y > LCD_HEIGHT - r ? LCD_HEIGHT - r : -1);
    if (cx >= 0 && cy >= 0) {
      float dx = x - cx, dy = y - cy, d = hypotf(dx, dy), lim = r - m;
      if (d > lim) { x = cx + dx / d * lim; y = cy + dy / d * lim; }
    }
    #endif
  }

  void stepTent(int idx, unsigned long now, float dt) {
    PartyTent &t = tents[idx];
    const float cx = icx, cy = icy;
    float aimA = t.dirA, Lt = t.L, thT = t.dirA;
    if (t.track) {
      float n1x = t.nx[1], n1y = t.ny[1];
      aimA = atan2f(t.ty - n1y, t.tx - n1x);
      Lt = constrain(hypotf(t.tx - n1x, t.ty - n1y) - 6, 0.0f, t.L);
      thT = atan2f(t.ty - cy, t.tx - cx);
    }
    if (!t.free) thT = clampCone(thT, 70);
    t.th.step(t.th.x + wrapPi(thT - t.th.x), dt);
    t.aim.step(t.aim.x + wrapPi(aimA - t.aim.x), dt);
    t.lam.step(t.lam.t, dt);
    t.curl.step(t.curlT, dt);
    t.bulb.step(1, dt);
    t.neck.step(t.pinching ? 0 : 1, dt);

    // Lifecycle: grow, hold (with a boop or two), retract
    if (t.phase == PT_GROW && t.lam.x > 0.9f) {
      t.phase = PT_HOLD;
      if (t.holdMs >= 0) t.holdUntil = now + t.holdMs;
      for (int b = 0; b < t.boops; b++) later(now, 30 + b * 600, PE_BOOP, idx, t.gen);
    }
    if (t.phase == PT_HOLD && t.holdMs >= 0 && eyeReached(now, t.holdUntil)) retract(t, false);
    // A flick pinches off its bulb while it whips back
    if (t.pinchArmed && !t.pinching && t.lam.x > 0.3f && eyeReached(now, t.pinchAt)) t.pinching = true;
    if (t.pinching && t.neck.x < 0.15f) {
      t.pinching = t.pinchArmed = false;
      const int tip = PARTY_NN - 1;
      float a = atan2f(t.ny[tip] - t.ny[tip - 1], t.nx[tip] - t.nx[tip - 1]), v = t.pinchV, c = cosf(a), s = sinf(a);
      spawnDrop(t.nx[tip], t.ny[tip], c * v + t.vx[tip] * 0.4f, s * v + t.vy[tip] * 0.4f, t.bulbR * 0.95f, now, false);
      if (eyeRnd01() < 0.4f) spawnDrop(t.nx[tip - 1], t.ny[tip - 1], c * v * 0.5f, s * v * 0.5f, 2, now, true);
      t.bulbless = now + 150; t.neck.snap(1); t.phase = PT_HOLD;
      retract(t, false);
      t.lam.f = 3.5f; t.lam.z = 0.5f;
    }
    if (t.phase == PT_RETRACT && t.lam.x < 0.08f) {   // drained back in: the body takes the volume
      t.on = false;
      modeKick(2, -0.04f, t.th.x); modeKick(3, 0.03f, t.th.x);
      E.v += 0.1f / pkE;
      return;
    }

    // Pose curve ψ(s) = aim + curl s² + sway; nodes 0 and 1 ride the rim
    float th = t.th.x, rb = radiusAt(th, Reff), ca = cosf(th), sa = sinf(th);
    t.nx[0] = cx + 0.8f * rb * ca; t.ny[0] = cy + 0.8f * rb * sa;
    t.nx[1] = cx + rb * ca; t.ny[1] = cy + rb * sa;
    t.Lc = max(0.0f, t.lam.x) * Lt;
    float seg = t.Lc / 8, swA = t.phase == PT_RETRACT ? 0 : t.swA;
    float qx = t.nx[1], qy = t.ny[1];
    int n = max(1, (int)ceilf(dt / 0.008f));
    float h = dt / n;
    for (int i = 2; i < PARTY_NN; i++) {
      float s = (i - 1.5f) / 8, psi = t.aim.x + t.curl.x * s * s + swA * (t.mirror ? t.side : 1) * s * sinf(TWO_PI * t.swF * this->t - 2.5f * s + t.swPh);
      qx += seg * cosf(psi); qy += seg * sinf(psi);
      float u = i / (float)(PARTY_NN - 1);
      float f = t.soft ? 1.4f + (0.8f - 1.4f) * u : 3.2f * (1 - 0.5f * u), z = t.soft ? 0.3f : 0.6f - 0.25f * u, w = TWO_PI * f;
      for (int k = 0; k < n; k++) {
        t.vx[i] += (w * w * (qx - t.nx[i]) - 2 * z * w * t.vx[i]) * h; t.nx[i] += t.vx[i] * h;
        t.vy[i] += (w * w * (qy - t.ny[i]) - 2 * z * w * t.vy[i]) * h; t.ny[i] += t.vy[i] * h;
      }
    }
    // Length pass from the root out, then keep every node on the glass
    for (int i = 2; i < PARTY_NN; i++) {
      float dx = t.nx[i] - t.nx[i - 1], dy = t.ny[i] - t.ny[i - 1], d = max(1e-6f, hypotf(dx, dy));
      float dd = constrain(d, 0.85f * seg, 1.15f * seg);
      if (dd != d) { t.nx[i] = t.nx[i - 1] + dx / d * dd; t.ny[i] = t.ny[i - 1] + dy / d * dd; }
      onGlass(t.nx[i], t.ny[i], 4);
    }
  }

  // Width profile (art direction): plump root with a flare into its mound, a
  // round bulb at the tip; short tentacles are thinner. Once per frame.
  void prepTent(PartyTent &t, unsigned long now) {
    const float r = tier().r;
    float w0 = (t.kind == PT_FLICK || t.kind == PT_STREAMER ? 0.22f : 0.275f) * r * PARTY_CHUNK;
    float wTip = max(2.5f, 0.07f * r) * PARTY_CHUNK;
    float wS = 0.45f + 0.55f * constrain(t.Lc / (0.3f * r), 0.0f, 1.0f);
    float b = max(3.0f, 0.11f * r) * PARTY_BULB * max(0.6f, t.bulb.x) * (t.Lc > 0.5f * r ? 1.15f : 1) * PARTY_CHUNK;
    if ((long)(now - t.bulbless) < 0) b = wTip * 0.6f;
    t.bulbR = b;
    for (int i = 0; i < PARTY_NN; i++) {
      float s = max(0.0f, (i - 1) / 8.0f), u = 1 - s;
      float hw = wTip + (w0 - wTip) * powf(u, 1.6f) + (s < 0.15f ? 0.35f * w0 * sq(1 - s / 0.15f) : 0);
      if (i == 0) hw = w0 * 1.3f;
      if (i == PARTY_NN - 2) hw = min(hw, 0.8f * b * t.neck.x + 0.3f);
      t.hw[i] = hw * wS;
      int a = max(0, i - 1), c = min(PARTY_NN - 1, i + 1);
      float dx = t.nx[c] - t.nx[a], dy = t.ny[c] - t.ny[a], d = hypotf(dx, dy);
      if (d < 1e-4f) { t.ux[i] = cosf(t.th.x); t.uy[i] = sinf(t.th.x); } else { t.ux[i] = dx / d; t.uy[i] = dy / d; }
    }
    t.bulbR = b * wS;
    float A = PI * b * b;
    for (int i = 1; i < PARTY_NN - 1; i++) A += (t.hw[i] + t.hw[i + 1]) * hypotf(t.nx[i + 1] - t.nx[i], t.ny[i + 1] - t.ny[i]);
    t.area = A;
  }
  // The polygon around the chain, grown by grow px, into partyTX/TY: left side, bulb, right side back
  void buildTent(const PartyTent &t, float grow) {
    int k = 0;
    for (int i = 0; i < PARTY_NN - 1; i++) {
      float w = t.hw[i] + grow;
      partyTX[k] = t.nx[i] - t.uy[i] * w; partyTY[k] = t.ny[i] + t.ux[i] * w; k++;
    }
    const int tip = PARTY_NN - 1;
    float br = t.bulbR + grow, bx = t.nx[tip], by = t.ny[tip];
    float c = -t.uy[tip], s = t.ux[tip];   // tangent turned +90°, then 8 steps of -22.5°
    const float sc = 0.92388f, ss = -0.38268f;
    for (int j = 0; j <= 8; j++) {
      partyTX[k] = bx + br * c; partyTY[k] = by + br * s; k++;
      float c2 = c * sc - s * ss; s = c * ss + s * sc; c = c2;
    }
    for (int i = PARTY_NN - 2; i >= 0; i--) {
      float w = t.hw[i] + grow;
      partyTX[k] = t.nx[i] + t.uy[i] * w; partyTY[k] = t.ny[i] - t.ux[i] * w; k++;
    }
  }

  // ---- Droplets (motion spec §2.6) --------------------------------------------
  void spawnDrop(float x, float y, float vx, float vy, float r, unsigned long now, bool sat) {
    for (int i = 0; i < PARTY_DROPS; i++) {
      PartyDrop &d = drops[i];
      if (d.on) continue;
      d.on = true; d.sat = sat; d.bridge = false; d.phase = 0;
      d.x = x; d.y = y; d.vx = vx; d.vy = vy; d.r = max(sat ? 2.0f : 2.5f, r); d.gap = 99; d.th = 0; d.rim = Reff;
      d.t0 = now; d.flyMs = sat ? 180 : (unsigned long)eyeRnd(250, 450);
      d.rs.init(d.r, 2.6f, 1);
      return;
    }
  }
  void stepDrop(PartyDrop &d, unsigned long now, float dt) {
    const float cx = icx, cy = icy;
    d.th = atan2f(d.y - cy, d.x - cx);
    d.rim = radiusAt(d.th, Reff);
    d.gap = hypotf(d.x - cx, d.y - cy) - d.rim - d.r;
    float c = cosf(d.th), s = sinf(d.th);
    if (d.phase == 0) {   // flying: drag, and a boing off the bezel
      float k = expf(-3 * dt);
      d.vx *= k; d.vy *= k;
      float x = d.x + d.vx * dt, y = d.y + d.vy * dt, gx = x, gy = y;
      onGlass(gx, gy, d.r + 3);
      if (gx != x) d.vx = -d.vx * 0.4f;
      if (gy != y) d.vy = -d.vy * 0.4f;
      d.x = gx; d.y = gy;
      if (now - d.t0 > d.flyMs) {
        d.phase = 1;
        d.rx.init(d.x, 1.4f, 0.55f); d.rx.v = d.vx;
        d.ry.init(d.y, 1.4f, 0.55f); d.ry.v = d.vy;
      }
    } else if (d.phase == 1) {   // flowing back to the rim, faster if it dawdles
      if (now - d.t0 > 2000) { d.rx.f = d.ry.f = 2.5f; d.rx.z = d.ry.z = 0.6f; }
      float px = d.x, py = d.y;
      d.x = d.rx.step(cx + (d.rim - 2) * c, dt); d.y = d.ry.step(cy + (d.rim - 2) * s, dt);
      d.vx = (d.x - px) / dt; d.vy = (d.y - py) / dt;
      if (d.gap < 1) { d.phase = 2; d.rs.snap(d.r); }
    } else {   // absorbed: shrinks into the rim, which wobbles
      d.r = d.rs.step(0, dt);
      d.x += (cx + (d.rim - d.r) * c - d.x) * 0.3f; d.y += (cy + (d.rim - d.r) * s - d.y) * 0.3f;
      if (d.r < 0.6f) { d.on = false; modeKick(3, 0.04f, d.th); E.v += 0.15f / pkE; }
    }
  }
  // Ellipse stretched along the velocity into partyDX/DY; once close, a bridge to the rim into partyBX/BY
  void buildDrop(PartyDrop &d, float grow) {
    const float rT = tier().r;
    float sp = hypotf(d.vx, d.vy), e = 1 + 0.35f * min(1.0f, sp / (4 * rT)), a = atan2f(d.vy, d.vx);
    float ra = d.r * e + grow, rb = d.r / e + grow, ca = cosf(a), sa = sinf(a);
    for (int i = 0; i < 12; i++) {
      float x = ra * partyCos30[i], y = rb * partySin30[i];
      partyDX[i] = d.x + x * ca - y * sa; partyDY[i] = d.y + x * sa + y * ca;
    }
    d.bridge = d.phase != 0 && d.gap < 0.3f * rT;
    if (!d.bridge) return;
    float k = constrain(1 - d.gap / (0.3f * rT), 0.0f, 1.0f), c = cosf(d.th), s = sinf(d.th);
    float rx = icx + (d.rim - 2) * c, ry = icy + (d.rim - 2) * s, nx = -s, ny = c;
    float wm = 0.6f * d.r * sqrtf(k) + grow, wd = d.r * 0.85f + grow, wr = wm * 1.7f;
    float mx = (d.x + rx) / 2, my = (d.y + ry) / 2;
    partyBX[0] = d.x + nx * wd; partyBY[0] = d.y + ny * wd;
    partyBX[1] = mx + nx * wm;  partyBY[1] = my + ny * wm;
    partyBX[2] = rx + nx * wr;  partyBY[2] = ry + ny * wr;
    partyBX[3] = rx - nx * wr;  partyBY[3] = ry - ny * wr;
    partyBX[4] = mx - nx * wm;  partyBY[4] = my - ny * wm;
    partyBX[5] = d.x - nx * wd; partyBY[5] = d.y - ny * wd;
  }
  // Every tentacle, droplet and bridge into a raster, grown by grow px and shifted by (ox, oy)
  bool addSmall(PartyRaster &ras, float grow, float ox, float oy) {
    bool any = false;
    for (int i = 0; i < PARTY_TENTS; i++) {
      PartyTent &t = tents[i];
      if (!t.on || t.Lc <= 0.5f) continue;
      buildTent(t, grow);
      ras.add(partyTX, partyTY, PARTY_TP, ox, oy);
      any = true;
    }
    for (int i = 0; i < PARTY_DROPS; i++) {
      PartyDrop &d = drops[i];
      if (!d.on || d.r <= 0.5f) continue;
      buildDrop(d, grow);
      ras.add(partyDX, partyDY, 12, ox, oy);
      if (d.bridge) ras.add(partyBX, partyBY, 6, ox, oy);
      any = true;
    }
    return any;
  }

  // ---- Motion ---------------------------------------------------------------
  void step(unsigned long now) {
    float dt = min(0.05f, (now - lastMs) / 1000.0f);
    lastMs = now;
    if (dt <= 0) return;
    t = eyeSecs(now);
    cW.step(code ? 1 : 0, dt);
    syncTier();
    runEvents(now);
    if (morph) stepMorph(now, dt);
    const float R = morphR(), r = tier().r;
    bool free = !run && !morph;   // settled in his corner
    if (free && !code) heroRequest(now);
    stepSeqs(now, dt);

    // Corner run: crouch for the lead-in, then follow an arc to the new corner's home
    free = !run && !morph;
    if (run) stepRun(now, dt);
    else if (holdCorner || code) { if (eyeReached(now, nextRoam)) nextRoam = now + 4000; }   // a roam waits until 4 s after the turn
    else if (free && eyeReached(now, nextRoam) && !nSeqs()) roam(now);   // a roam waits for the patterns to finish
    free = !run && !morph;

    if (padUntil && eyeReached(now, padUntil)) { padUntil = 0; if (!run && !morph) botEyes.padX = botEyes.padY = 0; }
    if (free && !code && eyeReached(now, nextFidget)) fidget(now);

    // Wander: a new spot in the corner box every 3.5-8 s; half the long moves are a scoot hop
    if (free && !code && eyeReached(now, nextWander) && !seqHolds()) {
      float x0, x1, y0, y1;
      box(corner, x0, x1, y0, y1);
      float nx = eyeRnd(min(x0, x1), max(x0, x1)), ny = eyeRnd(min(y0, y1), max(y0, y1));
      if (hypotf(nx - wTx, ny - wTy) > 0.15f * r && random(2)) { L.v += 0.06f * r / pkL; modeKick(2, -0.05f, 0); }
      wTx = nx; wTy = ny;
      nextWander = now + (unsigned long)eyeRnd(3500, 8000);
    }
    if (!morph && (!run || !going)) { wx.step(wTx, dt); wy.step(wTy, dt); }

    // Music: on the CoreS3 / Stackchan the spikes pump on the beat (smoothed, so
    // fast tempos pump instead of strobing) and tentacles drum it. Without a mic,
    // a spike fan every 5-12 s, and sometimes a flick.
    #ifdef TARGET_CORES3
    bool heard = audioSpectrum.alive && !code;
    float env = heard ? audioSpectrum.beatEnv : 0, bass = heard ? constrain(audioSpectrum.bass, 0.0f, 1.0f) : 0;
    #else
    bool heard = false;
    float env = 0, bass = 0;
    #endif
    beatSm.step(env, dt);
    if (talk || code) {
      // a voice turn owns the spikes (stepTalk), and code mode its own beats (stepCode)
    } else if (heard) {
      if (free && env > 0.9f && prevEnv < 0.5f && nSeqs()) {   // a pattern owns the spikes: the beat throbs it
        throbUntil = now + 250; modeKick(2, 0.03f + 0.04f * bass, 0);
      } else if (free && env > 0.9f && prevEnv < 0.5f) {
        field(alphaHome(), 0.3f + 0.5f * bass, 120, now);
        modeKick(2, 0.04f + 0.08f * bass, 0);
        if (bass > 0.6f && now - lastHop > 450) { L.v += 0.06f * r / pkL; lastHop = now; }
        if (bass > 0.7f && now - lastPop > 600) { botEyes.popS.v += 0.03f / botEyes.pkPopS; lastPop = now; }
        if (PARTY_TENTS && now - lastBeatGrab > 450) beatGrab(now, bass);
      }
    } else if (free && eyeReached(now, nextPulse) && nSeqs()) {   // a throb on the pattern, not a new fan
      throbUntil = now + 250; modeKick(2, 0.05f, 0); later(now, 180, PE_KICK2, -1, 0, 0.03f);
      nextPulse = now + (unsigned long)eyeRnd(5000, 12000);
    } else if (free && eyeReached(now, nextPulse)) {
      field(alphaHome() + eyeRnd(-40, 40) * DEG_TO_RAD, 0.5f, (unsigned long)eyeRnd(500, 900), now);
      modeKick(2, 0.10f, 0);
      later(now, 180, PE_KICK2, -1, 0, 0.06f);
      if (PARTY_TENTS && eyeRnd01() < 0.25f && liveTents() < partyTentCap) flick(now, 0.25f);
      nextPulse = now + (unsigned long)eyeRnd(5000, 12000);
    }
    prevEnv = env;
    stepTalk(now, dt, r, free && !code);
    stepCode(now, dt);

    // The pattern library's scheduler (feeler groups are one of its entries now); it rests during a voice turn
    if (free && !talk && !code) scheduleSeq(now, heard);
    stepSlots(now, dt);

    // Field, attractor, spikes (up to 0.55 r). Spike k starts |k-4| x 35 ms late so the fan opens outward.
    if (eyeReached(now, fieldUntil)) fieldT = 0;
    F.step(fieldT, dt);
    if (!seqDrivesAl()) al.step(al.x + wrapPi(alT - al.x), dt);   // the carousel turns it itself
    for (int k = 0; k < PARTY_SPIKES; k++) {
      BotSpring &s = spk[k];
      bool late = (long)(now - evAt) < abs(k - 4) * 35;
      float d = (k - 4) / 2.2f;
      float target = late ? s.t : F.x * 0.55f * R * expf(-d * d) * jit[k];
      if (target > s.x) { s.f = 3.2f; s.z = 0.45f; } else { s.f = 4.5f; s.z = 0.38f; }   // pop up, snap back
      s.step(target, dt);
      if (s.x < -0.05f * R) { s.x = -0.05f * R; s.v = 0; }   // the snap-back dimple stays shallow
    }

    // Porcupine: spikes all round for 450 ms after a shake, then the snap back
    bool porc = !eyeReached(now, porcUntil);
    hP.f = porc ? 5 : 4.5f; hP.z = porc ? 0.6f : 0.38f;
    hP.step(porc ? 0.30f * R : 0, dt);

    // Lift, then the surface modes. Mode 2 is the squash (the lift spring's
    // undershoot is the landing splat) plus a stretch along the velocity.
    // Mid-morph the drop is the wide ellipse of the stroke around his eyes.
    L.step(0, dt);
    float vx = wx.v, vy = wy.v - L.v, speed = hypotf(vx, vy);
    if (speed > 0.2f * R) phiV = atan2f(vy, vx);   // freeze the stretch axis when slow
    float Sv = 0.18f * constrain(speed / (2.5f * R), 0.0f, 1.0f) * PARTY_JIGGLE;
    float c2 = 0.05f + (PARTY_MORPH_J - 0.05f) * (1 - morphOn()) + codeM2;
    if (L.x < 0) c2 += -L.x / R * 0.9f;
    float msq = 0;
    for (int k = 0; k < PARTY_MODES; k++) {
      mc[k].step(k == 0 ? c2 + Sv * cosf(2 * phiV) : k == 1 ? swC + codePump : 0, dt);
      ms[k].step(k == 0 ? Sv * sinf(2 * phiV) : k == 1 ? swS : 0, dt);
      float cl = PARTY_MODE_DEF[k].cl * max(1.0f, PARTY_JIGGLE);
      mc[k].x = constrain(mc[k].x, -cl, cl); ms[k].x = constrain(ms[k].x, -cl, cl);
      msq += mc[k].x * mc[k].x + ms[k].x * ms[k].x;
    }
    tapT *= expf(-dt / 0.6f);
    shakeT *= expf(-dt / 0.6f);
    D.step(0, dt);
    E.step((run && going) || morph ? 0.8f : constrain(speed / (3 * R) + 0.6f * tapT + shakeT + 0.25f * beatSm.x + codeE, 0.0f, 1.0f) * kappa.x, dt);

    // Bulge toward the gaze, or toward the spikes while a field is up
    float gx = botEyes.gxOut, gy = botEyes.gyOut;
    float gG = 0.07f * min(1.0f, hypotf(gx / 0.35f, gy / 0.25f));
    const PartySeq *ld = leadSeq();   // a pattern keeps the bulge on its axis, with a mound under a crown
    bulgeA = ld ? ld->axis : F.x > 0.05f ? al.x : atan2f(gy, gx);
    g.step(max(max(gG, 0.10f * F.x), seqG), dt);

    // Volume: the modes and the tentacles borrow from the body (motion spec §3.3)
    B = 1 + 0.05f * sinf(TWO_PI * 0.21f * t) + 0.08f * talkSm.x;   // speaking: he swells with the words
    Reff = R * B / sqrtf(1 + 0.5f * msq) * sqrtf(1 - borrow);

    // One integer centre for body, shadow, tentacles and eyes, moved only past 0.6 px so nothing shimmers
    float on = morphOn();
    float fcx = wx.x, fcy = wy.x - L.x + mc[0].x * R * on;   // planted bottom: squash keeps the base on the floor
    float st = (now - shakeAt) / 1000.0f;   // shake: a sideways jiggle at 5.7 Hz dying away
    if (shakeAt && st < 1.5f) fcx += 0.12f * R * expf(-5 * st) * sinf(TWO_PI * 5.7f * st);
    if (fabsf(fcx - icx) > 0.6f) icx = (int16_t)lroundf(fcx);
    if (fabsf(fcy - icy) > 0.6f) icy = (int16_t)lroundf(fcy);

    prepShape(Reff);
    for (int i = 0; i < PARTY_TENTS; i++) if (tents[i].on) stepTent(i, now, dt);
    for (int i = 0; i < PARTY_DROPS; i++) if (drops[i].on) stepDrop(drops[i], now, dt);

    // Eyes trail the body, look with the whole face, ride the squash and the breath
    // (mid-morph they stay centred, where the face's eyes are)
    ox.step(0.20f * R * gx / 0.35f * on, dt);
    oy.step((0.15f * R * gy / 0.25f + 0.6f * mc[0].x * R - 0.3f * (B - 1) * R - (mouthOn ? 0.12f * R : 0)) * on, dt);   // up a bit for the mouth
  }

  // The hop between the face's place and his corner, along an arc that rises a
  // quarter of the way, while the outline shrinks (or grows) and the eyes scale
  void stepMorph(unsigned long now, float dt) {
    if (morph == 2 && now - exitAt > 200 && P.t != 0) {   // anticipation over: launch
      modeKick(2, -0.12f, 0); L.v += 0.10f * tier().r / pkL;
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
    if (morph == 1 && P.x > 0.97f && code) {   // code mode: the drop lands in place, a thought pops up
      morph = 0;
      codeLanded(now);
    } else if (morph == 1 && P.x > 0.97f) {   // landed in the corner: a jiggle, a spike burst, two droplets, then the stretch
      morph = 0;
      modeKick(2, 0.16f, 0); modeKick(4, 0.03f, 0);
      field(alphaHome(), 0.8f, 300, now);
      botEyes.blinkStart(now, BW_SWAP);
      nextWander = now + 3000;
      nextFidget = now + (unsigned long)eyeRnd(12000, 25000);
      nextPulse = now + (unsigned long)eyeRnd(5000, 12000);
      nextRoam = now + (unsigned long)eyeRnd(PARTY_ROAM_MIN_MS, PARTY_ROAM_MAX_MS);
      nextFeeler = now + (unsigned long)eyeRnd(5000, 9000);
      for (int i = 0; i < 2; i++) {
        float a = alphaHome() + eyeRnd(-50, 50) * DEG_TO_RAD, v = eyeRnd(90, 140), r0 = Reff;
        spawnDrop(icx + r0 * cosf(a), icy + r0 * sinf(a), v * cosf(a), v * sinf(a), 0.08f * tier().r, now, false);
      }
      if (PARTY_TENTS) later(now, 400, PE_WAKE, -1, 0);
    } else if (morph == 2 && P.t == 0 && P.x < 0.03f) {   // back to the face
      morph = 0;
      active = false; haveCorner = false;
      if (code && !codeOn) codeClear();
      clearExtras();
      botEyes.padX = botEyes.padY = 0;
      botEyes.blinkStart(now, BW_SWAP);
    }
  }

  // Idle fidgets every 12-25 s, alternating: a shiver, or a scoot (feelers took over the probe)
  void fidget(unsigned long now) {
    uint8_t f = lastFidget == 1 ? 2 : 1;
    lastFidget = f;
    nextFidget = now + (unsigned long)eyeRnd(12000, 25000);
    if (f == 1) {   // shiver: a quick n = 7 ripple round the rim
      shiverAt = now;
    } else {   // scoot: a hop to a new spot in his corner, with a glance that way
      float x0, x1, y0, y1;
      box(corner, x0, x1, y0, y1);
      wTx = eyeRnd(min(x0, x1), max(x0, x1)); wTy = eyeRnd(min(y0, y1), max(y0, y1));
      L.v += 0.12f * tier().r / pkL; modeKick(2, -0.06f, 0);
      look(wTx > wx.x ? 0.9f : -0.9f, 0, 700, now);
      nextWander = now + (unsigned long)eyeRnd(3500, 8000);
    }
  }

  void stepRun(unsigned long now, float dt) {
    const PartyRun &m = *run;
    const float r = tier().r;
    if (!going) {
      if (!eyeReached(now, goAt)) return;
      going = true;
      fromX = wx.x; fromY = wy.x;   // he may have drifted during the lead-in
      corner = runTo; wTx = dstX; wTy = dstY;
      modeKick(2, m.jGo, 0); L.v += m.lift * r / pkL;
      Q.snap(0); Q.f = m.f; Q.z = m.z;
      float a = atan2f(dstY - fromY, dstX - fromX);
      field(a, 0.35f, 220, now, true);   // reach toward where he's going
      for (int s = 0; s < m.streamers && s < 2; s++) {   // a poke trails two floppy streamers
        PartyTentOpt o;
        o.dir = a + PI + (s ? 0.75f : -0.75f); o.L = eyeRnd(0.4f, 0.7f) * r * PARTY_REACH; o.kind = PT_STREAMER; o.hold = -1;
        o.gf = 3; o.gz = 0.6f; o.soft = true; o.curl = 0; o.free = true; o.swA = 0; o.boops = 0;
        strI[s] = spawnTent(o, now); strG[s] = tentGen;
      }
    }
    Q.step(1, dt);
    float p = constrain(Q.x, 0.0f, 1.03f), kx, ky;
    bowPoint(m, kx, ky);
    float x = (1 - p) * (1 - p) * fromX + 2 * (1 - p) * p * kx + p * p * dstX;
    float y = (1 - p) * (1 - p) * fromY + 2 * (1 - p) * p * ky + p * p * dstY;
    // Move along the path but keep a velocity, so stretch and slosh see the motion
    wx.v = (x - wx.x) / dt; wy.v = (y - wy.x) / dt;
    wx.x = wx.t = x; wy.x = wy.t = y;
    PartyTent *grab = tentOf(grabI, grabG);
    if (grab && p > 0.55f) retract(*grab, true);   // past the latch: let go
    if (Q.x > 0.97f) {   // landed: squash, spike burst, blink; the streamers flick a droplet and drain
      run = nullptr;
      if (code) landedAt = now;
      modeKick(2, m.jLand, 0); modeKick(4, 0.06f, 0);
      field(alphaHome(), m.land, 300, now);
      botEyes.blinkStart(now, BW_SWAP);
      botEyes.padX = botEyes.padY = 0; padUntil = 0;
      nextWander = now + 1500;
      nextRoam = now + (unsigned long)eyeRnd(PARTY_ROAM_MIN_MS, PARTY_ROAM_MAX_MS);
      for (int s = 0; s < 2; s++) {
        PartyTent *st = tentOf(strI[s], strG[s]);
        if (!st) continue;
        if (eyeRnd01() < 0.5f) { st->pinchArmed = true; st->pinchAt = now + 120; st->pinchV = eyeRnd(110, 160); }
        later(now, 380, PE_RETRACT, strI[s], strG[s]);
      }
      if (grab) retract(*grab, true);
    }
  }

  // ---- Shape ----------------------------------------------------------------
  // Per-frame constants of the radius function
  float a3, a4, a5, w3, w4, w5, cBul, sBul, cDent, sDent;
  void prepShape(float R) {
    float A = (1 + 2.0f * E.x) * kappa.x * R * min(1.0f, 2 * constrain(P.x, 0.0f, 1.0f));   // no wobble while it's still the face's outline
    a3 = A * 0.065f * (0.6f + 0.4f * sinf(TWO_PI * 0.050f * t + ph[3]));   // edge waves n = 3, 4, 5, each
    a4 = A * 0.045f * (0.6f + 0.4f * sinf(TWO_PI * 0.071f * t + ph[4]));   // with a slow amplitude drift so
    a5 = A * 0.027f * (0.6f + 0.4f * sinf(TWO_PI * 0.037f * t + ph[5]));   // the pattern never repeats
    w3 = TWO_PI * 0.13f * t + ph[0]; w4 = -TWO_PI * 0.19f * t + ph[1]; w5 = TWO_PI * 0.29f * t + ph[2];
    cBul = cosf(bulgeA); sBul = sinf(bulgeA); cDent = cosf(thD); sDent = sinf(thD);
    float sp = (millis() - shiverAt) / 600.0f;   // shiver: n = 7 at 1.1 Hz under a 600 ms sin² envelope
    a7 = (shiverAt && sp < 1) ? 0.02f * R * sq(sinf(PI * sp)) : 0;
    w7 = TWO_PI * 1.1f * t;
    floorOn = P.x > 0.9f;
  }
  float a7, w7;
  bool floorOn;

  // Body radius at angle th, without spikes, mounds or the floor (tentacle roots, droplets)
  float radiusAt(float th, float R) const {
    float wob = a3 * sinf(3 * th + w3) + a4 * sinf(4 * th + w4) + a5 * sinf(5 * th + w5);
    if (a7 > 0) wob += a7 * sinf(7 * th + w7);
    float m = 0;
    for (int k = 0; k < PARTY_MODES; k++) m += mc[k].x * cosf((k + 2) * th) + ms[k].x * sinf((k + 2) * th);
    return radiusCore(cosf(th), sinf(th), R, wob, m);
  }
  float radiusCore(float c, float s, float R, float wob, float m) const {
    float r = R * (1 + m) + wob;
    float cb = c * cBul + s * sBul;
    if (cb > 0) r += R * g.x * cb * cb * cb;
    // Poke dent: a soft dip 0.6 rad each side of the finger (cos 0.6 = 0.825)
    if (D.x < -0.05f) {
      float cd = c * cDent + s * sDent;
      if (cd > 0.825f) { float q = cosf(acosf(min(1.0f, cd)) * PI / 1.2f); r += D.x * q * q; }
    }
    return r;
  }
  // Floor (art director's eye zone): the eyes reach 2.47 R on a mood pop, so the
  // sides never pull in past 0.97 R, the rest past 0.85 R. Mid-morph the drop is
  // the stroke's flat ellipse, so no floor then.
  static float floorFor(float s, float R) { return (fabsf(s) < 0.5736f ? 0.97f : 0.85f) * R; }   // within 35° of horizontal

  // Radii for this frame (partyRR, with the unit vectors in partyC/partyS)
  // Edge waves and surface modes step around the outline by rotation (sin(n(θ + 3°) + w)
  // from sin and cos at θ), so the whole outline costs a few sinf/cosf, not hundreds.
  void buildOutline(float R) {
    float a0 = al.x, ca = cosf(a0), sa = sinf(a0);
    float ws[4], wc[4], mcs[PARTY_MODES], msn[PARTY_MODES];
    const float amp[4] = {a3, a4, a5, a7}, w[4] = {w3, w4, w5, w7};
    const int harm[4] = {3, 4, 5, 7};
    int nh = a7 > 0 ? 4 : 3;
    for (int h = 0; h < nh; h++) { ws[h] = sinf(harm[h] * a0 + w[h]); wc[h] = cosf(harm[h] * a0 + w[h]); }
    for (int k = 0; k < PARTY_MODES; k++) { mcs[k] = cosf((k + 2) * a0); msn[k] = sinf((k + 2) * a0); }
    // Tentacle mounds and droplet necks, as unit vectors
    float tc[PARTY_TENTS ? PARTY_TENTS : 1], ts[PARTY_TENTS ? PARTY_TENTS : 1], tm[PARTY_TENTS ? PARTY_TENTS : 1];
    float dc[PARTY_DROPS ? PARTY_DROPS : 1], ds[PARTY_DROPS ? PARTY_DROPS : 1], dm[PARTY_DROPS ? PARTY_DROPS : 1];
    int nt = 0, nd = 0;
    const float rT = tier().r;
    for (int i = 0; i < PARTY_TENTS; i++) {
      const PartyTent &t = tents[i];
      if (!t.on) continue;
      tc[nt] = cosf(t.th.x); ts[nt] = sinf(t.th.x); tm[nt] = 0.12f * constrain(t.lam.x, 0.0f, 1.2f) * R; nt++;
    }
    for (int i = 0; i < PARTY_DROPS; i++) {
      const PartyDrop &d = drops[i];
      if (!d.on || d.phase == 0 || d.gap >= 0.3f * rT) continue;
      dc[nd] = cosf(d.th); ds[nd] = sinf(d.th); dm[nd] = 0.10f * (1 - max(0.0f, d.gap) / (0.3f * rT)) * R; nd++;
    }
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
      float m = 0;
      for (int k = 0; k < PARTY_MODES; k++) {
        m += mc[k].x * mcs[k] + ms[k].x * msn[k];
        float c2 = mcs[k] * partyModeStepC[k] - msn[k] * partyModeStepS[k];
        msn[k] = msn[k] * partyModeStepC[k] + mcs[k] * partyModeStepS[k];
        mcs[k] = c2;
      }
      float r = radiusCore(c, s, R, wob, m);
      for (int j = 0; j < nt; j++) {   // a tentacle roots in a mound (fake smooth-min)
        float q = c * tc[j] + s * ts[j];
        if (q > 0) { q *= q; q *= q; r += tm[j] * q * q; }
      }
      for (int j = 0; j < nd; j++) {   // a returning droplet pulls a neck toward itself
        float q = c * dc[j] + s * ds[j];
        if (q > 0) { float q2 = q * q, q4 = q2 * q2; r += dm[j] * q4 * q4 * q4; }
      }
      partyRR[i] = r;
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
    // Pattern spikes (spec §5.1, §5.4): offsets from vertex 0, 5 or 7 wide; a lean tilts the tip sideways
    memset(partyTT, 0, sizeof(partyTT));
    for (int bi = 0; bi < PARTY_BANKS; bi++) for (int j = 0; j < PARTY_SLOTS; j++) {
      const PartySlot &q = banks[bi].s[j];
      float h = q.h.x;
      if (fabsf(h) < 0.05f) continue;
      const float *lut = q.wide == 7 ? PARTY_LUT7 : PARTY_SPK_LUT;
      int w = q.wide == 7 ? 3 : 2;
      float cl = cosf(q.lean), sl = sinf(q.lean);
      for (int d = -w; d <= w; d++) {
        int i = ((q.off + d) % PARTY_N + PARTY_N) % PARTY_N;
        float v = h * lut[abs(d)];
        partyRR[i] += v * cl; partyTT[i] += v * sl;
      }
    }
    if (floorOn) for (int i = 0; i < PARTY_N; i++) partyRR[i] = max(partyRR[i], floorFor(partyS[i], R));
  }
  // The outline polygon around (cx, cy), grown outward by grow px
  void placeOutline(float cx, float cy, float grow) {
    for (int i = 0; i < PARTY_N; i++) {
      partyVX[i] = cx + (partyRR[i] + grow) * partyC[i] - partyTT[i] * partyS[i];
      partyVY[i] = cy + (partyRR[i] + grow) * partyS[i] + partyTT[i] * partyC[i];
    }
  }

  // ---- Render ---------------------------------------------------------------
  void render(unsigned long now) {
    if (gfx == nullptr) return;
    if (!active) enter(now);
    uint32_t t0 = micros(), ts = t0;
    auto stage = [&](int i) { uint32_t n = micros(); partyRenderStats.stageUs[i] += n - ts; ts = n; };
    codeBackdrop.draw(botFaceColor);   // code mode's minimap goes under everything (bot_mode cleared the screen)
    stage(6);
    step(now);
    stage(0);

    const PartyTier &T = tier();
    const float R = Reff, on = morphOn();
    uint16_t *buf = lcdCanvasPixels();   // null on an 8-bit canvas: no shadows or rim then

    // Sheen colour: what the show looks like at the screen centre, low-passed;
    // and every 1.5 s, the brightest spot in reach (tentacles like to feel for it)
    if (buf) {
      uint16_t p = partySwap(buf[(LCD_HEIGHT / 2) * LCD_WIDTH + LCD_WIDTH / 2]);
      float target[3] = {(float)((p >> 11) & 31) * 8.2f, (float)((p >> 5) & 63) * 4.05f, (float)(p & 31) * 8.2f};
      for (int c = 0; c < 3; c++) sheen[c] += (target[c] - sheen[c]) * 0.15f;
      if (eyeReached(now, nextBright)) sampleBright(buf, now);
    }

    buildOutline(R);
    float tentArea = 0;
    int live = 0;
    for (int i = 0; i < PARTY_TENTS; i++) if (tents[i].on) { prepTent(tents[i], now); tentArea += tents[i].area; live++; }
    borrow = min(0.08f, 0.3f * tentArea / ((float)PI * R * R));
    if (live > partyRenderStats.tentsMax) partyRenderStats.tentsMax = live;
    const bool small = P.x > 0.9f;
    stage(1);
    if (buf && P.x > 0.6f) drawShadow(buf, R);
    if (buf && small) drawLiftShadow(buf);
    stage(2);
    if (buf) drawRimAndBody(buf, small);
    else {
      partyRB.clear();
      placeOutline(icx, icy, 0);
      partyRB.add(partyVX, partyVY, PARTY_N);
      if (small) addSmall(partyRB, 0, 0, 0);
      partyRB.begin();
      int16_t xa[PARTY_MAX_SPANS], xb[PARTY_MAX_SPANS];
      for (int y = partyRB.ya; y <= partyRB.yb; y++) {
        int k = partyRB.spans(y, xa, xb);
        for (int i = 0; i < k; i++) gfx->drawFastHLine(xa[i], y, xb[i] - xa[i] + 1, BOT_COLOR_BG);
      }
    }
    stage(3);
    if (P.x > 0.8f) drawSheen(R);
    stage(4);

    // Eyes: the firmware model, no mouth, clamped inside the drop
    float k = morphK(), hw = 2.04f * 48 * k, hh = 1.22f * 48 * k;
    float ex = ox.x, ey = oy.x, sq0 = mc[0].x;
    float ax = max(1.0f, R * (1 + sq0) - hw - 3), ay = max(1.0f, R * (1 - sq0) - hh - 3), q = hypotf(ex / ax, ey / ay);
    if (q > 1 && on > 0.99f) { ex /= q; ey /= q; }
    if (fabsf(ex - iox) > 0.6f) iox = (int16_t)lroundf(ex);
    if (fabsf(ey - ioy) > 0.6f) ioy = (int16_t)lroundf(ey);
    botEyes.render(icx + iox, icy + ioy, k, botFaceColor, false, now, T.brows || P.x < 0.9f);
    mouthX = icx + iox; mouthY = icy + ioy; mouthK = k;   // bot_mode adds the face's mouth offset (x k), kept off the floor
    mouthFloor = icy + (int16_t)(0.85f * R) - 6;
    stage(5);

    uint32_t us = micros() - t0;
    partyRenderStats.sumUs += us;
    partyRenderStats.count++;
    if (us > partyRenderStats.maxUs) partyRenderStats.maxUs = us;
    guard(us, now);
  }

  // Cost guard: cap live tentacles at 3 only while a 2 s window has a frame over
  // 9 ms; lift the cap after 10 s of windows under 7 ms. Kevin would rather the
  // frame rate dip than lose tentacles, so this only catches a runaway.
  void guard(uint32_t us, unsigned long now) {
    if (us > guardMax) guardMax = us;
    if (now - guardAt < 2000) return;
    if (guardMax > 9000) { partyTentCap = min(3, PARTY_TENTS); calmSince = 0; }
    else if (guardMax < 7000) {
      if (!calmSince) calmSince = now;
      else if (now - calmSince > 10000) partyTentCap = PARTY_TENTS;
    } else calmSince = 0;
    guardAt = now; guardMax = 0;
  }

  // The brightest of 12 points on two arcs into the show, if it stands out
  void sampleBright(const uint16_t *buf, unsigned long now) {
    nextBright = now + 1500;
    const float r = tier().r;
    int best = -1, sum = 0, n = 0;
    for (int arc = 0; arc < 2; arc++) for (int i = 0; i < 6; i++) {
      float a = clampCone(alphaHome() + (-55 + i * 22) * DEG_TO_RAD, 60), d = (arc ? 2.2f : 1.6f) * r;
      int x = (int)lroundf(icx + d * cosf(a)), y = (int)lroundf(icy + d * sinf(a));
      if (x < 4 || y < 4 || x >= LCD_WIDTH - 4 || y >= LCD_HEIGHT - 4) continue;
      uint16_t p = partySwap(buf[y * LCD_WIDTH + x]);
      int l = (((p >> 11) & 31) * 4 + ((p >> 5) & 63) * 5 + (p & 31) * 2) / 8;
      sum += l; n++;
      if (l > best) { best = l; brightX = x; brightY = y; }
    }
    haveBright = n && best * 4 > sum * 5 / n;   // 1.25 x the average
  }

  // Rim and body in one pass, over the union of the body, tentacles, droplets
  // and bridges. The rim is everything grown 1.5 px; each rim row is split
  // around the fill's spans, so only the thin ring is read, halved and written
  // back, and the inside is cleared to black. Overlapping parts read as one liquid.
  // Rim colour: the show at half brightness, or a faint face-colour line on dark scenes.
  void drawRimAndBody(uint16_t *buf, bool small) {
    partyRA.clear();
    placeOutline(icx, icy, 1.5f);
    partyRA.add(partyVX, partyVY, PARTY_N);
    if (small) addSmall(partyRA, 1.5f, 0, 0);
    partyRA.begin();
    partyRB.clear();
    placeOutline(icx, icy, 0);
    partyRB.add(partyVX, partyVY, PARTY_N);
    if (small) addSmall(partyRB, 0, 0, 0);
    partyRB.begin();
    uint16_t fc = botFaceColor;
    uint16_t fr = (uint16_t)(((fc >> 11) & 31) * 0.22f), fg = (uint16_t)(((fc >> 5) & 63) * 0.22f), fb = (uint16_t)((fc & 31) * 0.22f);
    auto rim = [&](uint16_t *row, int a, int b) {
      for (int x = a; x <= b; x++) {
        uint16_t p = partySwap(row[x]);
        uint16_t r = max((uint16_t)(((p >> 11) & 31) >> 1), fr), gg = max((uint16_t)(((p >> 5) & 63) >> 1), fg), bb = max((uint16_t)((p & 31) >> 1), fb);
        row[x] = partySwap((r << 11) | (gg << 5) | bb);
      }
    };
    int16_t oa[PARTY_MAX_SPANS], ob[PARTY_MAX_SPANS], ia[PARTY_MAX_SPANS], ib[PARTY_MAX_SPANS];
    for (int y = partyRA.ya; y <= partyRA.yb; y++) {
      uint16_t *row = buf + y * LCD_WIDTH;
      int no = partyRA.spans(y, oa, ob);
      int ni = partyRB.spans(y, ia, ib);
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

  // Lift shadow: one flat 75 % tone (+1, +2) px under the tentacles and droplets, so they float over the show
  void drawLiftShadow(uint16_t *buf) {
    partyRA.clear();
    if (!addSmall(partyRA, 0, 1, 2)) return;
    partyRA.begin();
    int16_t xa[PARTY_MAX_SPANS], xb[PARTY_MAX_SPANS];
    for (int y = partyRA.ya; y <= partyRA.yb; y++) {
      int k = partyRA.spans(y, xa, xb);
      uint16_t *row = buf + y * LCD_WIDTH;
      for (int i = 0; i < k; i++) for (int x = xa[i]; x <= xb[i]; x++) row[x] = partySwap(partyDark75(partySwap(row[x])));
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
      float r = max(radiusAt(th, R), floorOn ? floorFor(s, R) : 0.0f) - 2, w = 0.08f * R * sinf(PI * u);
      sx[i] = icx + r * c; sy[i] = icy + r * s;
      sx[23 - i] = icx + (r - w) * c; sy[23 - i] = icy + (r - w) * s;
    }
    partyFillPoly(sx, sy, 24, [&](int y, int xa, int xb) { gfx->drawFastHLine(xa, y, xb - xa + 1, col); });
    float vx = wx.v, vy = wy.v - L.v;
    if (hypotf(vx, vy) < 0.2f * R && E.x < 0.6f) {   // specular dot, hidden in fast motion
      float th = 225 * DEG_TO_RAD, c = cosf(th), s = sinf(th), r = radiusAt(th, R) - 2 - 0.04f * R;
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
