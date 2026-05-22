#ifndef STACKCHAN_TOUCH_H
#define STACKCHAN_TOUCH_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include "config.h"
#include "stackchan_base.h"
#include "stackchan_leds.h"

// ============================================================================
// Head Touch State Machine
// ============================================================================
// Polls Si12T each frame. Detects:
//   - Single tap: any zone press→release triggers pet reaction
//   - Double-tap center: two center taps within 400ms → recenter head
//   - Long press: hold any zone 2+ seconds → chill mode (10 min)
//
// Touch zones: 0=left, 1=center, 2=right

// Forward declarations
struct BotModeState;
extern BotModeState botMode;

struct ScTouchState {
  // Previous frame state per zone
  uint8_t prevZone[3] = { OUTPUT_NONE, OUTPUT_NONE, OUTPUT_NONE };

  // Double-tap tracking (center zone only)
  unsigned long lastCenterTapMs = 0;
  bool waitingForDoubleTap = false;

  // Debounce
  unsigned long lastReactionMs = 0;
  static constexpr uint16_t DEBOUNCE_MS = 300;
  static constexpr uint16_t DOUBLE_TAP_WINDOW_MS = 400;

  // Single tap held in suspense during double-tap window
  bool pendingSingleTap = false;
  uint8_t pendingTapZone = 0;
  unsigned long pendingTapAt = 0;

  // Long press tracking
  bool anyPressed = false;
  unsigned long pressStartMs = 0;
  bool longPressFired = false;
  static constexpr uint16_t SC_LONG_PRESS_MS = 2000;

  // Chill mode
  bool chillMode = false;
  unsigned long chillEndMs = 0;
  static constexpr uint32_t CHILL_DURATION_MS = 600000;  // 10 minutes

  void init() {
    memset(prevZone, OUTPUT_NONE, sizeof(prevZone));
    lastCenterTapMs = 0;
    waitingForDoubleTap = false;
    lastReactionMs = 0;
    pendingSingleTap = false;
    anyPressed = false;
    longPressFired = false;
    chillMode = false;
    chillEndMs = 0;
  }

  // Returns: 0=nothing, 1=single tap, 2=double-tap recenter, 3=long-press chill
  uint8_t update() {
    if (!sysStatus.scHeadTouchReady) return 0;

    unsigned long now = millis();

    // Check chill mode expiry
    if (chillMode && now >= chillEndMs) {
      chillMode = false;
    }

    // Read fresh touch data
    scReadTouch();

    uint8_t curZone[3];
    for (int i = 0; i < 3; i++) {
      curZone[i] = scGetTouchZone(i);
    }

    // Check if anything is currently pressed
    bool nowPressed = (curZone[0] != OUTPUT_NONE ||
                       curZone[1] != OUTPUT_NONE ||
                       curZone[2] != OUTPUT_NONE);

    uint8_t result = 0;

    // Long press detection: any zone held for 2+ seconds
    if (nowPressed && !anyPressed) {
      // Just started pressing
      pressStartMs = now;
      longPressFired = false;
    }
    if (nowPressed && !longPressFired && (now - pressStartMs >= SC_LONG_PRESS_MS)) {
      // Long press triggered!
      longPressFired = true;
      pendingSingleTap = false;
      waitingForDoubleTap = false;
      lastReactionMs = now;
      result = 3;
    }
    anyPressed = nowPressed;

    // Skip tap detection if long press just fired
    if (longPressFired) {
      memcpy(prevZone, curZone, sizeof(prevZone));
      return result;
    }

    // Detect release events (was pressed, now not)
    for (int z = 0; z < 3; z++) {
      bool wasPressed = (prevZone[z] != OUTPUT_NONE);
      bool isPressed  = (curZone[z] != OUTPUT_NONE);

      if (wasPressed && !isPressed && (now - lastReactionMs > DEBOUNCE_MS)) {
        if (z == 1) {
          // Center zone — check for double-tap
          if (waitingForDoubleTap && (now - lastCenterTapMs < DOUBLE_TAP_WINDOW_MS)) {
            pendingSingleTap = false;
            waitingForDoubleTap = false;
            lastReactionMs = now;
            result = 2;
          } else {
            lastCenterTapMs = now;
            waitingForDoubleTap = true;
            pendingSingleTap = true;
            pendingTapZone = z;
            pendingTapAt = now;
          }
        } else {
          // Left or right — immediate single tap
          lastReactionMs = now;
          pendingSingleTap = false;
          waitingForDoubleTap = false;
          result = 1;
          pendingTapZone = z;
        }
      }
    }

    // Fire pending single tap if double-tap window expired
    if (pendingSingleTap && (now - pendingTapAt >= DOUBLE_TAP_WINDOW_MS)) {
      pendingSingleTap = false;
      waitingForDoubleTap = false;
      lastReactionMs = now;
      result = 1;
      pendingTapZone = 1;
    }

    memcpy(prevZone, curZone, sizeof(prevZone));
    return result;
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
