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
//
// Touch zones: 0=left, 1=center, 2=right
// Pressure levels: OUTPUT_NONE=0, OUTPUT_LOW=1, OUTPUT_MID=2, OUTPUT_HIGH=3

// Forward declarations — implemented in bot_mode.h
struct BotModeState;
extern BotModeState botMode;

struct ScTouchState {
  // Previous frame state per zone
  uint8_t prevZone[3] = { OUTPUT_NONE, OUTPUT_NONE, OUTPUT_NONE };

  // Double-tap tracking (center zone only)
  unsigned long lastCenterTapMs = 0;
  bool waitingForDoubleTap = false;

  // Debounce: ignore rapid re-triggers
  unsigned long lastReactionMs = 0;
  static constexpr uint16_t DEBOUNCE_MS = 300;

  // Double-tap window
  static constexpr uint16_t DOUBLE_TAP_WINDOW_MS = 400;

  // Single tap held in suspense during double-tap window
  bool pendingSingleTap = false;
  uint8_t pendingTapZone = 0;
  unsigned long pendingTapAt = 0;

  void init() {
    memset(prevZone, OUTPUT_NONE, sizeof(prevZone));
    lastCenterTapMs = 0;
    waitingForDoubleTap = false;
    lastReactionMs = 0;
    pendingSingleTap = false;
  }

  // Call once per frame from main loop
  // Returns: 0=nothing, 1=single tap fired, 2=double-tap recenter fired
  uint8_t update() {
    if (!sysStatus.scHeadTouchReady) return 0;

    unsigned long now = millis();

    // Read fresh touch data
    scReadTouch();

    // Get current zone states
    uint8_t curZone[3];
    for (int i = 0; i < 3; i++) {
      curZone[i] = scGetTouchZone(i);
    }

    uint8_t result = 0;

    // Detect release events (was pressed, now not)
    for (int z = 0; z < 3; z++) {
      bool wasPressed = (prevZone[z] != OUTPUT_NONE);
      bool isPressed  = (curZone[z] != OUTPUT_NONE);

      if (wasPressed && !isPressed && (now - lastReactionMs > DEBOUNCE_MS)) {
        // Release detected on zone z
        if (z == 1) {
          // Center zone — check for double-tap
          if (waitingForDoubleTap && (now - lastCenterTapMs < DOUBLE_TAP_WINDOW_MS)) {
            // Double-tap! Cancel pending single tap and recenter
            pendingSingleTap = false;
            waitingForDoubleTap = false;
            lastReactionMs = now;
            result = 2;  // recenter
          } else {
            // First center tap — hold in suspense
            lastCenterTapMs = now;
            waitingForDoubleTap = true;
            pendingSingleTap = true;
            pendingTapZone = z;
            pendingTapAt = now;
          }
        } else {
          // Left or right zone — immediate single tap (no double-tap check)
          lastReactionMs = now;
          pendingSingleTap = false;
          waitingForDoubleTap = false;
          result = 1;  // single tap
          pendingTapZone = z;  // store which zone for reaction
        }
      }
    }

    // Fire pending single tap if double-tap window expired
    if (pendingSingleTap && (now - pendingTapAt >= DOUBLE_TAP_WINDOW_MS)) {
      pendingSingleTap = false;
      waitingForDoubleTap = false;
      lastReactionMs = now;
      result = 1;  // single tap (delayed center)
    }

    // Save state for next frame
    memcpy(prevZone, curZone, sizeof(prevZone));
    return result;
  }
};

static ScTouchState scTouch_state;

// ============================================================================
// Pet Reaction — personality-specific response to head touch
// ============================================================================
// Different reactions per personality × zone:
//   Chill: gentle, happy reactions
//   Hyper: excited, over-the-top reactions
//   Grumpy: annoyed, reluctant reactions

// Personality-specific expression pools for touch reactions
static const uint8_t SC_TOUCH_EXPR_CHILL[]  = { EXPR_HAPPY, EXPR_SHY, EXPR_LOVE, EXPR_CHILL, EXPR_WINKING };
static const uint8_t SC_TOUCH_EXPR_HYPER[]  = { EXPR_EXCITED, EXPR_LOVE, EXPR_SURPRISED, EXPR_KISSING, EXPR_SASSY };
static const uint8_t SC_TOUCH_EXPR_GRUMPY[] = { EXPR_ANNOYED, EXPR_SKEPTICAL, EXPR_ANGRY, EXPR_MISCHIEF, EXPR_DEVIOUS };

