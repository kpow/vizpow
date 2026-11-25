// ============================================================================
// vizMini — a pocket vizbot for the ESP32-C3 Super Mini + 1.3" mono OLED
// ============================================================================
// Single-core cooperative loop: the web server and the face renderer share one
// core, interleaved every iteration so web control stays responsive while the
// face animates at ~25 FPS.
//
// The face engine (tween.h / bot_faces.h / bot_eyes.h / bot_eye_model.h /
// bot_eye_params.h) is the vizbot renderer, copied unmodified apart from
// bot_faces.h dropping its bot_sounds.h include (no sound here). gfx_mono.h
// adapts its drawing primitives onto a 1-bit U8g2 buffer. See README.md.
// ============================================================================

#include "config.h"
#include "gfx_mono.h"          // defines GfxDevice + the u8g2 display object

// --- globals the reused renderer (bot_eyes.h) expects to find ---------------
GfxDevice  gfxDev;
GfxDevice *gfx = &gfxDev;       // bot_eyes.h draws through this
float      accelX = 0, accelY = 0, accelZ = 1.0f;  // no IMU on this board (unused)
uint8_t    botBackgroundStyle = 0;                 // 0 = no ambient stroke

#include "bot_eyes.h"          // renderBotFace() + the parametric eye model (botEyes)
#include "face_motion.h"       // whole-face rotation (rare tilts, spins, flips)
#include "ferro_blob.h"        // ferro mode: black ferrofluid drop on a white screen
#include "touch_input.h"
#include "strip_leds.h"
#include "weather.h"
#include "ink_time.h"          // Ink Time: the clock and weather as ferrofluid
#include "device_name.h"       // his name: hotspot, <name>.local, VizCloud
#include "web_ui.h"            // also brings in cloud_client.h (VizCloud)
#include "sleep_schedule.h"    // when he sleeps (shared with vizbot, host-tested)
#include <time.h>

// Top-level interaction mode
enum AppMode : uint8_t { MODE_FACE = 0, MODE_INFO, MODE_FERRO };
enum InfoView : uint8_t { VIEW_TIME = 0, VIEW_WEATHER };
AppMode  g_mode = MODE_FACE;
InfoView g_infoView = VIEW_TIME;
unsigned long g_lastPressMs = 0;       // for double-tap detection
unsigned long g_viewSwitchMs = 0;      // when the info view last changed (auto-flip timer)

// ============================================================================
// Runtime state
// ============================================================================
BotFaceState  face;    // expression tween: mouth + brows (eyes live in botEyes)
TouchInput    touch;

uint8_t  g_expr      = EXPR_NEUTRAL;
uint8_t  g_prevExpr  = EXPR_NEUTRAL;   // expression to restore after a startle
bool     g_snow      = SNOW_DEFAULT_ON;
uint8_t  g_contrast  = DEFAULT_CONTRAST;

char          g_sayText[49] = {0};
unsigned long g_sayUntil    = 0;
unsigned long g_startleUntil = 0;
unsigned long g_lastInputMs = 0;
unsigned long g_lastFrame   = 0;
uint32_t      g_faceUs      = 0;      // last face render time, shown in /state
uint32_t      g_sendUs      = 0;      // last OLED transfer time
unsigned long g_frameMs     = 0;      // last frame-to-frame time

// Sleep (owner: the "Sleep" section below). g_lastInputMs counts web and cloud
// too; bedtime is held off only by a person at the button (g_lastHumanMs).
sleepsched::Machine g_sleep;
unsigned long g_lastHumanMs = 0;
unsigned long g_peekUntil   = 0;      // asleep: eyes open until then
inline bool asleep() { return g_sleep.asleep(); }
unsigned long g_prevFrameAt = 0;

// ---- snow particles --------------------------------------------------------
struct Flake { int16_t x; int16_t y; uint8_t spd; };
static const uint8_t SNOW_N = 14;
Flake snow[SNOW_N];

void snowInit() {
  for (uint8_t i = 0; i < SNOW_N; i++) {
    snow[i] = { (int16_t)random(0, OLED_WIDTH), (int16_t)random(0, OLED_HEIGHT),
                (uint8_t)random(1, 3) };
  }
}
void snowDrawAndStep() {
  for (uint8_t i = 0; i < SNOW_N; i++) {
    u8g2.setDrawColor(1);
    u8g2.drawPixel(snow[i].x, snow[i].y);
    snow[i].y += snow[i].spd;
    if ((millis() / 200 + i) % 3 == 0) snow[i].x += (i & 1) ? 1 : -1;
    if (snow[i].y >= OLED_HEIGHT) {
      snow[i].y = 0;
      snow[i].x = random(0, OLED_WIDTH);
    }
    if (snow[i].x < 0) snow[i].x = OLED_WIDTH - 1;
    if (snow[i].x >= OLED_WIDTH) snow[i].x = 0;
  }
}

