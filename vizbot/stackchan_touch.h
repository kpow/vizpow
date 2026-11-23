#ifndef STACKCHAN_TOUCH_H
#define STACKCHAN_TOUCH_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include "config.h"
#include "stackchan_base.h"
#include "stackchan_leds.h"

// ============================================================================
// Head Touch State Machine (M5Stack StackChan-BSP model)
// ============================================================================
// The Si12T exposes THREE pads in a line along the TOP of the head, ordered
// Front → Middle → Back (NOT left/center/right — confirmed against the official
// m5stack/StackChan-BSP touch_sensor driver). We normalize raw point_type so:
//   zone 0 = FRONT, zone 1 = MIDDLE, zone 2 = BACK
//
// Debounce uses M5Unified's proven m5::Button_Class (the same class the BSP builds
// its touch gestures on). We fire on the IMMEDIATE click (release edge) so a tap
// responds the instant you lift — capacitive pads are often held >0.5s, which the
// multi-click/hold machinery would otherwise misread as a "hold" and drop. Events:
//   - Tap FRONT → nod   (yes) — or, on VOICE_ENABLED builds, talk to vizbrain
//   - Tap BACK  → shake (no)   [middle pad ignored — a finger there also maxes a
//     neighbour pad, so it can't be its own gesture]
//   - Hold 2s   → chill mode (10/30/60/120 min, see scChillMinutes)
//
// The tapped zone is recovered by tracking each pad's peak intensity during the
// press and reading it on the release edge.

#include <M5Unified.h>  // m5::Button_Class

// Forward declarations
struct BotModeState;
extern BotModeState botMode;

enum ScTapZone : uint8_t { SC_ZONE_FRONT = 0, SC_ZONE_MIDDLE = 1, SC_ZONE_BACK = 2 };

// ---- Pad log (GET /debug/touchraw) ------------------------------------------
// What the Si12T reported, kept so a burst of taps nobody made can be read back
// afterwards: every contact (start, length, strongest level per pad) and every
// change in the three levels. Nothing here feeds back into the gestures.
// It lives in PSRAM (mem.h): 1.7 KB of internal RAM is more than the camera's
// DMA block has to spare. Without PSRAM there is no log.
#define SC_TOUCH_LOG_CONTACTS 48
#define SC_TOUCH_LOG_TRACE    128
// What the rest of vizBot was doing, as bits, stamped on every logged contact
// and level change, so a burst of touches nobody made can be matched to the
// subsystem that was busy at the time. scTouchContext() is defined in
// vizbot.ino, after every subsystem's header. Names: SC_TOUCH_CTX_NAMES.
#define SC_TOUCH_CTX_BITS 14
static const char* const SC_TOUCH_CTX_NAMES[SC_TOUCH_CTX_BITS] = {
  "listening", "thinking", "speaking", "wakeMic", "wakeSending", "headMoving",
  "servosResting", "cloudSync", "camera", "party", "speaker", "upload", "chill", "ignored",
};
uint16_t scTouchContext();

struct ScTouchContact { uint32_t ms; uint16_t durMs; uint8_t peak[3]; uint8_t samples; uint16_t ctx; };
struct ScTouchSample  { uint32_t ms; uint8_t level[3]; uint16_t ctx; };
struct ScTouchLog {
  uint32_t reads = 0, pressedReads = 0;
  uint32_t hist[3][4] = {};                 // [zone][level] read counts
  uint32_t firedFront = 0, firedBack = 0, firedHold = 0, rejected = 0;
  uint8_t  now[3] = { 0, 0, 0 };
  ScTouchContact contacts[SC_TOUCH_LOG_CONTACTS];
  uint32_t contactCount = 0;                // total ever; ring index = count % size
  ScTouchSample trace[SC_TOUCH_LOG_TRACE];
  uint32_t traceCount = 0;
  uint32_t contactStartMs = 0;
  uint8_t  contactSamples = 0;
  uint16_t contactCtx = 0;                  // OR of the context over the contact
  uint32_t ctxReads[SC_TOUCH_CTX_BITS] = {}; // reads with each bit set: how often it is on anyway
};
static ScTouchLog* scTouchLogPtr = nullptr;   // read by the web task; made by the loop

