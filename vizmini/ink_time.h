#ifndef INK_TIME_H
#define INK_TIME_H

#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "gfx_mono.h"
#include "weather.h"

// ============================================================================
// Ink Time — vizMini's clock and weather as ferrofluid (Kevin's pick, 2026-11-03)
// ============================================================================
// Concept A from the info-screen pitch (artifact "vizMini Info Screens"): the
// numbers are ink pulled out of the drop on a white lightbox. Everything is a
// list of capsules (a stroke with a radius at each end) merged as metaballs:
// each capsule adds a (1 - d^2/R^2)^2 falloff to a field and the ink is where
// the field passes 0.3, so strokes that meet grow gooey necks.
//
// The C3 has no FPU, so the per-pixel field is integer: the projection onto
// each capsule steps in 16.16 across a row, the falloff is Q12 and the field
// is a saturating uint16. Capsules are built in float (a few dozen a frame).
//
// Phase 1: the renderer and the clock (liquid 7-segment digits, a magnet spike
// per digit that pops each second in a wave, hopping colon drops, a changed
// digit drains into a pool and grows the new strokes out of it), the date in
// a bold 3x5 font, and a +-2 px layout drift each minute against uneven wear.
// ============================================================================

#define INK_FK        1.488f     // visible radius -> field support (a lone capsule's edge lands on r)
#define INK_THRESH    1229       // 0.3 in Q12
#define INK_MAX_PRIMS 64

struct InkPrim {
  float x1, y1, x2, y2, r1, r2;
  float cx, cy;                  // where it drains to / grows from in a morph
  uint8_t key;                   // slot * 8 + segment, or 0xFF
};

class InkTime {
public:
  uint32_t lastUs = 0, maxUs = 0;   // render cost (shown in /state)
  int inkPixels = 0;                // black pixels last frame (brightness, LEDs)

  uint8_t forceSky = 0xFF;          // test switch (/bot/info?sky=rain), 0xFF = real weather

  // ---- scenes and transitions -------------------------------------------------
  // One move everywhere (concept A): everything gathers into the drop, then
  // splits out into the next screen.
  //   enter: his eyes shut, the face's black is the drop and contracts like an
  //          iris opening (corners first), then splits into the screen
  //   flip:  clock <-> weather through the drop
  //   exit:  the screen gathers into the drop, it swells over everything and
  //          his eyes open on the black (vizmini.ino draws the face on top)
  enum : uint8_t { SC_CLOCK = 0, SC_WEATHER };
  enum : uint8_t { TR_NONE = 0, TR_ENTER, TR_EXIT, TR_FLIP };

  void startEnter(uint8_t sc, unsigned long now) { scene = sc; begin(TR_ENTER, INK_ENTER_MS, now); }
  void startExit(unsigned long now) { if (tr != TR_EXIT) begin(TR_EXIT, INK_EXIT_MS, now); }
  void startFlip(uint8_t to, unsigned long now) {
    if (tr == TR_EXIT || to == scene) return;
    from = scene; scene = to; begin(TR_FLIP, INK_FLIP_MS, now);
  }
  uint8_t current() const { return scene; }
  bool exitDone() const { return exited; }
  bool busy() const { return tr != TR_NONE; }

  // How far toward the ink screen the panel brightness should be, 0..1
  float brightP(unsigned long now) const {
    float p = progress(now);
    if (tr == TR_ENTER) return seg01(p, .12f, .55f);
    if (tr == TR_EXIT) return exited ? 0 : 1 - seg01(p, .45f, .85f);
    return 1;
  }
  // The face drawn over the black this frame, and whether its eyes are shut
  bool faceOver(unsigned long now, bool &shut) const {
    float p = progress(now);
    if (tr == TR_ENTER && p < .12f) { shut = true; return true; }
    if (tr == TR_EXIT && (p >= .85f || exited)) { shut = p < .9f && !exited; return true; }
    return false;
  }

  void render(uint8_t *buf, unsigned long now, const Weather &w) {
    uint32_t t0 = micros();
    float p = progress(now);
    if (tr != TR_NONE && p >= 1) {
      if (tr == TR_EXIT) exited = true;
      else tr = TR_NONE;
    }
    memset(buf, 0xFF, OLED_WIDTH * OLED_HEIGHT / 8);      // the lightbox
    n = 0;
    float tms = (float)(now % 600000UL);
    switch (tr) {
      case TR_NONE:
        buildScene(scene, now, w);
        inkPixels = renderField(buf);
        postScene(buf, scene, now, w);
        break;
      case TR_FLIP: {
        int na = buildInto(A, from, now, w), nb = buildInto(B, scene, now, w);
        n = 0;
        // gather 0-.42, a beat in the drop, then a longer split
        float g = p < .42f ? p / .42f * .5f : (p < .55f ? .5f : .5f + .5f * (p - .55f) / .45f);
        morphGather(A, na, B, nb, g, 64, 30, tms);
        inkPixels = renderField(buf);
        if (p < .3f) postScene(buf, from, now, w, false);
        if (p > .7f) postScene(buf, scene, now, w, false);
        break;
      }
      case TR_ENTER:
        if (p < .12f) { memset(buf, 0, OLED_WIDTH * OLED_HEIGHT / 8); inkPixels = 8192; break; }   // all drop
        // 72 px just covers the corners; shrink fast first so the white opens at once
        if (p < .55f) { float u = seg01(p, .12f, .55f); dropPrims(72 - 59 * (1 - (1 - u) * (1 - u)), tms); }
        else { int nb = buildInto(B, scene, now, w); n = 0; morphGather(nullptr, 0, B, nb, .5f + .5f * seg01(p, .55f, 1), 64, 32, tms); }
        inkPixels = renderField(buf);
        if (p > .85f) postScene(buf, scene, now, w, false);
        break;
      case TR_EXIT:
        if (exited || p >= .85f) { memset(buf, 0, OLED_WIDTH * OLED_HEIGHT / 8); inkPixels = 8192; break; }
        if (p < .45f) { int na = buildInto(A, scene, now, w); n = 0; morphGather(A, na, nullptr, 0, .5f * seg01(p, 0, .45f), 64, 32, tms); }
        else { float u = seg01(p, .45f, .85f); dropPrims(13 + 59 * u * u, tms); }   // slow, then fast to the corners
        inkPixels = renderField(buf);
        if (p < .15f) postScene(buf, scene, now, w, false);
        break;
    }
    cost(t0);
  }