// ============================================================================
// Expression / sleep helpers
// ============================================================================
void applyExpression(uint8_t v) {
  if (v >= BOT_NUM_EXPRESSIONS) v = EXPR_NEUTRAL;
  g_expr = v;
  face.transitionTo(v);
  leds.setMood(v);
  motion.onExpression(v);
}

// After a double spin: dizzy for a moment, then back (rides the startle restore)
void dizzyAfterSpin() {
  if (!g_startleUntil) g_prevExpr = g_expr;
  face.transitionTo(EXPR_DIZZY);
  leds.setMood(EXPR_DIZZY);
  g_startleUntil = millis() + MOTION_DIZZY_MS;
}

void startle() {
  g_prevExpr = g_expr;
  face.transitionTo(EXPR_SURPRISED, 120);
  leds.setMood(EXPR_SURPRISED);
  g_startleUntil = millis() + STARTLE_MS;
}

// ---- Time + Weather (info) mode -------------------------------------------
// Ink Time: the clock and the weather as ferrofluid on a white lightbox.
// Entering, flipping and leaving all move through the drop (ink_time.h); while
// his face shows over the drop it is drawn on top here.
void renderInfo() {
  unsigned long now = millis();
  ink.render(u8g2.getBufferPtr(), now, weatherNow());
  bool shut;
  if (ink.faceOver(now, shut)) {
    tweenManager.update();
    face.update();
    botEyes.sleeping = shut;
    botEyes.step(now);
    renderMiniFace();
  }
  u8g2.sendBuffer();
  if (ink.exitDone()) {                  // swelled back into the face
    g_mode = MODE_FACE;
    botEyes.sleeping = false;
    applyExpression(g_expr);
  }
}

// Flip Time <-> Weather through the drop and restart the auto-switch timer
void toggleInfoView() {
  g_infoView = (g_infoView == VIEW_TIME) ? VIEW_WEATHER : VIEW_TIME;
  g_viewSwitchMs = millis();
  ink.startFlip(g_infoView == VIEW_WEATHER ? InkTime::SC_WEATHER : InkTime::SC_CLOCK, millis());
}

void enterInfo(bool weather = false) {
  g_mode = MODE_INFO;
  g_infoView = weather ? VIEW_WEATHER : VIEW_TIME;   // the clock, unless asked for the weather
  g_viewSwitchMs = millis();
  g_lastInputMs = millis();
  ink.startEnter(weather ? InkTime::SC_WEATHER : InkTime::SC_CLOCK, millis());
}

// Leave through the drop: the face takes over when it has swelled back
void exitInfo() {
  if (g_mode == MODE_INFO) ink.startExit(millis());
}

// Cut straight back (party mode and motion tests take the screen now)
void exitInfoNow() {
  if (g_mode != MODE_INFO) return;
  g_mode = MODE_FACE;
  botEyes.sleeping = false;
  applyExpression(g_expr);
}

// Draw the face at FACE_SCALE about the OLED face centre, like vizbot's
// renderBotFaceAt(): the eye model scales itself, the scaled copy carries the mouth.
void renderFaceAt(int16_t cx, int16_t cy, float k) {
  BotFaceState f = face;
  auto sc = [k](int16_t v) { return (int16_t)lroundf(v * k); };
  f.mouthWidth = sc(f.mouthWidth);
  f.mouthOffsetY = sc(f.mouthOffsetY);
  f.mouthCurve = sc(f.mouthCurve);
  botFaceDrawCX = cx;
  botFaceDrawCY = cy;
  botFaceDrawScale = k;
  renderBotFace(f, /*bgColor=*/0x0000);
}
void renderMiniFace() { renderFaceAt(FACE_ORIGIN_X, FACE_ORIGIN_Y, FACE_SCALE); }

// ============================================================================
// Web control hooks (declared extern in web_ui.h)
// ============================================================================
// ---- Ferro mode -------------------------------------------------------------
// The panel's "contrast" is segment current (brightness). The white field
// lights ~3x the pixels of the face, so ferro runs it at 0.55x the user's
// value, ramped one step per frame (art direction §3.2).
uint8_t g_panelContrast = DEFAULT_CONTRAST;   // what the panel is set to right now

// Brightness follows the morph: lerp(face, ferro, P) (art direction §6.1)
uint8_t panelContrastTarget() {
  if (asleep()) return SLEEP_CONTRAST;
  if (g_mode == MODE_INFO) {                            // the ink screens' white lightbox
    float inkC = constrain(g_contrast * FERRO_CONTRAST_K, (float)FERRO_CONTRAST_MIN, (float)FERRO_CONTRAST_MAX);
    return (uint8_t)lroundf(g_contrast + (inkC - g_contrast) * ink.brightP(millis()));
  }
  if (g_mode != MODE_FERRO) return g_contrast;
  float ferroC = constrain(g_contrast * FERRO_CONTRAST_K, (float)FERRO_CONTRAST_MIN, (float)FERRO_CONTRAST_MAX);
  return (uint8_t)lroundf(g_contrast + (ferroC - g_contrast) * ferro.brightP());
}