// Loop task only (ScTouchState::update): makes the log on first use.
inline ScTouchLog* scTouchLogGet() {
  static bool tried = false;
  if (!scTouchLogPtr && !tried) {
    tried = true;
    void* mem = memAlloc(MEM_DIAG, sizeof(ScTouchLog), MEM_BIG_ONLY);
    if (mem) scTouchLogPtr = new (mem) ScTouchLog();
  }
  return scTouchLogPtr;
}

// ---- TouchAO -------------------------------------------------------------------
// The head-touch active object (ao_core.h). It owns the Si12T's logic: reads it
// every loop pass (SIG_TICK), turns contacts into taps and holds, and publishes
// them (SIG_TOUCH_TAP / SIG_TOUCH_HOLD) instead of acting on them.
//
// What it fixes (docs/stackchan-interactions.md):
//  - Disturbances. Servo moves, rail switches, LED flashes, the amp and the mic
//    codec all shift the chip's supply. Whoever causes one publishes it; a
//    contact that overlaps one (plus a settle margin) is logged but ignored.
//  - The reference. A chip start makes the chip take its idle reference. Taken
//    under load (servo homing at a cold boot, a nod after a phantom tap) it is
//    wrong, and the chip storms until the next one. So a start is only ever
//    requested here, and only run once the bot has been still for
//    SC_TOUCH_STILL_MS; until then taps are muted and the idle drift is asked
//    to hold the head (SIG_TOUCH_NEED_STILL).
enum ScRefWhy : uint8_t { SC_REF_BOOT = 1, SC_REF_RAIL, SC_REF_STORM, SC_REF_LATCH, SC_REF_ASKED };
#define SC_TOUCH_STILL_MS      2000   // no disturbance this long before a reference
#define SC_TOUCH_ASK_STILL_MS  4000   // how long each "hold the head still" asks for
#define SC_TOUCH_CTX_IGNORED   13     // contact log ctx bit: overlapped a disturbance

struct ScTouchState {
  m5::Button_Class headBtn;

  // Peak intensity per normalized zone during the current/last contact.
  uint8_t peak[3] = { 0, 0, 0 };
  bool    rawPressed = false;       // previous read's contact state
  uint8_t lastTapZone = SC_ZONE_MIDDLE;

  // 2s hold → chill. Detected via pressedFor() with a one-shot latch so the
  // Button_Class hold threshold can stay short (snappy click resolution).
  bool holdFired = false;
  static constexpr uint16_t SC_CHILL_HOLD_MS = 2000;

  // Chill mode (quiet block, length = scChillMinutes): the loop's, not TouchAO's.
  bool chillMode = false;
  unsigned long chillEndMs = 0;

  // Blips. A finger builds up over several reads and stays 300 ms or more
  // (Kevin's taps, 22 Nov: 337-911 ms, 5-13 reads). Supply noise jumps
  // straight to full on all pads for a single read (70-83 ms) and fired a
  // front tap nobody made, 1 h into an idle watch. A contact shorter than
  // this is never a tap.
  static constexpr uint16_t SC_TAP_MIN_MS = 150;
  uint32_t lastContactMs = 0;                    // length of the contact that just ended

  // Storms. Touches nobody made come on all three pads at once, in bursts or
  // as one contact that never ends. Four such undisturbed contacts inside
  // eight seconds, or one held for fifteen, mean the reference is bad: ask for
  // a new one (at most one per SC_HEAL_GAP_MS).
  static constexpr uint8_t  SC_STORM_CONTACTS  = 4;
  static constexpr uint16_t SC_STORM_WINDOW_MS = 8000;
  static constexpr uint16_t SC_LATCH_MS        = 15000;
  static constexpr uint16_t SC_HEAL_GAP_MS     = 10000;
  uint32_t contactStartMs = 0;
  bool     contactIgnored = false;               // the current contact overlapped a disturbance
  bool     latchAsked = false;
  uint32_t allPadMs[SC_STORM_CONTACTS] = {};    // when the last all-pad contacts ended (0 = none)
  uint8_t  allPadIdx = 0;
  uint32_t heals = 0;                           // storms seen (shown by /debug/touchraw)