// Flash colors per personality
static const uint8_t SC_TOUCH_FLASH_CHILL[]  = { 100, 200, 255 };  // soft blue
static const uint8_t SC_TOUCH_FLASH_HYPER[]  = { 255, 100, 200 };  // hot pink
static const uint8_t SC_TOUCH_FLASH_GRUMPY[] = { 255,  60,  20 };  // angry orange

// Head wiggle per personality (yaw swing in tenths of degrees)
static const int SC_TOUCH_WIGGLE_CHILL  =  80;   // gentle tilt
static const int SC_TOUCH_WIGGLE_HYPER  = 200;   // big swing
static const int SC_TOUCH_WIGGLE_GRUMPY =  40;   // minimal grudging shift

inline void scFirePetReaction(uint8_t zone, uint8_t personalityIndex) {
  // Pick expression based on personality
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
    default:  // CHILL and any custom
      pool = SC_TOUCH_EXPR_CHILL;
      flashColor = SC_TOUCH_FLASH_CHILL;
      wiggle = SC_TOUCH_WIGGLE_CHILL;
      break;
  }

  // Expression
  uint8_t expr = pool[random(0, poolSize)];
  botMode.face.transitionTo(expr, 150);

  // Saying (reuse existing SAY_REACT_TAP pool)
  if (random(100) < 60) {
    char buf[MAX_SAY_LEN];
    getRandomSayingText(SAY_REACT_TAP, buf, sizeof(buf));
    botMode.speechBubble.show(buf, 2500);
  }

  // LED flash
  scLeds.flash(flashColor[0], flashColor[1], flashColor[2], 300);

  // Head wiggle — direction based on zone
  int yawDir = 0;
  if (zone == 0) yawDir = wiggle;       // left zone → wiggle left
  else if (zone == 2) yawDir = -wiggle; // right zone → wiggle right
  else yawDir = (random(2) ? wiggle : -wiggle);  // center → random direction

  scMoveYaw(yawDir, 200);
  // Schedule return to center (handled by shakeReactEnd in bot_mode)

  // Mark as reacting
  botMode.registerInteraction();
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 1500;
}

// ============================================================================
// Double-Tap Recenter — personality-flavored return to home
// ============================================================================

inline void scFireRecenter(uint8_t personalityIndex) {
  botMode.registerInteraction();

  switch (personalityIndex) {
    case PERSONALITY_HYPER:
      // Snap to center fast, then overshoot and settle
      scMoveYaw(0, 150);
      scMovePitch(SC_SERVO_Y_HOME_DEG * 10, 150);
      // Quick overshoot wiggle
      delay(150);
      scMoveYaw(60, 100);
      delay(100);
      scMoveYaw(-40, 100);
      delay(100);
      scMoveYaw(0, 100);
      scLeds.flash(255, 255, 0, 200);  // yellow flash
      botMode.face.transitionTo(EXPR_EXCITED, 150);
      break;

    case PERSONALITY_GRUMPY:
      // Reluctant pause, then slow grudging move
      botMode.face.transitionTo(EXPR_ANNOYED, 200);
      delay(400);  // dramatic pause
      scMoveYaw(0, 800);   // slow
      scMovePitch(SC_SERVO_Y_HOME_DEG * 10, 800);
      scLeds.flash(255, 80, 20, 150);  // orange flash
      break;

    default:  // CHILL
      // Smooth, gentle return
      scMoveYaw(0, 500);
      scMovePitch(SC_SERVO_Y_HOME_DEG * 10, 500);
      scLeds.flash(100, 200, 255, 250);  // soft blue
      botMode.face.transitionTo(EXPR_HAPPY, 300);
      break;
  }

  // Brief reaction state so bot doesn't immediately change expression
  botMode.shakeReacting = true;
  botMode.shakeReactEnd = millis() + 2000;
}

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_TOUCH_H