// Up to 8 register steps a frame: keeps pace with a morph, never jumps
void stepPanelContrast() {
  int t = panelContrastTarget(), c = g_panelContrast;
  if (c == t) return;
  c += constrain(t - c, -8, 8);
  g_panelContrast = (uint8_t)c;
  gfxDev.setContrast(g_panelContrast);
}

// ---- Ferro visits: off / on / auto (Kevin: auto) ------------------------------
// auto: a ~60 s visit every 8-15 min of face mode. on: he stays a drop.
enum FerroSetting : uint8_t { FERRO_SET_OFF = 0, FERRO_SET_ON, FERRO_SET_AUTO };
static const char *const ferroSetNames[3] = { "off", "on", "auto" };
uint8_t       g_ferroSet     = FERRO_SET_AUTO;
unsigned long g_nextVisitAt  = 0;   // auto: when the next visit starts
unsigned long g_visitEndAt   = 0;   // when this visit leaves (0 = stays)

void scheduleVisit() {
  g_nextVisitAt = millis() + (unsigned long)random(FERRO_VISIT_GAP_MIN_S, FERRO_VISIT_GAP_MAX_S + 1) * 1000UL;
}

void enterFerro(unsigned long stayMs = 0) {
  if (g_mode == MODE_INFO) exitInfoNow();
  g_visitEndAt = stayMs ? millis() + stayMs : 0;
  if (g_mode == MODE_FERRO) return;
  g_mode = MODE_FERRO;
  g_startleUntil = 0;
  botEyes.sleeping = false;
  ferro.enter(millis());
}

// Leave on the morph (the drop swells back into the face)
void exitFerro() {
  if (g_mode != MODE_FERRO) return;
  ferro.requestExit(millis());
}

// Cut straight back (info mode, rotation tests)
void exitFerroNow() {
  if (g_mode != MODE_FERRO) return;
  ferro.leave();
  botEyes.sleeping = false;
  g_mode = MODE_FACE;
  g_visitEndAt = 0;
  applyExpression(g_expr);
  if (g_ferroSet == FERRO_SET_AUTO) scheduleVisit();
}

void setFerroSetting(uint8_t v) {
  if (v > FERRO_SET_AUTO) return;
  g_ferroSet = v;
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putUChar("ferro", v);
  p.end();
  if (v == FERRO_SET_ON) enterFerro();
  else if (v == FERRO_SET_OFF) exitFerro();
  else { if (g_mode == MODE_FERRO && !g_visitEndAt) g_visitEndAt = millis() + FERRO_VISIT_MS; scheduleVisit(); }
}