  // Disturbances and the reference.
  uint32_t ignoreUntilMs = 0;   // contacts before this are disturbances
  bool     cameraOn = false;    // the camera holds the bus and its own supply: a window until it ends
  bool     needRef = true;      // a chip power-up reference is never trusted
  uint8_t  refWhy = SC_REF_BOOT;
  uint32_t lastRefMs = 0, askedStillMs = 0;
  uint32_t refs = 0, ignoredContacts = 0, lockMisses = 0;

  void init() {
    // Hold threshold parked well above any plausible tap so a lingering finger
    // still registers as a click on release (Button_Class would otherwise convert
    // a >threshold press into a "hold" and never emit a click). The 2s chill hold
    // is detected independently via pressedFor().
    headBtn.setHoldThresh(8000);
    headBtn.setDebounceThresh(35);  // a touch firmer than the 10ms default → quieter
    peak[0] = peak[1] = peak[2] = 0;
    rawPressed = false;
    lastTapZone = SC_ZONE_MIDDLE;
    holdFired = false;
    chillMode = false;
    chillEndMs = 0;
    needRef = true;
    refWhy = SC_REF_BOOT;
    ignoreUntilMs = millis() + SC_TOUCH_STILL_MS;   // the boot itself was a disturbance
  }

  void disturb(uint32_t now, uint32_t ms) {
    const uint32_t until = now + ms;
    if ((int32_t)(until - ignoreUntilMs) > 0) ignoreUntilMs = until;
  }
  bool disturbed(uint32_t now) const { return cameraOn || (int32_t)(ignoreUntilMs - now) > 0; }
  bool still(uint32_t now) const {
    return !disturbed(now) && (int32_t)(now - ignoreUntilMs) >= (int32_t)SC_TOUCH_STILL_MS;
  }

  void askRef(uint8_t why) {
    if (!needRef) refWhy = why;
    needRef = true;
    askedStillMs = 0;   // ask for stillness on the next tick
  }

  void onEvent(const AoEvt& e) {
    const uint32_t now = millis();
    switch (e.sig) {
      case SIG_TICK:         sample(now); break;
      case SIG_MOTION_START: disturb(now, e.a); break;
      case SIG_RAIL_SWITCH:  disturb(now, e.a + 500); askRef(SC_REF_RAIL); break;
      case SIG_LED_PUNCH:    disturb(now, e.a + 100); break;
      case SIG_MIC_START:
      case SIG_MIC_STOP:     disturb(now, 500); break;   // codec reset, MICBIAS step
      case SIG_AMP_ON:
      case SIG_AMP_OFF:      disturb(now, 300); break;
      case SIG_CAMERA_START: cameraOn = true;  disturb(now, 300); break;
      case SIG_CAMERA_END:   cameraOn = false; disturb(now, 300); break;
      case SIG_TOUCH_RESTART: askRef(SC_REF_ASKED); break;
      default: break;
    }
  }

  // Take the reference now (the bot is still). Returns false if the bus was busy.
  bool takeRef(uint32_t now) {
    ScI2cLock lock(100);
    if (!lock.held) return false;
    const bool ok = scTouchChipStartLocked();   // also sets the 1.5 s quiet window
    refs++;
    lastRefMs = now;
    needRef = false;
    latchAsked = false;
    for (auto& t : allPadMs) t = 0;
    aoPublish(SIG_TOUCH_REF, SRC_TOUCH, refWhy, ok ? 1 : 0);
    return true;
  }