  // ---- text: Helvetica bold in black. A doubled 3x5 font smeared ("TUE NOV"
  // read "THE HOV", "%" read "Z"), so the ink screens use U8g2's helvB08 ----
  // colour: 0 black, 1 white
  static void inkStr(const char *s, int x, int baseline, bool centre = false, uint8_t colour = 0,
                     const uint8_t *font = u8g2_font_helvB08_tr) {
    u8g2.setFont(font);
    if (centre) x -= u8g2.getStrWidth(s) / 2;
    u8g2.setFontMode(1);
    u8g2.setDrawColor(colour);
    u8g2.drawStr(x, baseline, s);
    u8g2.setDrawColor(1);
  }

  // ---- boot intro ------------------------------------------------------------
  // t: ms since the intro started. The white opens like an iris on the black,
  // a drip falls in and splashes into the drop, then a compass spike sweeps
  // round it while the WiFi joins.
  static constexpr unsigned long INTRO_JOIN_AT = 1400;   // the joining loop starts here

  void introJoin(uint8_t *buf, unsigned long t, const char *ssid) {
    uint32_t t0 = micros();
    n = 0;
    if (t < 450) {                                         // the iris opens
      memset(buf, 0, OLED_WIDTH * OLED_HEIGHT / 8);
      float R = 72 * ease(t / 450.0f), R2 = R * R;
      for (int y = 0; y < OLED_HEIGHT; y++) for (int x = 0; x < OLED_WIDTH; x++) {
        float dxp = x + .5f - 64, dyp = y + .5f - 32;
        if (dxp * dxp + dyp * dyp < R2) setPx(buf, x, y, true);
      }
      cost(t0);
      return;
    }
    memset(buf, 0xFF, OLED_WIDTH * OLED_HEIGHT / 8);
    const float cx = 64, cy = 22;
    if (t < 1000) {                                        // a drip falls in
      float v = (t - 450) / 550.0f, y = -4 + (cy + 4) * v * v;
      add({ cx, y - 6 * v, cx, y, 2, 2.6f, cx, cy, 0xFF });
    } else if (t < INTRO_JOIN_AT) {                        // and splashes into the drop
      float k = t - 1000, R = 11 * (1 - expf(-k / 90) * cosf(k / 45));
      add({ cx, cy, cx, cy, R, R, cx, cy, 0xFF });
      static const float A3[3] = { -2.5f, -1.57f, -.64f };
      float d = 19 * sinf(PI * constrain(k / 400, 0.0f, 1.0f));
      for (float a : A3) add({ cx + cosf(a) * d, cy + sinf(a) * d * .8f, cx + cosf(a) * d, cy + sinf(a) * d * .8f, 2.3f, 2.3f, cx, cy, 0xFF });
    } else {                                               // joining: a compass spike sweeps round
      float R = 10.5f + .6f * sinf(t * .004f), a = (t - INTRO_JOIN_AT) * .0045f;
      add({ cx, cy, cx, cy, R, R, cx, cy, 0xFF });
      add({ cx + cosf(a) * 8, cy + sinf(a) * 8, cx + cosf(a) * 20, cy + sinf(a) * 16, 2.6f, .9f, cx, cy, 0xFF });
      add({ cx - cosf(a) * 8, cy - sinf(a) * 8, cx - cosf(a) * 15, cy - sinf(a) * 12, 2.4f, .9f, cx, cy, 0xFF });
    }
    renderField(buf);
    if (t >= INTRO_JOIN_AT) {
      char l[40]; int dots = (t / 300) % 4;
      snprintf(l, sizeof(l), "%s%.*s", ssid, dots, "...");
      inkStr("Joining", 64, 50, true);
      inkStr(l, 64, 61, true);
    }
    cost(t0);
  }