void ctlSetFerro(bool on) { if (asleep()) return; g_lastInputMs = millis(); if (on) enterFerro(); else exitFerro(); }
void ctlFerroSetting(const String &v) {
  g_lastInputMs = millis();
  for (uint8_t i = 0; i < 3; i++) if (v == ferroSetNames[i]) setFerroSetting(i);
}
void ctlFerroVisit() { if (asleep()) return; g_lastInputMs = millis(); enterFerro(FERRO_VISIT_MS); }
// Party patterns and tentacle moves, by vizMini name or the nearest vizBot
// name (breath, crown, calm, feelers, long, big)
void ctlFerroPlay(const String &pat, const String &tent) {
  if (asleep()) return;
  g_lastInputMs = millis();
  if (g_mode != MODE_FERRO) enterFerro();
  String p = pat, t = tent;
  if (p == "breath") p = "breathe"; else if (p == "crown") p = "crown5"; else if (p == "calm") p = "crown3";
  else if (p == "feelers") { p = ""; t = "feeler"; } else if (p == "long") { p = ""; t = "reach"; }
  if (t == "big") t = "reach";
  for (uint8_t i = 1; i < FP_NUM; i++) if (p == ferroPatternNames[i]) ferro.startPattern(i, millis());
  static const char *const kinds[4] = { "reach", "feeler", "flick", "arm" };
  for (uint8_t i = 0; i < 4; i++) if (t == kinds[i]) ferro.tentMove(i, millis());
}
void ctlPartyStack(const String &v) {
  for (uint8_t i = 0; i < 3; i++) if (v == ferroStackNames[i]) {
    ferro.stack = i;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar("pStack", i);
    p.end();
  }
}
void ctlSetExpression(uint8_t v) { if (asleep()) return; g_lastInputMs = millis(); if (g_mode == MODE_INFO) exitInfo(); applyExpression(v); }
void ctlSetInfo(bool on) {
  if (asleep()) return;
  g_lastInputMs = millis();
  if (g_mode == MODE_FERRO) exitFerroNow();
  if (on) {
    if (g_mode == MODE_INFO) toggleInfoView();
    else enterInfo();
  } else if (g_mode == MODE_INFO) exitInfo();
}
// Test switch: show the ink weather as one sky (clear, cloudy, rain, snow,
// storm, fog, wind) instead of the real one; "real" or anything else clears it
void ctlInfoSky(const String &v) {
  static const char *const names[SKY_NUM] = { "clear", "cloudy", "rain", "snow", "storm", "fog", "wind" };
  ink.forceSky = 0xFF;
  for (uint8_t i = 0; i < SKY_NUM; i++) if (v == names[i]) ink.forceSky = i;
  if (ink.forceSky != 0xFF && !asleep()) {
    if (g_mode != MODE_INFO) enterInfo(true);
    else if (g_infoView != VIEW_WEATHER) toggleInfoView();
    g_viewSwitchMs = millis();
  }
}
void ctlSetSnow(bool on)         { g_lastInputMs = millis(); g_snow = on; }
void ctlSpark()                  { if (asleep()) return; g_lastInputMs = millis(); leds.spark(); }
void ctlMotion(int level, int move) {
  g_lastInputMs = millis();
  if (level >= 0) motion.setLevel((uint8_t)level);
  if (move > 0 && !asleep()) {
    if (g_mode == MODE_INFO) exitInfoNow();
    if (g_mode == MODE_FERRO) exitFerroNow();   // stance moves for ferro are phase F3
    motion.start((uint8_t)move, /*force=*/true);
  }
}
void ctlSetLeds(int mode, int bri) {
  g_lastInputMs = millis();
  if (mode >= 0) leds.setMode((uint8_t)mode);
  if (bri >= 0)  leds.setBrightness((uint8_t)bri);
}
void ctlSetContrast(uint8_t c) {
  g_lastInputMs = millis();
  g_contrast = c;
  g_panelContrast = panelContrastTarget();   // a slider move jumps straight there
  gfxDev.setContrast(g_panelContrast);
}
void ctlSay(const char* text) {
  if (asleep()) return;   // no words in his sleep
  g_lastInputMs = millis();
  strncpy(g_sayText, text, sizeof(g_sayText) - 1);
  g_sayText[sizeof(g_sayText) - 1] = 0;
  g_sayUntil = millis() + SAY_DEFAULT_MS;
}
// ============================================================================
// Sleep — the only code that puts him to sleep or wakes him (docs/plan-sleep.md)
// ============================================================================
// sleep_schedule.h decides when. Here: the clock, persistence and effects.
// Asleep: eyes shut (a press peeks), panel eased to SLEEP_CONTRAST, strip dark,
// no party visits, moves, sayings or info screens. Saved settings stay as they
// are, so the morning puts everything back. Loop thread only (the web server
// runs from webLoop() on it too).
static int16_t  g_sleepMod = sleepsched::kTimeUnknown;   // last minute-of-day read
static uint32_t g_sleepClockAt = 0;
static bool     g_sleepClockRead = false;

// Local minute-of-day, or unknown before NTP (the hotspot never gets one).
// time() directly, as clockDigits() does: getLocalTime(&t, 0) can miss a set clock.
static int16_t sleepReadClock() {
  time_t now = time(nullptr);
  struct tm t;
  if (localtime_r(&now, &t) == nullptr || t.tm_year < (2024 - 1900)) return sleepsched::kTimeUnknown;
  return sleepsched::minuteOfDay(t.tm_hour, t.tm_min);
}

static void sleepSave() {
  const sleepsched::Config& c = g_sleep.config();
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBool  ("slpOn",   c.enabled);
  p.putUShort("slpAt",   c.sleepMin);
  p.putUShort("slpWake", c.wakeMin);
  p.putUChar ("slpNap",  c.napMin);
  p.end();
}

static void sleepLoad() {
  sleepsched::Config c = sleepsched::kDefaults;
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  c.enabled  = p.getBool  ("slpOn",   c.enabled);
  c.sleepMin = p.getUShort("slpAt",   c.sleepMin);
  c.wakeMin  = p.getUShort("slpWake", c.wakeMin);
  c.napMin   = p.getUChar ("slpNap",  c.napMin);
  p.end();
  g_sleep.begin(c, millis());   // sanitizes anything out of range
}

static void sleepApply(sleepsched::Change c) {
  if (c == sleepsched::Change::FellAsleep) {
    motion.paused = true;                    // first: the exits below re-apply his expression
    exitInfoNow();
    exitFerroNow();
    g_startleUntil = 0;
    g_sayText[0] = 0;
    g_peekUntil = 0;
    face.transitionTo(EXPR_CHILL, 800);
    leds.asleep = true;
    Serial.println("[sleep] asleep");
  } else if (c == sleepsched::Change::WokeUp) {
    leds.asleep = false;
    motion.paused = false;
    g_lastInputMs = millis();
    startle();                               // a little "oh!" on waking, then his face back
    if (g_ferroSet == FERRO_SET_ON) enterFerro();
    else if (g_ferroSet == FERRO_SET_AUTO) scheduleVisit();
    Serial.println("[sleep] awake");
  }
}