  void sample(uint32_t now) {
    if (!sysStatus.scHeadTouchReady) return;

    if (needRef) {
      const bool gapOk = !refs || now - lastRefMs >= SC_HEAL_GAP_MS || refWhy == SC_REF_RAIL || refWhy == SC_REF_BOOT;
      if (gapOk && still(now) && takeRef(now)) return;   // nothing to read until it has settled
      if (!askedStillMs || now - askedStillMs >= SC_TOUCH_ASK_STILL_MS - 500) {
        askedStillMs = now | 1;
        aoPublish(SIG_TOUCH_NEED_STILL, SRC_TOUCH, SC_TOUCH_ASK_STILL_MS, refWhy);
      }
    }

    // Read under the bus lock, or not at all: a read without it can return
    // another transaction's bytes, and an all-ones byte reads as all three pads.
    {
      ScI2cLock lock(20);
      if (!lock.held) { lockMisses++; return; }
      scChan.TouchSensor.update();
    }
    uint8_t inten[3] = {
      scGetTouchZone(2),  // FRONT  (BSP reverses the raw index)
      scGetTouchZone(1),  // MIDDLE
      scGetTouchZone(0),  // BACK
    };
    // Right after a chip start the pads are taking their reference: the log
    // still shows what they report, nothing else sees a touch.
    const bool quiet = (int32_t)(scTouchQuietUntilMs - now) > 0;
    const bool pressed = !quiet && (inten[0] || inten[1] || inten[2]);
    const bool ignoreNow = disturbed(now) || needRef;

    // Pad log: counts, and a trace entry whenever any level changes.
    ScTouchLog* const tl = scTouchLogGet();
    const uint16_t ctx = tl ? (scTouchContext() | (ignoreNow ? 1u << SC_TOUCH_CTX_IGNORED : 0)) : 0;
    if (tl) {
      tl->reads++;
      for (int b = 0; b < SC_TOUCH_CTX_BITS; b++) if (ctx & (1u << b)) tl->ctxReads[b]++;
      if (pressed) tl->pressedReads++;
      for (int z = 0; z < 3; z++) tl->hist[z][inten[z] & 3]++;
      if (inten[0] != tl->now[0] || inten[1] != tl->now[1] || inten[2] != tl->now[2]) {
        ScTouchSample& t = tl->trace[tl->traceCount % SC_TOUCH_LOG_TRACE];
        t.ms = now;
        t.ctx = ctx;
        for (int z = 0; z < 3; z++) t.level[z] = tl->now[z] = inten[z];
        tl->traceCount++;
      }
    }

    // A new contact: reset its peaks; it is ignored if any of it overlaps a disturbance.
    if (pressed && !rawPressed) {
      peak[0] = peak[1] = peak[2] = 0;
      contactStartMs = now;
      contactIgnored = false;
      if (tl) { tl->contactStartMs = now; tl->contactSamples = 0; tl->contactCtx = 0; }
    }
    if (pressed) {
      if (ignoreNow) contactIgnored = true;
      for (int z = 0; z < 3; z++) if (inten[z] > peak[z]) peak[z] = inten[z];
      if (tl && tl->contactSamples < 255) tl->contactSamples++;
      if (tl) tl->contactCtx |= ctx;
    }
    const bool allPads = peak[0] >= OUTPUT_MID && peak[1] >= OUTPUT_MID && peak[2] >= OUTPUT_MID;
    if (!pressed && rawPressed) {   // contact over
      const uint32_t dur = now - contactStartMs;
      lastContactMs = dur;
      if (tl) {
        ScTouchContact& c = tl->contacts[tl->contactCount % SC_TOUCH_LOG_CONTACTS];
        c.ms = tl->contactStartMs;
        c.durMs = dur > 65535 ? 65535 : (uint16_t)dur;
        for (int z = 0; z < 3; z++) c.peak[z] = peak[z];
        c.samples = tl->contactSamples;
        c.ctx = tl->contactCtx;
        tl->contactCount++;
      }
      if (contactIgnored) ignoredContacts++;
      aoPublish(SIG_TOUCH_CONTACT, SRC_TOUCH,
                peak[0] | (peak[1] << 8) | (peak[2] << 16) | ((contactIgnored ? 1 : 0) << 24), (int32_t)dur);
      // Storm: the fourth undisturbed all-pad contact inside eight seconds.
      if (allPads && !contactIgnored) {
        allPadMs[allPadIdx] = now | 1;
        allPadIdx = (allPadIdx + 1) % SC_STORM_CONTACTS;
        const uint32_t oldest = allPadMs[allPadIdx];
        if (oldest && now - oldest < SC_STORM_WINDOW_MS) {
          heals++;
          for (auto& t : allPadMs) t = 0;
          aoPublish(SIG_TOUCH_STORM, SRC_TOUCH, 1);
          askRef(SC_REF_STORM);
        }
      }
    }
    // Latch: an all-pad contact that will not end, disturbed or not.
    if (pressed && allPads && !latchAsked && now - contactStartMs > SC_LATCH_MS) {
      latchAsked = true;
      heals++;
      aoPublish(SIG_TOUCH_STORM, SRC_TOUCH, 2);
      askRef(SC_REF_LATCH);
    }
    rawPressed = pressed;

    // The button sees only clean contacts: one that meets a disturbance is let go.
    const bool clean = pressed && !contactIgnored;
    headBtn.setRawState(now, clean);

    // 2s hold → chill (one-shot latch).
    if (headBtn.isPressed() && !holdFired && headBtn.pressedFor(SC_CHILL_HOLD_MS)) {
      holdFired = true;
      if (tl) tl->firedHold++;
      aoPublish(SIG_TOUCH_HOLD, SRC_TOUCH);
      return;
    }

    // Immediate tap on release. A release that merely ends a chill-hold is
    // consumed here so it doesn't also fire a tap reaction.
    if (headBtn.wasClicked()) {
      if (holdFired) { holdFired = false; return; }
      if (contactIgnored || quiet || needRef) { if (tl) tl->rejected++; return; }
      if (lastContactMs < SC_TAP_MIN_MS) { if (tl) tl->rejected++; return; }   // a blip, not a finger
      // Require a FULL-strength hit (level 3). A real finger easily reaches it;
      // stray capacitive noise (e.g. a ceiling fan) only nudges a pad to level
      // 1, so it's rejected. Middle pad is ignored.
      const bool frontHigh = peak[SC_ZONE_FRONT] >= OUTPUT_HIGH;
      const bool backHigh  = peak[SC_ZONE_BACK]  >= OUTPUT_HIGH;
      if (!frontHigh && !backHigh) { if (tl) tl->rejected++; return; }
      // Back wins ties; otherwise whichever end reached full strength.
      lastTapZone = (backHigh && peak[SC_ZONE_BACK] >= peak[SC_ZONE_FRONT]) ? SC_ZONE_BACK : SC_ZONE_FRONT;
      if (tl) { if (lastTapZone == SC_ZONE_BACK) tl->firedBack++; else tl->firedFront++; }
      aoPublish(SIG_TOUCH_TAP, SRC_TOUCH, lastTapZone);
      return;
    }

    if (headBtn.isReleased()) holdFired = false;
  }
};