  // k: ms since the join finished (or failed). Online: a crown pops, an ink bar
  // wipes across and reveals the address. Hotspot: he sags into a puddle and
  // raises a feeler beside the hotspot name, held long enough to read. Then the
  // drop swells over the screen. Returns false once the screen is all black
  // (the face takes over and opens its eyes).
  bool introResult(uint8_t *buf, unsigned long k, bool online, const char *big, const char *small) {
    uint32_t t0 = micros();
    unsigned long hold = online ? INTRO_IP_HOLD_MS : INTRO_AP_HOLD_MS;
    if (k >= hold + 800) { memset(buf, 0, OLED_WIDTH * OLED_HEIGHT / 8); return false; }
    memset(buf, 0xFF, OLED_WIDTH * OLED_HEIGHT / 8);
    n = 0;
    const float cx = 64;
    float tms = (float)(millis() % 600000UL);
    if (k >= hold) {                                       // swell into the face
      float q = ease((k - hold) / 800.0f), yy = (online ? 13 : 12) + (32 - (online ? 13 : 12)) * q, R = (online ? 8 : 7) + (72 - (online ? 8 : 7)) * q;
      add({ cx, yy, cx, yy, R, R, cx, yy, 0xFF });
      for (int i = 0; i < 6; i++) {
        float a = tms * .004f + i * 1.047f, x = cx + cosf(a) * R * .9f, y = yy + sinf(a) * R * .9f, rr = R * .22f + 1;
        add({ x, y, x, y, rr, rr, cx, yy, 0xFF });
      }
      renderField(buf);
      cost(t0);
      return true;
    }
    float e = ease(k / 450.0f);
    if (online) {
      float cy = 22 + (13 - 22) * e, R = 10.5f + (8 - 10.5f) * e;
      add({ cx, cy, cx, cy, R, R, cx, cy, 0xFF });
      for (int i = 0; i < 5; i++) {                        // the crown pops
        float a = -HALF_PI + (i - 2) * .63f, L = 3 + 7 * fmaxf(0, expf(-k / 260.0f) * cosf(k / 70.0f));
        add({ cx + cosf(a) * 6, cy + sinf(a) * 6, cx + cosf(a) * (R + 2 + L), cy + sinf(a) * (R + 2 + L), 2.4f, .9f, cx, cy, 0xFF });
      }
    } else {
      float cy = 22 + (12 - 22) * e, sw = sinf(tms * .004f);
      add({ cx - 9 * e, cy, cx + 9 * e, cy, 10.5f + (6.5f - 10.5f) * e, 10.5f + (6.5f - 10.5f) * e, cx, cy, 0xFF });   // a puddle
      float hx = cx + 13 + 2 * sw, hy = fmaxf(3.5f, cy - 10 * e + cosf(tms * .004f));
      add({ cx + 8, cy - 2, hx, hy, 3, 1.8f, cx, cy, 0xFF });                                                       // a feeler
      add({ hx, hy, hx, hy, 2.6f, 2.6f, cx, cy, 0xFF });
    }
    // The ink bar wipes across and stays: the address sits on it in white
    // (draining it away cut through the letters mid-way)
    float wv = 56 * ease(((long)k - 300) / 350.0f);
    if (k > 300) add({ cx - wv, 44, cx + wv, 44, 7, 7, cx, 44, 0xFF });
    renderField(buf);
    if (k > 650) {
      if (!online) inkStr("Hotspot", 64, 32, true);
      inkStr(big, 64, 49, true, 1, u8g2_font_helvB10_tr);
      long nch = ((long)k - 950) / 30;                    // (k is unsigned: subtract as signed)
      if (nch > 0) { char sm[32]; snprintf(sm, sizeof(sm), "%.*s", nch, small); inkStr(sm, 64, 62, true); }   // typed in
    }
    cost(t0);
    return true;
  }
  static inline void setPx(uint8_t *buf, int x, int y, bool white) {
    if ((unsigned)x >= OLED_WIDTH || (unsigned)y >= OLED_HEIGHT) return;
    uint8_t *b = &buf[(y >> 3) * OLED_WIDTH + x];
    if (white) *b |= (1 << (y & 7)); else *b &= ~(1 << (y & 7));
  }

private:
  InkPrim P[INK_MAX_PRIMS], A[INK_MAX_PRIMS], B[INK_MAX_PRIMS];
  uint8_t scene = SC_CLOCK, from = SC_CLOCK, tr = TR_NONE;
  unsigned long trAt = 0; uint16_t trMs = 1;
  bool exited = false;
  uint8_t sky = SKY_CLEAR; bool skyDay = true;
  struct tm cti; bool cHave = false;           // the clock's time this frame (for its text)

  void begin(uint8_t kind, uint16_t ms, unsigned long now) { tr = kind; trMs = ms; trAt = now; exited = false; }
  float progress(unsigned long now) const { return tr == TR_NONE ? 1 : constrain((now - trAt) / (float)trMs, 0.0f, 1.0f); }
  static float seg01(float p, float a, float b) { return constrain((p - a) / (b - a), 0.0f, 1.0f); }

  // Build one scene's capsules into P (n)
  void buildScene(uint8_t sc, unsigned long now, const Weather &w) {
    if (sc == SC_CLOCK) buildClock(now); else buildWeather(now, w);
  }
  int buildInto(InkPrim *dst, uint8_t sc, unsigned long now, const Weather &w) {
    n = 0; buildScene(sc, now, w);
    memcpy(dst, P, n * sizeof(InkPrim));
    return n;
  }
  // The text and pixel effects drawn after the field. `fx` = shear, flash and
  // fog bands too (left out mid-transition so they don't fight the drop)
  void postScene(uint8_t *buf, uint8_t sc, unsigned long now, const Weather &w, bool fx = true) {
    if (sc == SC_CLOCK) clockText();
    else weatherPost(buf, now, w, fx);
  }