void sleepTick() {
  const uint32_t now = millis();
  if (!g_sleepClockRead || now - g_sleepClockAt >= SLEEP_CLOCK_POLL_MS) {
    g_sleepMod = sleepReadClock();
    g_sleepClockAt = now;
    g_sleepClockRead = true;
  }
  g_sleep.noteActivity(g_lastHumanMs);
  sleepApply(g_sleep.update(g_sleepMod, now));
}

// Web/cloud: fields < 0 are left as they are; act 1 = sleep now, 0 = wake, -1 = none.
// The handler has validated every value.
void ctlSleep(int on, int atMin, int wakeMin, int nap, int act) {
  if (on >= 0 || atMin >= 0 || wakeMin >= 0 || nap >= 0) {
    sleepsched::Config c = g_sleep.config();
    if (on >= 0)      c.enabled  = on != 0;
    if (atMin >= 0)   c.sleepMin = (uint16_t)atMin;
    if (wakeMin >= 0) c.wakeMin  = (uint16_t)wakeMin;
    if (nap >= 0)     c.napMin   = (uint8_t)nap;
    g_sleep.setConfig(c);
    sleepSave();
  }
  if (act == 1) g_sleep.sleepNow();
  else if (act == 0) g_sleep.wakeNow();
  sleepTick();   // apply now, so the reply shows the new state
}

String sleepJson() {
  const sleepsched::Config& c = g_sleep.config();
  char buf[128];
  const sleepsched::Cause cause = g_sleep.cause();
  snprintf(buf, sizeof(buf),
           "{\"on\":%s,\"at\":\"%02u:%02u\",\"wake\":\"%02u:%02u\",\"nap\":%u,\"asleep\":%s,\"cause\":\"%s\",\"clock\":%s}",
           c.enabled ? "true" : "false", c.sleepMin / 60u, c.sleepMin % 60u, c.wakeMin / 60u, c.wakeMin % 60u,
           (unsigned)c.napMin, asleep() ? "true" : "false",
           cause == sleepsched::Cause::Manual ? "manual" : cause == sleepsched::Cause::Schedule ? "schedule" : "none",
           g_sleepMod != sleepsched::kTimeUnknown ? "true" : "false");
  return String(buf);
}