static ScTouchState scTouch_state;
static AoActive scTouchAO;
inline void scTouchOnEvent(const AoEvt& e) { scTouch_state.onEvent(e); }

// Setup: registers TouchAO and its subscriptions. Call before the app AO so
// touch is dispatched first in each loop pass.
inline void scTouchAOStart() {
  scTouch_state.init();
  scTouchAO.name = "touch";
  scTouchAO.onEvent = scTouchOnEvent;
  scTouchAO.tickMs = 0;   // every loop pass, as the old per-frame read
  aoStart(scTouchAO);
  static const AoSig subs[] = { SIG_MOTION_START, SIG_RAIL_SWITCH, SIG_LED_PUNCH, SIG_MIC_START, SIG_MIC_STOP,
                                SIG_AMP_ON, SIG_AMP_OFF, SIG_CAMERA_START, SIG_CAMERA_END, SIG_TOUCH_RESTART };
  for (AoSig s : subs) aoSubscribe(scTouchAO, s);
}

// Chill length picked in the touch UI / web panel; persisted in NVS ("chillMin").
static const uint8_t SC_CHILL_OPTIONS[] = { 10, 30, 60, 120 };
#define SC_NUM_CHILL_OPTIONS 4
uint8_t scChillMinutes = 10;

// Snap any value to the nearest allowed option (guards stale/garbage NVS).
inline uint8_t scSnapChillMinutes(int m) {
  uint8_t best = SC_CHILL_OPTIONS[0];
  for (uint8_t i = 0; i < SC_NUM_CHILL_OPTIONS; i++)
    if (abs(m - SC_CHILL_OPTIONS[i]) < abs(m - best)) best = SC_CHILL_OPTIONS[i];
  return best;
}

inline int8_t scChillOptionIndex() {
  for (uint8_t i = 0; i < SC_NUM_CHILL_OPTIONS; i++)
    if (SC_CHILL_OPTIONS[i] == scChillMinutes) return i;
  return 0;
}

// Whole minutes left in the current chill, rounded up (0 when not chilling).
inline uint32_t scChillMinutesLeft() {
  if (!scTouch_state.chillMode) return 0;
  long ms = (long)(scTouch_state.chillEndMs - millis());
  return ms > 0 ? (ms + 59999UL) / 60000UL : 0;
}