  void buildClock(unsigned long now) {
    char digs[5]; cHave = clockDigits(digs, cti);
    unsigned long ms = subSecond(cti, now);
    if (strcmp(digs, shown) != 0) { strcpy(prevDigs, shown); strcpy(shown, digs); chAt = now; }   // a digit changed: morph
    driftFor(cti);
    float q = (now - chAt) / 700.0f;
    if (prevDigs[0] && q < 1) {
      static InkPrim Am[INK_MAX_PRIMS], Bm[INK_MAX_PRIMS];   // static: the loop task's stack is only 8 KB
      int na = clockPrims(Am, prevDigs, ms, now), nb = clockPrims(Bm, shown, ms, now);
      morphKeyed(Am, na, Bm, nb, q);
    } else {
      n = clockPrims(P, shown, ms, now);
    }
    stepDrips(shown, now);
  }
  // Date + AM/PM under the digits
  void clockText() {
    char line[24];
    if (cHave) {
      static const char *const D[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
      static const char *const M[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
      snprintf(line, sizeof(line), "%s %s %d %s", D[cti.tm_wday], M[cti.tm_mon], cti.tm_mday,
               CLOCK_24H ? "" : (cti.tm_hour < 12 ? "AM" : "PM"));
    } else {
      strcpy(line, "SYNCING");
    }
    inkStr(line, 64 + dx, 62 + dy, true);
  }

  // The temperature in liquid digits on the left, the drop acting out the sky
  // on the right, the condition / high-low / rain chance in bold underneath
  void buildWeather(unsigned long now, const Weather &w) {
    struct tm ti; time_t tn = time(nullptr); localtime_r(&tn, &ti);
    driftFor(ti);
    float t = (now % 600000UL) * 0.001f;
    bool forced = forceSky < SKY_NUM;
    sky = forced ? forceSky : (w.valid ? inkSky(w) : SKY_CLOUDY);
    skyDay = w.isDay || forced;
    flash = false;
    if (w.valid || forced) tempPrims(w.valid ? w.tempF : 72, t);
    skyPrims(sky, skyDay, t, now);
  }
  void weatherPost(uint8_t *buf, unsigned long now, const Weather &w, bool fx) {
    float t = (now % 600000UL) * 0.001f;
    bool forced = forceSky < SKY_NUM;
    skyPost(buf, sky, skyDay, t, now, fx);
    if (w.valid || forced) {
      static const char *const name[SKY_NUM] = { "CLEAR", "CLOUDY", "RAIN", "SNOW", "STORMS", "FOG", "WINDY" };
      char l1[17], l2[24];
      if (forced || !w.valid) strcpy(l1, name[sky]);
      else { strncpy(l1, w.condition, 16); l1[16] = 0; for (char *c = l1; *c; c++) *c = toupper(*c); }
      snprintf(l2, sizeof(l2), "H%d L%d  %d%%", w.valid ? w.hi : 79, w.valid ? w.lo : 58, w.valid ? w.pop : 40);
      inkStr(l1, 4 + dx, 51 + dy);
      inkStr(l2, 4 + dx, 62 + dy);
    } else {
      inkStr(WiFi.status() == WL_CONNECTED ? "No weather" : "No network", 4, 51);
    }
    if (!fx) return;
    if (sky == SKY_WIND) shear(buf, t);
    if (flash) { for (int i = 0; i < OLED_WIDTH * OLED_HEIGHT / 8; i++) buf[i] ^= 0xFF; }   // lightning
  }

  // ---- the drop -------------------------------------------------------------
  // One big drop at the centre with six drops riding its rim, so the edge
  // wobbles like liquid as it shrinks or swells
  void dropPrims(float R, float tms) {
    add({ 64, 32, 64, 32, R, R, 64, 32, 0xFF });
    for (int i = 0; i < 6; i++) {
      float a = tms * .004f + i * 1.047f, x = 64 + cosf(a) * R * .9f, y = 32 + sinf(a) * R * .9f, rr = R * .22f + 1;
      add({ x, y, x, y, rr, rr, 64, 32, 0xFF });
    }
  }
  // Gather list A into a drop at (gx, gy) over the first half, then split the
  // drop out into list B over the second; three drops orbit inside it
  void morphGather(const InkPrim *Al, int na, const InkPrim *Bl, int nb, float p, float gx, float gy, float tms) {
    bool half = p < .5f;
    float q = ease(half ? p * 2 : (1 - p) * 2);
    const InkPrim *L = half ? Al : Bl; int nl = half ? na : nb;
    for (int i = 0; i < nl; i++) add(toward(L[i], gx, gy, fmaxf(L[i].r1, L[i].r2) * .7f, q));
    float R = 13 * q;
    add({ gx, gy, gx, gy, R, R, gx, gy, 0xFF });
    for (int i = 0; i < 3; i++) {
      float a = tms * .006f + i * 2.1f, x = gx + cosf(a) * R * .55f, y = gy + sinf(a) * R * .45f, rr = R * .45f;
      add({ x, y, x, y, rr, rr, gx, gy, 0xFF });
    }
  }
  int n = 0;
  char shown[5] = "", prevDigs[5] = "";
  unsigned long chAt = 0;
  int lastSec = -1; unsigned long secAt = 0;
  int dx = 0, dy = 0;                        // the minute's layout drift

  void cost(uint32_t t0) { lastUs = micros() - t0; if (lastUs > maxUs) maxUs = lastUs; }

  // "HHMM" with a leading space for one-digit hours (12 h)
  // Read the clock directly: getLocalTime(&ti, 0) returns false whenever
  // millis() ticks before its first check, which flashed "--" now and then
  bool clockDigits(char *d, struct tm &ti) {
    time_t now = time(nullptr);
    localtime_r(&now, &ti);
    bool have = ti.tm_year > (2016 - 1900);
    if (!have) { strcpy(d, "  --"); return false; }
    int h = ti.tm_hour;
    if (!CLOCK_24H) { h %= 12; if (h == 0) h = 12; }
    snprintf(d, 5, "%2d%02d", h, ti.tm_min);
    return true;
  }

  // Milliseconds into the current second, so the spikes pop on the tick
  unsigned long subSecond(const struct tm &ti, unsigned long now) {
    if (ti.tm_sec != lastSec) { lastSec = ti.tm_sec; secAt = now; }
    return (now - secAt) % 1000;
  }

  // +-2 px each minute so static black never sits on the same pixels
  void driftFor(const struct tm &ti) {
    dx = (ti.tm_min % 5) - 2;
    dy = ((ti.tm_min / 5) % 3) - 1;
  }

  // ---- clock capsules ------------------------------------------------------
  // 7-segment skeleton in a unit box (y down): a b c d e f g
  static void segEnds(char s, float &x1, float &y1, float &x2, float &y2) {
    switch (s) {
      case 'a': x1 = 0; y1 = 0;  x2 = 1; y2 = 0;  break;
      case 'b': x1 = 1; y1 = 0;  x2 = 1; y2 = .5f; break;
      case 'c': x1 = 1; y1 = .5f; x2 = 1; y2 = 1;  break;
      case 'd': x1 = 0; y1 = 1;  x2 = 1; y2 = 1;  break;
      case 'e': x1 = 0; y1 = .5f; x2 = 0; y2 = 1;  break;
      case 'f': x1 = 0; y1 = 0;  x2 = 0; y2 = .5f; break;
      default:  x1 = 0; y1 = .5f; x2 = 1; y2 = .5f; break;   // g
    }
  }
  static const char *digitSegs(char ch) {
    static const char *const D[10] = { "abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc", "abcdefg", "abcdfg" };
    return (ch >= '0' && ch <= '9') ? D[ch - '0'] : (ch == '-' ? "g" : "");
  }

  // Digit geometry (Kevin, 2026-11-03: bigger, bolder, livelier, gooier).
  // Skeletons sit >= 11 px apart so neighbouring digits never neck together
  // (two strokes join while their field sum at the midpoint passes 0.3).
  static constexpr float DW = 16, DH = 38, DR = 4.4f, DY = 11;
  // Skeleton x of each digit slot: 4 digits, or 3 when the hour is one digit.
  // 14 px between neighbours: with the breathing and the wobble an 11 px gap
  // still let the 3 and the 9 neck together.
  static constexpr float XS4[4] = { 9, 39, 73, 103 }, XS3[4] = { 20, 20, 58, 88 };

  int clockPrims(InkPrim *out, const char *digs, unsigned long ms, unsigned long now) {
    const float y = DY + dy, t = (now % 600000UL) * 0.001f;
    bool four = digs[0] != ' ';
    const float *xs4 = XS4, *xs3 = XS3;
    const float *xs = four ? xs4 : xs3;
    float colX = (four ? 64 : 47) + dx;
    // Every 10 s the field surges: all spikes stand up together for 0.7 s
    float sp = (now % 10000UL) / 700.0f, surge = sp < 1 ? sinf(PI * sp) : 0;
    int k = 0;
    for (int i = 0; i < 4; i++) {
      char ch = digs[i];
      if (ch == ' ') continue;
      const char *segs = digitSegs(ch);
      if (!*segs) continue;
      float x = xs[i] + dx - (ch == '1' ? DW / 2 : 0), cx = x + DW / 2, cy = y + DH / 2;   // a 1 stands centred
      float r = DR * (1 + 0.06f * sinf(TWO_PI * 0.35f * t + i * 1.7f));   // the ink breathes
      for (const char *sg = segs; *sg && k < INK_MAX_PRIMS - 2; sg++) {
        float a, b, c, d; segEnds(*sg, a, b, c, d);
        int key = *sg - 'a';
        // Each stroke wanders half a pixel, so the digit never sits still
        float wx = 0.5f * sinf(1.3f * t + key * 1.7f + i), wy = 0.5f * cosf(1.1f * t + key * 2.3f + i);
        out[k++] = { x + a * DW + wx, y + b * DH + wy, x + c * DW - wy, y + d * DH + wx, r, r, cx, cy, (uint8_t)(i * 16 + key) };
      }
      // Two magnet spikes per digit: they breathe and sway, pop on each second
      // in a wave left to right, and surge with the field
      float tops[2], topY[2];
      if (strchr(segs, 'a')) { tops[0] = x + DW * .3f; tops[1] = x + DW * .75f; topY[0] = topY[1] = y; }
      else { tops[0] = strchr(segs, 'f') ? x : x + DW; tops[1] = x + DW; topY[0] = topY[1] = y; }
      for (int sI = 0; sI < 2; sI++) {
        if (sI == 1 && tops[1] == tops[0]) break;
        float tau = (float)((ms + 2000 - i * 70 - sI * 110) % 1000);
        // (the first ~4 px of a spike hide inside the stroke, so the rest pose is 4.5)
        float pop = 12.0f * fmaxf(0, expf(-tau / 220.0f) * cosf(tau / 95.0f));
        float hh = (4.5f + 1.8f * sinf(TWO_PI * 0.9f * t + i + sI * 2.1f) + pop) * (1 + 0.9f * surge);
        float lean = 1.8f * sinf(TWO_PI * 0.6f * t + i * 1.3f + sI);
        out[k++] = { tops[sI], topY[sI], tops[sI] + lean, topY[sI] - hh, 2.8f, 0.9f, cx, cy, (uint8_t)(i * 16 + 8 + sI) };
      }
    }
    // Colon: two drops that hop on the tick
    float msf = (float)ms;                    // (negating the unsigned ms itself wraps)
    float jump = 2.5f * fmaxf(0, expf(-msf / 150.0f) * cosf(msf / 70.0f));
    float cr = 3.0f * (1 + 0.08f * sinf(TWO_PI * 0.5f * t));
    out[k++] = { colX, y + 12 - jump, colX, y + 12 - jump, cr, cr, colX, y + 19, 0xF0 };
    out[k++] = { colX, y + 26 + jump * .5f, colX, y + 26 + jump * .5f, cr, cr, colX, y + 19, 0xF1 };
    return k;
  }

  // ---- drips: now and then a digit grows one from its bottom; it either gets
  // sucked back up or pinches off and falls through the date line ---------------
  struct Drip { bool on; float x, y0; unsigned long t0; bool falls; };
  Drip drips[2] = {};
  unsigned long nextDrip = 0;

  // ---- weather pieces --------------------------------------------------------
  bool flash = false;
  static constexpr float SX = 95, SY = 26;   // where the sky drop lives

  void tempPrims(int temp, float t) {
    char ts[6]; snprintf(ts, sizeof(ts), "%d", temp);
    int len = strlen(ts);
    // Skeletons 14 px apart (closer and the two digits merged into one blob)
    float w = len > 2 ? 9 : 12, h = 25, r = len > 2 ? 2.8f : 3.4f, gap = len > 2 ? 19 : 26, x = 5 + dx, y = 7 + dy;
    for (int i = 0; i < len && i < 3; i++) {
      const char *segs = digitSegs(ts[i]);
      float xi = x + i * gap - (ts[i] == '1' ? w / 2 : 0);
      float rr = r * (1 + 0.06f * sinf(TWO_PI * 0.35f * t + i * 1.7f));
      for (const char *sg = segs; *sg; sg++) {
        float a, b, c, d; segEnds(*sg, a, b, c, d);
        int key = *sg - 'a';
        float wx = 0.4f * sinf(1.3f * t + key * 1.7f + i), wy = 0.4f * cosf(1.1f * t + key * 2.3f + i);
        add({ xi + a * w + wx, y + b * h + wy, xi + c * w - wy, y + d * h + wx, rr, rr, 0, 0, 0xFF });
      }
    }
    float degX = x + len * gap - (len > 2 ? 2 : 4);
    float bob = 0.8f * sinf(TWO_PI * 0.7f * t);
    add({ degX, y + 2 + bob, degX, y + 2 + bob, 2.6f, 2.6f, 0, 0, 0xFF });       // the degree is a drop
  }

  // A cloud of four drops that boils slowly
  void cloud(float y, float k, float t) {
    static const float L[4][3] = { { -9, 4, 6.5f }, { 0, 0, 8.5f }, { 9, 4, 6.5f }, { 0, 6, 6 } };
    for (int i = 0; i < 4; i++) {
      float rr = (L[i][2] + 0.8f * sinf(t * 2.0f + i * 2)) * k;
      float x = SX + L[i][0] * k + 2 * sinf(t * 0.5f), yy = y + L[i][1] * k;
      add({ x, yy, x, yy, rr, rr, 0, 0, 0xFF });
    }
  }

  void skyPrims(uint8_t sky, bool day, float t, unsigned long now) {
    switch (sky) {
      case SKY_CLEAR: {
        float bob = 1.2f * sinf(t * 2.1f);
        add({ SX, SY + bob, SX, SY + bob, 12, 12, 0, 0, 0xFF });
        if (!day) break;                                     // night: the moon (bite in skyPost)
        for (int k = 0; k < 6; k++) {                        // magnet rays that turn and pulse
          float a = t * 0.4f + k * PI / 3, L = 4.5f + 3.5f * (.5f + .5f * sinf(t * 4 + k * 1.7f));
          add({ SX + cosf(a) * 9, SY + bob + sinf(a) * 9, SX + cosf(a) * (12 + L), SY + bob + sinf(a) * (12 + L), 2.5f, 0.9f, 0, 0, 0xFF });
        }
        break;
      }
      case SKY_CLOUDY: cloud(22, 1.15f, t); break;
      case SKY_RAIN: {
        cloud(12, 0.85f, t);
        add({ 76, 61, 115, 61, 2, 2, 0, 0, 0xFF });          // the puddle
        for (int i = 0; i < 4; i++) {
          float u = fmodf(t / 1.5f + i * .27f + i * 0.07f, 1.0f), x = 86 + i * 7;
          if (u < .3f) { float rr = .8f + 1.6f * u / .3f; add({ x, 20 + u / .3f * 2, x, 20 + u / .3f * 2, rr, rr, 0, 0, 0xFF }); }
          else {
            float v = (u - .3f) / .7f, y = 23 + 36 * v * v;
            if (v < .9f) add({ x, y - 4 * v, x, y, 1.5f, 2.2f, 0, 0, 0xFF });
            else { float rr = 3.4f * (1 - (v - .9f) / .1f); add({ x, 59.5f, x, 59.5f, rr, rr, 0, 0, 0xFF }); }   // splash into the puddle
          }
        }
        break;
      }
      case SKY_SNOW: {
        cloud(12, 0.85f, t);
        float pile = 1.4f + 1.6f * ((now % 16000UL) / 16000.0f);   // the drift builds, then resets
        add({ 78, 61.5f, 112, 61.5f, pile, pile, 0, 0, 0xFF });
        break;
      }
      case SKY_STORM: {
        cloud(13, 1.05f, t);
        for (int i = 0; i < 4; i++) {                        // the cloud bristles
          float x = 86 + i * 6, L = 4 + 3.5f * fabsf(sinf(t * 13 + i * 1.9f));
          add({ x, 21, x - 1, 21 + L, 2.4f, 0.9f, 0, 0, 0xFF });
        }
        unsigned long ph = now % 2600UL;
        if (ph < 170) {                                      // a lightning tendril
          static const float z[5][2] = { { 97, 24 }, { 90, 35 }, { 99, 39 }, { 90, 51 }, { 95, 60 } };
          for (int i = 0; i < 4; i++) add({ z[i][0], z[i][1], z[i + 1][0], z[i + 1][1], 1.4f, 1.4f, 0, 0, 0xFF });
          flash = ph < 70;
        }
        break;
      }
      case SKY_FOG: {
        add({ SX, SY, SX, SY, 11, 11, 0, 0, 0xFF });
        for (int i = 0; i < 2; i++) { float a = t * 1.2f + i * PI; add({ SX + cosf(a) * 9, SY + sinf(a) * 5, SX + cosf(a) * 9, SY + sinf(a) * 5, 5, 5, 0, 0, 0xFF }); }
        break;
      }
      case SKY_WIND: {
        float g = gust(t);
        add({ SX - 9, SY, SX + 6, SY - 1, 8.5f, 8.5f, 0, 0, 0xFF });   // a drop stretched by the wind
        for (int i = 0; i < 3; i++) {
          float yy = SY - 6 + i * 6;
          add({ SX + 5, yy, SX + 17 + 4 * g, yy - 1 + 2 * g * (i - 1), 2.4f, 0.9f, 0, 0, 0xFF });
        }
        float u = fmodf(t / 1.7f, 1.0f), rr = 2.4f * (1 - u * .3f);
        add({ SX + 12 + u * 30, SY + sinf(u * 7) * 3, SX + 12 + u * 30, SY + sinf(u * 7) * 3, rr, rr, 0, 0, 0xFF });   // one torn off
        break;
      }
    }
  }

  static float gust(float t) { return sinf(t * 1.7f) * .6f + sinf(t * 4.3f) * .4f; }

  void skyPost(uint8_t *buf, uint8_t sky, bool day, float t, unsigned long now, bool fx = true) {
    if (sky == SKY_CLEAR) {
      float bob = 1.2f * sinf(t * 2.1f);
      if (!day) {                                            // a bite out of the drop: the moon
        float mx = SX + 6, my = SY + bob - 4;
        for (int y = -10; y <= 10; y++) for (int x = -10; x <= 10; x++) if (x * x + y * y <= 90) setPx(buf, (int)mx + x, (int)my + y, true);
      } else {                                               // the glint at 10:30
        for (int a = 205; a <= 245; a += 3) {
          float rr = (a > 218 && a < 232) ? 7 : 7.4f, c = cosf(a * DEG_TO_RAD), s = sinf(a * DEG_TO_RAD);
          setPx(buf, (int)(SX + c * rr), (int)(SY + bob + s * rr), true);
          if (a > 218 && a < 232) setPx(buf, (int)(SX + c * 7.8f), (int)(SY + bob + s * 7.8f), true);
        }
      }
    }
    if (sky == SKY_SNOW) {                                   // filings drift down onto the pile
      for (int i = 0; i < 9; i++) {
        float u = fmodf(t / 4.2f + i * .113f + i * .03f, 1.0f);
        int y = (int)(21 + u * 36), x = (int)(78 + i * 4 + 3 * sinf(t * 2 + i));
        setPx(buf, x, y, false); if (y > 52) setPx(buf, x + 1, y, false); else setPx(buf, x, y + 1, false);
      }
    }
    if (sky == SKY_FOG && fx) {                              // white bands drift through everything
      int off = (int)(now / 220) % 4;
      for (int y = 0; y < OLED_HEIGHT; y++) if ((y + off) % 4 == 0) for (int x = 0; x < OLED_WIDTH; x++) setPx(buf, x, y, true);
    }
  }

  // Wind shears the whole picture with each gust (rows slide, more at the top)
  void shear(uint8_t *buf, float t) {
    float g = gust(t);
    static uint8_t src[OLED_WIDTH * OLED_HEIGHT / 8];
    memcpy(src, buf, sizeof(src));
    for (int y = 0; y < OLED_HEIGHT; y++) {
      int sh = (int)lroundf((32 - y) / 32.0f * 3 * g);
      if (!sh) continue;
      uint8_t bit = 1 << (y & 7);
      for (int x = 0; x < OLED_WIDTH; x++) {
        int sx = x - sh;
        bool white = (sx < 0 || sx >= OLED_WIDTH) ? true : (src[(y >> 3) * OLED_WIDTH + sx] & bit);
        if (white) buf[(y >> 3) * OLED_WIDTH + x] |= bit; else buf[(y >> 3) * OLED_WIDTH + x] &= ~bit;
      }
    }
  }

  void stepDrips(const char *digs, unsigned long now) {
    if ((long)(now - nextDrip) >= 0) {
      nextDrip = now + random(2500, 6001);
      for (auto &d : drips) {
        if (d.on) continue;
        bool four = digs[0] != ' ';
        const float *xs4 = XS4, *xs3 = XS3;
        int slot = random(four ? 0 : 1, 4);
        const char *segs = digitSegs(digs[slot]);
        if (!*segs) break;
        float x = (four ? xs4 : xs3)[slot] + dx - (digs[slot] == '1' ? DW / 2 : 0);
        if (strchr(segs, 'd')) d.x = x + DW * (0.25f + 0.5f * random(100) / 100.0f);
        else d.x = strchr(segs, 'c') ? x + DW : x;
        d.y0 = DY + dy + DH;
        d.t0 = now; d.falls = random(100) < 45; d.on = true;
        break;
      }
    }
    for (auto &d : drips) {
      if (!d.on) continue;
      float t = (now - d.t0);
      float L = 7.0f * ease(t / 700.0f) + (t > 700 ? 0.8f * sinf((t - 700) / 90.0f) : 0);
      float bulb = 1.5f + 1.6f * ease(t / 700.0f);
      if (!d.falls) {
        if (t > 1200) { float q = ease((t - 1200) / 500.0f); L *= 1 - q; bulb *= 1 - .7f * q; }
        if (t > 1700) { d.on = false; continue; }
        add({ d.x, d.y0 - 2, d.x, d.y0 + L, 2.6f, 1.4f, d.x, d.y0, 0xFF });
        add({ d.x, d.y0 + L, d.x, d.y0 + L, bulb, bulb, d.x, d.y0, 0xFF });
      } else {
        if (t < 1350) {
          float neck = t > 1200 ? 1.4f - 1.1f * (t - 1200) / 150.0f : 1.4f;
          add({ d.x, d.y0 - 2, d.x, d.y0 + L, 2.6f, neck, d.x, d.y0, 0xFF });
          add({ d.x, d.y0 + L, d.x, d.y0 + L, bulb, bulb, d.x, d.y0, 0xFF });
        } else {
          float ft = (t - 1350) * 0.001f, yy = d.y0 + 7 + 150.0f * ft * ft, v = 300.0f * ft;
          if (yy > 72) { d.on = false; continue; }
          float st = fminf(v * 0.012f, 3.0f);          // it stretches as it speeds up
          add({ d.x, yy - st, d.x, yy, 2.4f, 2.8f, d.x, yy, 0xFF });
        }
      }
    }
  }

  static float ease(float t) { t = constrain(t, 0.0f, 1.0f); return t < .5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); }
  static InkPrim toward(const InkPrim &a, float x, float y, float r, float q) {
    InkPrim o = a;
    o.x1 += (x - a.x1) * q; o.y1 += (y - a.y1) * q; o.x2 += (x - a.x2) * q; o.y2 += (y - a.y2) * q;
    o.r1 += (r - a.r1) * q; o.r2 += (r - a.r2) * q;
    return o;
  }

  // Shared keys slide; lost strokes drain into their digit's centre, new ones
  // grow out of it, with a pool swelling there mid-change
  void morphKeyed(const InkPrim *A, int na, const InkPrim *B, int nb, float q) {
    n = 0;
    float e = ease(q);
    bool changed[4] = { false, false, false, false };
    for (int i = 0; i < na; i++) {
      int j = findKey(B, nb, A[i].key);
      if (j >= 0) {
        InkPrim o = A[i];
        o.x1 += (B[j].x1 - A[i].x1) * e; o.y1 += (B[j].y1 - A[i].y1) * e;
        o.x2 += (B[j].x2 - A[i].x2) * e; o.y2 += (B[j].y2 - A[i].y2) * e;
        o.r1 += (B[j].r1 - A[i].r1) * e; o.r2 += (B[j].r2 - A[i].r2) * e;
        add(o);
      } else {
        if (A[i].key < 64) changed[A[i].key >> 4] = true;
        if (q < .6f) add(toward(A[i], A[i].cx, A[i].cy, .4f, ease(q / .6f)));
      }
    }
    for (int j = 0; j < nb; j++) {
      if (findKey(A, na, B[j].key) >= 0) continue;
      if (B[j].key < 64) changed[B[j].key >> 4] = true;
      if (q > .35f) add(toward(B[j], B[j].cx, B[j].cy, .4f, 1 - ease((q - .35f) / .65f)));
    }
    float pr = 5.5f * sinf(PI * q);
    for (int s = 0; s < 4; s++) {
      if (!changed[s]) continue;
      for (int j = 0; j < nb; j++) if (B[j].key < 64 && (B[j].key >> 4) == s) { add({ B[j].cx, B[j].cy, B[j].cx, B[j].cy, pr, pr, B[j].cx, B[j].cy, 0xFF }); break; }
    }
  }
  static int findKey(const InkPrim *L, int nl, uint8_t key) { for (int i = 0; i < nl; i++) if (L[i].key == key) return i; return -1; }
  void add(const InkPrim &p) { if (n < INK_MAX_PRIMS && (p.r1 > .05f || p.r2 > .05f)) P[n++] = p; }

  // ---- the field -----------------------------------------------------------
  // Each capsule adds (1 - u)^2, u = d^2 / (r FK)^2, inside its box; the ink
  // is where the sum passes 0.3. Returns the number of black pixels.
  int renderField(uint8_t *buf) {
    static uint16_t f[OLED_WIDTH * OLED_HEIGHT];
    memset(f, 0, sizeof(f));
    int bx0 = OLED_WIDTH, by0 = OLED_HEIGHT, bx1 = -1, by1 = -1;   // union of the boxes
    for (int i = 0; i < n; i++) {
      const InkPrim &q = P[i];
      float R = fmaxf(q.r1, q.r2) * INK_FK;
      int x0 = max(0, (int)floorf(fminf(q.x1, q.x2) - R)), x9 = min(OLED_WIDTH - 1, (int)ceilf(fmaxf(q.x1, q.x2) + R));
      int y0 = max(0, (int)floorf(fminf(q.y1, q.y2) - R)), y9 = min(OLED_HEIGHT - 1, (int)ceilf(fmaxf(q.y1, q.y2) + R));
      if (x0 > x9 || y0 > y9) continue;
      bx0 = min(bx0, x0); by0 = min(by0, y0); bx1 = max(bx1, x9); by1 = max(by1, y9);
      float dxs = q.x2 - q.x1, dys = q.y2 - q.y1, L2 = dxs * dxs + dys * dys, inv = L2 > 1e-6f ? 1.0f / L2 : 0;
      // h = ((x+.5-x1) dx + (y+.5-y1) dy) / L2, stepped in 16.16
      int32_t hStep = (int32_t)(dxs * inv * 65536.0f);
      int32_t DX = (int32_t)(dxs * 256.0f), DY = (int32_t)(dys * 256.0f);   // Q8
      float ra = fmaxf(q.r1, .3f) * INK_FK, rb = fmaxf(q.r2, .3f) * INK_FK;
      int32_t IRa = (int32_t)(4096.0f / (ra * ra));   // 1 / R^2, Q12 per px^2, at each end
      int32_t IRb = (int32_t)(4096.0f / (rb * rb));
      int32_t R2max = (int32_t)(fmaxf(ra, rb) * fmaxf(ra, rb) * 4096.0f);   // Q12: outside this, nothing adds
      for (int y = y0; y <= y9; y++) {
        float py = y + .5f - q.y1;
        int32_t PY = (int32_t)(py * 256.0f);
        float px0 = x0 + .5f - q.x1;
        int32_t H = (int32_t)((px0 * dxs + py * dys) * inv * 65536.0f);
        int32_t PX = (int32_t)(px0 * 256.0f);
        uint16_t *row = &f[y * OLED_WIDTH];
        for (int x = x0; x <= x9; x++, H += hStep, PX += 256) {
          // All 32-bit: |ex|, |ey| < 50 px in Q8, so ex^2 + ey^2 < 2^31
          int32_t h = H < 0 ? 0 : (H > 65536 ? 65536 : H);
          int32_t ex = PX - ((h * DX) >> 16);
          int32_t ey = PY - ((h * DY) >> 16);
          int32_t d2q = (ex * ex + ey * ey) >> 4;                              // d^2, Q12 px^2
          if (d2q >= R2max) continue;                                          // past the widest end
          int32_t IR = IRa + (((IRb - IRa) * (h >> 4)) >> 12);                 // 1 / R^2 along the capsule
          int32_t u = (d2q >> 6) * IR >> 6;                                    // d^2 / R^2, Q12
          if (u >= 4096) continue;
          int32_t k = 4096 - u;
          int32_t v = row[x] + ((k * k) >> 12);
          row[x] = v > 65535 ? 65535 : v;
        }
      }
    }
    if (bx1 < 0) return 0;
    int ink = 0;
    for (int y = by0; y <= by1; y++) {
      uint8_t bit = 1 << (y & 7);
      uint8_t *page = &buf[(y >> 3) * OLED_WIDTH];
      const uint16_t *row = &f[y * OLED_WIDTH];
      for (int x = bx0; x <= bx1; x++) if (row[x] > INK_THRESH) { page[x] &= ~bit; ink++; }
    }
    return ink;
  }
};

InkTime ink;

#endif // INK_TIME_H