String ctlStateJson() {
  String s = "{";
  s += "\"fw\":\"" FIRMWARE_VERSION "\",";
  s += "\"name\":" + jsonStr(g_devName) + ",\"host\":\"" + String(g_devHost) + ".local\",";
  s += "\"heap\":" + String(ESP.getFreeHeap()) + ",\"minHeap\":" + String(ESP.getMinFreeHeap()) + ",";
  s += "\"cloud\":" + cloudStatusJson() + ",";
  s += "\"ip\":\"" + webIP() + "\",";
  s += "\"rssi\":" + String((WiFi.status() == WL_CONNECTED) ? (int)WiFi.RSSI() : 0) + ",";
  s += "\"ap\":" + String(webIsAP() ? "true" : "false") + ",";
  s += "\"expr\":" + String(g_expr) + ",";
  s += "\"mode\":\"" + String(g_mode == MODE_INFO ? "info" : g_mode == MODE_FERRO ? "party" : "face") + "\",";
  s += "\"info\":\"" + String(g_infoView == VIEW_WEATHER ? "weather" : "time") + "\",";
  s += "\"partyUs\":" + String(ferro.cnt ? ferro.sumUs / ferro.cnt : 0) + ",";
  s += "\"partyMaxUs\":" + String(ferro.maxUs) + ",";
  s += "\"partyPat\":\"" + String(ferro.patternName()) + "\",";
  s += "\"partyStack\":\"" + String(ferroStackNames[ferro.stack]) + "\",";
  s += "\"tents\":" + String(ferro.liveTents()) + ",";
  s += "\"partySet\":\"" + String(ferroSetNames[g_ferroSet]) + "\",";
  s += "\"nextVisitS\":" + String(g_ferroSet == FERRO_SET_AUTO && g_mode == MODE_FACE ? (long)(g_nextVisitAt - millis()) / 1000 : -1) + ",";
  s += "\"visitLeftS\":" + String(g_mode == MODE_FERRO && g_visitEndAt ? (long)(g_visitEndAt - millis()) / 1000 : -1) + ",";
  s += "\"panel\":" + String(g_panelContrast) + ",";
  s += "\"sleep\":" + sleepJson() + ",";
  ferro.sumUs = ferro.cnt = ferro.maxUs = 0;
  s += "\"snow\":" + String(g_snow ? "true" : "false") + ",";
  s += "\"contrast\":" + String(g_contrast) + ",";
  s += "\"leds\":\"" + String(leds.modeName()) + "\",";
  s += "\"ledBri\":" + String(leds.getBrightness()) + ",";
  s += "\"ledPat\":\"" + String(leds.patternName()) + "\",";
  s += "\"ledPal\":\"" + String(leds.paletteName()) + "\",";
  s += "\"ledPatSel\":\"" + String(leds.patternSel()) + "\",";
  s += "\"ledPalSel\":\"" + String(leds.paletteSel()) + "\",";
  s += "\"ledSpeed\":" + String(leds.getSpeed()) + ",";
  s += "\"ledPace\":\"" + String(leds.paceName()) + "\",";
  for (int i = 0; i < 3; i++) s += "\"ledC" + String(i + 1) + "\":\"" + leds.colorHex(i) + "\",";
  s += "\"motion\":\"" + String(motion.levelName()) + "\",";
  s += "\"move\":\"" + String(motion.moveName()) + "\",";
  s += "\"angle\":" + String(motion.angle(), 1) + ",";
  s += "\"faceUs\":" + String(g_faceUs) + ",";
  s += "\"inkUs\":" + String(ink.lastUs) + ",\"inkMaxUs\":" + String(ink.maxUs) + ",";
  {
    Weather w = weatherNow();
    s += "\"wx\":{\"ok\":" + String(w.valid ? "true" : "false") + ",\"t\":" + String(w.tempF) + ",\"cond\":\"" + String(w.condition) +
         "\",\"code\":" + String(w.code) + ",\"sky\":" + String(inkSky(w)) + ",\"hi\":" + String(w.hi) + ",\"lo\":" + String(w.lo) +
         ",\"pop\":" + String(w.pop) + ",\"wind\":" + String(w.windMph) + ",\"day\":" + String(w.isDay ? "true" : "false") + "},";
  }
  ink.maxUs = 0;
  s += "\"sendUs\":" + String(g_sendUs) + ",";
  s += "\"frameMs\":" + String(g_frameMs);
  s += "}";
  return s;
}

// ============================================================================
// Overlays drawn directly on the buffer (unscaled, screen-space)
// ============================================================================
void drawSayText() {
  u8g2.setFont(u8g2_font_6x12_tf);
  int16_t w = u8g2.getStrWidth(g_sayText);
  int16_t boxW = min((int16_t)(w + 8), (int16_t)OLED_WIDTH);
  int16_t x = (OLED_WIDTH - boxW) / 2;
  int16_t y = OLED_HEIGHT - 14;
  u8g2.setDrawColor(1); u8g2.drawRBox(x, y, boxW, 14, 2);   // filled bubble
  u8g2.setDrawColor(0); u8g2.drawStr(x + 4, y + 11, g_sayText); // knockout text
  u8g2.setDrawColor(1);
}

// ============================================================================
// Boot — the Ink Time intro (ink_time.h), timed to the real WiFi join
// ============================================================================
// The white opens like an iris, a drip splashes into the drop, a compass spike
// sweeps while tryStation() waits (bootFrame), then the result plays (address
// or hotspot name) and the drop swells into the face, whose eyes open.
unsigned long g_bootAt = 0;            // when the intro started

void bootFrame(unsigned long) {
  static unsigned long last = 0;
  if (millis() - last < FRAME_INTERVAL_MS) return;
  last = millis();
  if (!g_bootAt) g_bootAt = millis();
  ink.introJoin(u8g2.getBufferPtr(), millis() - g_bootAt, webSsid().c_str());
  leds.update();
  u8g2.sendBuffer();
}

void ctlIntroFrame(long t, long k, bool ap) {
  if (t >= 0) ink.introJoin(u8g2.getBufferPtr(), t, webSsid().c_str());
  else if (k >= 0) ink.introResult(u8g2.getBufferPtr(), k, !ap, ap ? "vizMini-8DCC" : "10.0.0.227",
                                   ap ? "192.168.4.1" : (String(g_devHost) + ".local").c_str());
}

// After webBegin(): finish the opening if the join beat it, then the result
// and the swell. The web server already answers between frames.
void bootOutro(bool online, const String &where) {
  while (!g_bootAt || millis() - g_bootAt < InkTime::INTRO_JOIN_AT) { bootFrame(0); delay(5); }
  String small = online ? String(g_devHost) + ".local" : String("192.168.4.1");
  unsigned long rs = millis();
  for (;;) {
    unsigned long now = millis();
    if (!ink.introResult(u8g2.getBufferPtr(), now - rs, online, where.c_str(), small.c_str())) break;
    leds.update();
    u8g2.sendBuffer();
    webLoop();
    while (millis() - now < FRAME_INTERVAL_MS) delay(2);
  }
  u8g2.sendBuffer();                   // all black: the face opens its eyes on it
}