// Change the chill length. If already chilling, restart the timer at the new
// length so the change takes effect immediately.
inline void scSetChillMinutes(int m) {
  scChillMinutes = scSnapChillMinutes(m);
  if (scTouch_state.chillMode)
    scTouch_state.chillEndMs = millis() + scChillMinutes * 60000UL;
}

// ============================================================================
// Pet Reaction — personality-specific response to head touch
// ============================================================================

static const uint8_t SC_TOUCH_EXPR_CHILL[]  = { EXPR_HAPPY, EXPR_SHY, EXPR_LOVE, EXPR_CHILL, EXPR_WINKING };
static const uint8_t SC_TOUCH_EXPR_HYPER[]  = { EXPR_EXCITED, EXPR_LOVE, EXPR_SURPRISED, EXPR_KISSING, EXPR_SASSY };
static const uint8_t SC_TOUCH_EXPR_GRUMPY[] = { EXPR_ANNOYED, EXPR_SKEPTICAL, EXPR_ANGRY, EXPR_MISCHIEF, EXPR_DEVIOUS };

static const uint8_t SC_TOUCH_FLASH_CHILL[]  = { 100, 200, 255 };
static const uint8_t SC_TOUCH_FLASH_HYPER[]  = { 255, 100, 200 };
static const uint8_t SC_TOUCH_FLASH_GRUMPY[] = { 255,  60,  20 };

static const int SC_TOUCH_WIGGLE_CHILL  =  80;
static const int SC_TOUCH_WIGGLE_HYPER  = 200;
static const int SC_TOUCH_WIGGLE_GRUMPY =  40;

inline void scFirePetReaction(uint8_t zone, uint8_t personalityIndex) {
  const uint8_t* pool;
  uint8_t poolSize = 5;
  const uint8_t* flashColor;
  int wiggle;

  switch (personalityIndex) {
    case PERSONALITY_HYPER:
      pool = SC_TOUCH_EXPR_HYPER;
      flashColor = SC_TOUCH_FLASH_HYPER;
      wiggle = SC_TOUCH_WIGGLE_HYPER;
      break;
    case PERSONALITY_GRUMPY:
      pool = SC_TOUCH_EXPR_GRUMPY;
      flashColor = SC_TOUCH_FLASH_GRUMPY;
      wiggle = SC_TOUCH_WIGGLE_GRUMPY;
      break;
    default:
      pool = SC_TOUCH_EXPR_CHILL;
      flashColor = SC_TOUCH_FLASH_CHILL;
      wiggle = SC_TOUCH_WIGGLE_CHILL;
      break;
  }

  uint8_t expr = pool[random(0, poolSize)];
  botMode.face.transitionTo(expr, 150);

  if (random(100) < 60) {
    char buf[MAX_SAY_LEN];
    getRandomSayingText(SAY_REACT_TAP, buf, sizeof(buf));
    botMode.speechBubble.show(buf, 2500);
  }

  scLeds.flash(flashColor[0], flashColor[1], flashColor[2], 300);

  int yawDir = 0;
  if (zone == 0) yawDir = wiggle;
  else if (zone == 2) yawDir = -wiggle;
  else yawDir = (random(2) ? wiggle : -wiggle);

  scMoveYaw(yawDir, 200);

  botMode.registerInteraction();
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 1500;
}

// ============================================================================
// Nod / Shake — front tap = "yes", back tap = "no"
// ============================================================================
// The nod/shake gestures are MotionAO step sequences (stackchan_base.h, the
// same ones the web UI head presets ask for), paired with a face change, a
// spoken phrase and a synth sound. They return at once: the loop keeps running.

static const char* const SC_NOD_PHRASES[]   = { "Yep!", "Mhm", "For sure", "Totally", "Yeah!" };
static const char* const SC_SHAKE_PHRASES[] = { "Nope", "No way", "Uh-uh", "Nuh-uh", "Nah" };

