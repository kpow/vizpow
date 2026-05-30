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
//   - Tap FRONT → nod   (yes)
//   - Tap BACK  → shake (no)   [middle pad ignored — every tap is front or back]
//   - Hold 2s   → chill mode (10 min)
//
// The tapped zone is recovered by tracking each pad's peak intensity during the
// press and reading it on the release edge.

#include <M5Unified.h>  // m5::Button_Class

// Forward declarations
struct BotModeState;
extern BotModeState botMode;

enum ScTapZone : uint8_t { SC_ZONE_FRONT = 0, SC_ZONE_MIDDLE = 1, SC_ZONE_BACK = 2 };

struct ScTouchState {
  m5::Button_Class headBtn;

  // Peak intensity per normalized zone during the current/last contact.
  uint8_t peak[3] = { 0, 0, 0 };
  bool    rawPressed = false;       // previous-frame raw pressed state (for peak reset)
  uint8_t lastTapZone = SC_ZONE_MIDDLE;

  // 2s hold → chill. Detected via pressedFor() with a one-shot latch so the
  // Button_Class hold threshold can stay short (snappy click resolution).
  bool holdFired = false;
  static constexpr uint16_t SC_CHILL_HOLD_MS = 2000;

  // Chill mode (10 min quiet) — owned here; read by the main loop's idle gate.
  bool chillMode = false;
  unsigned long chillEndMs = 0;
  static constexpr uint32_t CHILL_DURATION_MS = 600000;  // 10 minutes

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
  }

  // Returns: 0=nothing, 1=tap (see lastTapZone), 3=hold → chill
  uint8_t update() {
    if (!sysStatus.scHeadTouchReady) return 0;

    unsigned long now = millis();

    if (chillMode && now >= chillEndMs) chillMode = false;

    // Read fresh touch data, normalize Front→Middle→Back (BSP reverses raw index).
    scReadTouch();
    uint8_t inten[3] = {
      scGetTouchZone(2),  // FRONT
      scGetTouchZone(1),  // MIDDLE
      scGetTouchZone(0),  // BACK
    };
    bool pressed = (inten[0] || inten[1] || inten[2]);

    // Track per-zone peak intensity; reset on a fresh contact.
    if (pressed && !rawPressed) {
      peak[0] = peak[1] = peak[2] = 0;
    }
    if (pressed) {
      for (int z = 0; z < 3; z++) if (inten[z] > peak[z]) peak[z] = inten[z];
    }
    rawPressed = pressed;

    headBtn.setRawState(now, pressed);

    // 2s hold → chill (one-shot latch).
    if (headBtn.isPressed() && !holdFired && headBtn.pressedFor(SC_CHILL_HOLD_MS)) {
      holdFired = true;
      return 3;
    }

    // Immediate tap on release. A release that merely ends a chill-hold is
    // consumed here so it doesn't also fire a tap reaction.
    if (headBtn.wasClicked()) {
      if (holdFired) { holdFired = false; return 0; }
      // Require a FULL-strength hit (level 3) on the front or back pad. A real
      // finger easily reaches it; stray capacitive noise (e.g. a ceiling fan)
      // only nudges a pad to level 1, so it's rejected. Middle pad is ignored.
      bool frontHigh = peak[SC_ZONE_FRONT] >= OUTPUT_HIGH;
      bool backHigh  = peak[SC_ZONE_BACK]  >= OUTPUT_HIGH;
      if (!frontHigh && !backHigh) return 0;
      // Back wins ties; otherwise whichever end reached full strength.
      lastTapZone = (backHigh && peak[SC_ZONE_BACK] >= peak[SC_ZONE_FRONT])
                      ? SC_ZONE_BACK : SC_ZONE_FRONT;
      return 1;
    }

    if (headBtn.isReleased()) holdFired = false;

    return 0;
  }
};

static ScTouchState scTouch_state;

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
// Uses the proven blocking nod/shake gestures from stackchan_base.h (the same
// ones the web UI head presets call), paired with a face change, a spoken
// phrase, and a synth sound. The gesture pumps botSounds.update() while it runs.

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

// Pump fed to the blocking nod/shake gestures so the synth keeps ticking while
// the servos move (the gesture briefly blocks the render loop).
inline void scGesturePumpSound() {
#ifdef TARGET_CORES3
  botSounds.update();
#endif
}

inline void scFireNod() {
  botMode.face.transitionTo(EXPR_HAPPY, 150);
  botMode.speechBubble.show(SC_NOD_PHRASES[random(0, 5)], 2500);
  scLeds.flash(80, 255, 120, 300);
  botMode.registerInteraction();
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 2500;  // cover the blocking gesture + tail
#ifdef TARGET_CORES3
  scPlayRandomSound(SC_NOD_SOUNDS, sizeof(SC_NOD_SOUNDS) / sizeof(SC_NOD_SOUNDS[0]));
#endif
  scNodGesture(scGesturePumpSound);  // proven web-preset nod (blocking, pumps sound)
}

inline void scFireShake() {
  botMode.face.transitionTo(EXPR_ANNOYED, 150);
  botMode.speechBubble.show(SC_SHAKE_PHRASES[random(0, 5)], 2500);
  scLeds.flash(255, 80, 40, 300);
  botMode.registerInteraction();
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 2500;  // cover the blocking gesture + tail
#ifdef TARGET_CORES3
  scPlayRandomSound(SC_SHAKE_SOUNDS, sizeof(SC_SHAKE_SOUNDS) / sizeof(SC_SHAKE_SOUNDS[0]));
#endif
  scShakeGesture(scGesturePumpSound);  // proven web-preset shake (blocking, pumps sound)
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
// Long-Press Chill — hold head 2s → 10 min quiet mode
// ============================================================================

inline void scFireChillMode() {
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
    scTouch_state.chillEndMs = millis() + ScTouchState::CHILL_DURATION_MS;

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