// ============================================================================
// Setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  cloudBootCheck();                  // keep (or wait to keep) the running firmware image
  deviceNameLoad();                  // before the WiFi: hotspot and mDNS use it

  gfxDev.begin();
  gfxDev.setContrast((uint8_t)constrain(DEFAULT_CONTRAST * FERRO_CONTRAST_K, (float)FERRO_CONTRAST_MIN, (float)FERRO_CONTRAST_MAX));   // the intro is a white lightbox
  g_panelContrast = (uint8_t)constrain(DEFAULT_CONTRAST * FERRO_CONTRAST_K, (float)FERRO_CONTRAST_MIN, (float)FERRO_CONTRAST_MAX);

  touch.begin();
  face.init();
  botEyes.init(millis());
  botEyes.sleeping = true;           // asleep until the WiFi joins
  motion.begin();                    // level from NVS (rare by default)
  motion.onDizzy = dizzyAfterSpin;
  ferro.begin();
  {
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    g_ferroSet = p.getUChar("ferro", FERRO_SET_AUTO);
    ferro.stack = min((int)p.getUChar("pStack", 1), 2);
    p.end();
    if (g_ferroSet > FERRO_SET_AUTO) g_ferroSet = FERRO_SET_AUTO;
  }
  snowInit();

  leds.begin();                      // strip + onboard pixel, mode/brightness from NVS
  sleepLoad();                       // schedule from NVS; no clock yet, so awake

  String where;
#if WIFI_ENABLED
  where = webBegin(bootFrame);       // join wifi (animating) or start AP + captive portal
  if (!webIsAP()) {                  // start NTP clock sync once on a network
    configTzTime(TZ_POSIX, NTP_SERVER_1, NTP_SERVER_2);
  }
  weatherBegin();                    // background fetches (waits for WiFi itself)
  cloudBegin();                      // VizCloud sync task (waits for a network itself)
#else
  WiFi.mode(WIFI_OFF);               // TEMP: radio fully off for the noise test
  where = "wifi-off-test";
#endif
  Serial.printf("[vizMini] %s  ->  %s\n", webIsAP() ? "AP" : "STA", where.c_str());
#if WIFI_ENABLED
  bootOutro(!webIsAP(), where);
#endif
  // Wake up on the black the drop left; on the hotspot keep its name in a bubble
  botEyes.sleeping = false;
  if (webIsAP()) {
    snprintf(g_sayText, sizeof(g_sayText), "wifi: %s", where.c_str());
    g_sayUntil = millis() + BOOT_AP_NOTE_MS;
  }

  g_lastInputMs = millis();
  if (g_ferroSet == FERRO_SET_ON) enterFerro();
  else if (g_ferroSet == FERRO_SET_AUTO) scheduleVisit();
}