// Randomized sound pools (thematic: nod=positive, shake=negative).
#ifdef TARGET_CORES3
static const MidiSequenceId SC_NOD_SOUNDS[]   = { SEQ_CONFIRM, SEQ_SUCCESS, SEQ_HAPPY_HUM, SEQ_COIN, SEQ_PING };
static const MidiSequenceId SC_SHAKE_SOUNDS[] = { SEQ_SHAKE_RATTLE, SEQ_DISMISS, SEQ_ERROR, SEQ_SAD_SIGH, SEQ_CURIOUS_BEEP };
inline void scPlayRandomSound(const MidiSequenceId* pool, uint8_t n) {
  botSounds.play(pool[random(0, n)]);
}
#endif

inline void scFireNod() {
  botMode.face.transitionTo(EXPR_HAPPY, 150);
  botMode.speechBubble.show(SC_NOD_PHRASES[random(0, 5)], 2500);
  scLeds.flash(80, 255, 120, 300);
  botMode.registerInteraction();
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 2500;  // cover the gesture + tail
#ifdef TARGET_CORES3
  scPlayRandomSound(SC_NOD_SOUNDS, sizeof(SC_NOD_SOUNDS) / sizeof(SC_NOD_SOUNDS[0]));
#endif
  scNodGesture();
}

inline void scFireShake() {
  botMode.face.transitionTo(EXPR_ANNOYED, 150);
  botMode.speechBubble.show(SC_SHAKE_PHRASES[random(0, 5)], 2500);
  scLeds.flash(255, 80, 40, 300);
  botMode.registerInteraction();
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 2500;  // cover the gesture + tail
#ifdef TARGET_CORES3
  scPlayRandomSound(SC_SHAKE_SOUNDS, sizeof(SC_SHAKE_SOUNDS) / sizeof(SC_SHAKE_SOUNDS[0]));
#endif
  scShakeGesture();
}

// ============================================================================
// Double-Tap Recenter
// ============================================================================

inline void scFireRecenter(uint8_t personalityIndex) {
  botMode.registerInteraction();

  switch (personalityIndex) {
    case PERSONALITY_HYPER:
      scMoveYaw(0, 150);
      scMovePitch(SC_SERVO_Y_HOME_DEG * 10, 150);
      delay(150);
      scMoveYaw(60, 100);
      delay(100);
      scMoveYaw(-40, 100);
      delay(100);
      scMoveYaw(0, 100);
      scLeds.flash(255, 255, 0, 200);
      botMode.face.transitionTo(EXPR_EXCITED, 150);
      break;

    case PERSONALITY_GRUMPY:
      botMode.face.transitionTo(EXPR_ANNOYED, 200);
      delay(400);
      scMoveYaw(0, 800);
      scMovePitch(SC_SERVO_Y_HOME_DEG * 10, 800);
      scLeds.flash(255, 80, 20, 150);
      break;

    default:
      scMoveYaw(0, 500);
      scMovePitch(SC_SERVO_Y_HOME_DEG * 10, 500);
      scLeds.flash(100, 200, 255, 250);
      botMode.face.transitionTo(EXPR_HAPPY, 300);
      break;
  }

  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 2000;
}

// ============================================================================
// Long-Press Chill — hold head 2s → quiet mode for scChillMinutes
// ============================================================================

// durationMs=0 means the chosen length (scChillMinutes). /bot/chill?minutes=N
// overrides — long chills (e.g. 480 min) double as the idle-drift-off soak
// experiment.
inline void scFireChillMode(uint32_t durationMs = 0) {
  // Toggle: if already chilling, wake up
  if (scTouch_state.chillMode) {
    scTouch_state.chillMode = false;
    botMode.face.transitionTo(EXPR_HAPPY, 300);
    botMode.speechBubble.show("I'm up!", 2000);
    scLeds.flash(255, 220, 50, 300);
    // Restore default LED mode
    scLeds.mode = SC_LED_MODE_RAINBOW;
    scLeds.speed = 128;
  } else {
    scTouch_state.chillMode = true;
    scTouch_state.chillEndMs = millis() +
        (durationMs ? durationMs : scChillMinutes * 60000UL);

    scGoHome(800);
    botMode.face.transitionTo(EXPR_CHILL, 500);
    botMode.speechBubble.show("Zzz...", 3000);

    scLeds.mode = SC_LED_MODE_BREATHING;
    scLeds.speed = 40;
    scLeds.flash(80, 120, 255, 500);
  }

  botMode.registerInteraction();
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 3000;
}

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_TOUCH_H