void loop() {
  if (WIFI_ENABLED) webLoop();        // keep web control responsive every pass
  leds.update();                      // inside lights, 50 Hz
  cloudLoop();                        // run what VizCloud asked for (cheap when idle)
  sleepTick();                        // after web + cloud, so their commands land this pass

  // ---- WiFi signal readout (gated; rssi also lives in /state) ----
#if WIFI_RSSI_LOG
  {
    static unsigned long lastRssi = 0;
    if (!webIsAP() && WiFi.status() == WL_CONNECTED && millis() - lastRssi > 3000) {
      lastRssi = millis();
      Serial.printf("[wifi] RSSI %d dBm\n", (int)WiFi.RSSI());
    }
  }
#endif

  // ---- touch debug: log raw pin transitions + a heartbeat ----
#if defined(TOUCH_DEBUG) && TOUCH_DEBUG
  {
    static int lastRaw = -1; static unsigned long lastBeat = 0;
    int raw = digitalRead(TOUCH_PIN);
    if (raw != lastRaw) { lastRaw = raw; Serial.printf("[touch] GPIO%d raw=%d\n", TOUCH_PIN, raw); }
    if (millis() - lastBeat > 3000) { lastBeat = millis(); Serial.printf("[touch] heartbeat raw=%d\n", raw); }
  }
#endif

  // ---- touch gestures ----
  TouchEvent ev = touch.update();
  if (ev != TOUCH_NONE) {
#if defined(TOUCH_DEBUG) && TOUCH_DEBUG
    Serial.printf("[touch] event=%d\n", (int)ev);
#endif
    unsigned long now = millis();
    bool isDouble = (ev == TOUCH_PRESS) && (now - g_lastPressMs < DOUBLE_TAP_MS);
    if (ev == TOUCH_PRESS) g_lastPressMs = now;

    if (asleep()) {
      if (ev == TOUCH_LONG_PRESS) { g_sleep.wakeNow(); sleepTick(); }   // hold to wake
      else if (ev == TOUCH_PRESS) g_peekUntil = now + SLEEP_PEEK_MS;     // a press is a peek
    } else {
      g_lastInputMs = now;
      g_lastHumanMs = now;
      if (g_mode == MODE_INFO) {
        if (ev == TOUCH_PRESS) toggleInfoView();        // single touch flips view
        else if (ev == TOUCH_LONG_PRESS) exitInfo();    // hold to leave
      } else if (ev == TOUCH_LONG_PRESS) {
        enterInfo();                                    // hold -> time/weather
      } else if (ev == TOUCH_PRESS) {
        if (isDouble) enterInfo();                      // double-tap -> time/weather
        else {                                          // single -> cycle mood, new light show
          applyExpression((g_expr + 1) % BOT_NUM_EXPRESSIONS);
          leds.shuffle();
        }
      }
    }
  }

  // ---- info (time + weather) mode: render and skip the face ----
  if (g_mode == MODE_INFO) {
    if (millis() - g_lastFrame < FRAME_INTERVAL_MS) return;   // full rate: the ink moves
    g_lastFrame = millis();
    if (INFO_AUTO_SWITCH_MS && millis() - g_viewSwitchMs >= INFO_AUTO_SWITCH_MS)
      toggleInfoView();                                       // auto-flip every 30s
    stepPanelContrast();
    renderInfo();
    return;
  }

  // ---- ferro mode: the drop gets the whole screen, the face sleeps ----
  if (g_mode == MODE_FERRO) {
    if (millis() - g_lastFrame < FRAME_INTERVAL_MS) return;
    g_lastFrame = millis();
    unsigned long fnow = millis();
    if (g_visitEndAt && (long)(fnow - g_visitEndAt) >= 0 && !ferro.morphing()) { g_visitEndAt = 0; exitFerro(); }
    ferro.step(fnow);
    if (ferro.eyeDraw) {                       // the eyes ride inside the drop during a morph
      tweenManager.update();
      face.update();
      botEyes.sleeping = ferro.eyesShut;
      botEyes.step(fnow);
    }
    if (ferro.done()) {                        // swelled back into the face: hand over
      ferro.leave();
      botEyes.sleeping = false;
      g_mode = MODE_FACE;
      g_visitEndAt = 0;
      applyExpression(g_expr);
      if (g_ferroSet == FERRO_SET_AUTO) scheduleVisit();
      return;
    }
    ferro.render(fnow);
    if (ferro.eyeDraw) renderFaceAt((int16_t)lroundf(ferro.eyeCX), (int16_t)lroundf(ferro.eyeCY), ferro.eyeK);
    stepPanelContrast();
    uint32_t t1 = micros();
    u8g2.sendBuffer();
    g_sendUs = micros() - t1;
    g_frameMs = fnow - g_prevFrameAt;
    g_prevFrameAt = fnow;
    return;
  }

  // ---- auto ferro visit (waits out info mode and any rotation move) ----
  if (g_ferroSet == FERRO_SET_AUTO && g_nextVisitAt && (long)(millis() - g_nextVisitAt) >= 0 && !motion.turned() && !asleep()) {
    g_nextVisitAt = 0;
    enterFerro(FERRO_VISIT_MS);
    return;
  }

  // ---- end a startle reaction ----
  if (g_startleUntil && millis() > g_startleUntil) {
    g_startleUntil = 0;
    applyExpression(g_prevExpr);
  }

  // ---- frame-rate-capped render ----
  if (millis() - g_lastFrame < FRAME_INTERVAL_MS) return;
  g_lastFrame = millis();

  tweenManager.update();
  face.update();
  botEyes.lastInteractMs = g_lastInputMs;   // the yawn waits for 5 quiet minutes
  botEyes.sleeping = asleep() && (long)(millis() - g_peekUntil) >= 0;   // shut; a press peeks
  botEyes.step(millis());                   // blinks, gaze, fidgets, face springs
  leds.setBlink(botEyes.b);
  motion.step(millis());
  stepPanelContrast();                      // eases back up after ferro mode

  u8g2.clearBuffer();
  // Snow stays upright, so it sits out the (rare, short) moves
  if (g_snow && !motion.turned() && !asleep()) snowDrawAndStep();
  uint32_t t0 = micros();
  renderMiniFace();
  motion.apply();                           // turn + fit the frame (no-op when upright)
  g_faceUs = micros() - t0;
  if (g_sayText[0] && millis() < g_sayUntil) drawSayText();
  else g_sayText[0] = 0;
  uint32_t t1 = micros();
  u8g2.sendBuffer();
  g_sendUs = micros() - t1;
  unsigned long now = millis();
  g_frameMs = now - g_prevFrameAt;
  g_prevFrameAt = now;
}
