#ifndef BOT_MODE_H
#define BOT_MODE_H

#include <Arduino.h>
#include "config.h"
#include "bot_faces.h"
#include "bot_eyes.h"

// ============================================================================
// Bot Mode — Main State Machine & Render Pipeline
// ============================================================================
// Bot Mode is the 4th display mode for TARGET_LCD. It renders an animated
// desktop companion character on the 240x280 LCD using procedural geometry.
//
// States: ACTIVE -> IDLE -> SLEEPY -> SLEEPING
// Any interaction (touch, shake, motion) wakes the bot.
// ============================================================================

#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

// External references
extern Arduino_GFX *gfx;
extern float accelX, accelY, accelZ;
extern bool menuVisible;

// Bot activity states
enum BotState : uint8_t {
  BOT_ACTIVE = 0,    // Normal active state — expressions, look-around, reactions
  BOT_IDLE,          // Reduced movement after period of no interaction
  BOT_SLEEPY,        // Transitioning to sleep (half-lidded, yawns)
  BOT_SLEEPING       // Asleep (eyes closed, Zzz animation)
};

// ============================================================================
// Bot Mode Configuration
// ============================================================================

#define BOT_IDLE_TIMEOUT_MS     60000    // 1 min of no interaction -> idle
#define BOT_SLEEPY_TIMEOUT_MS  180000    // 3 min of no interaction -> sleepy
#define BOT_SLEEP_TIMEOUT_MS   300000    // 5 min of no interaction -> sleeping
#define BOT_WAKE_THRESHOLD     1.8f      // Acceleration magnitude to wake from sleep
#define BOT_FRAME_DELAY_MS     33        // ~30 FPS target
#define BOT_RANDOM_EXPR_MIN_MS 15000     // Min time between random idle expressions
#define BOT_RANDOM_EXPR_MAX_MS 45000     // Max time between random idle expressions

// ============================================================================
// Bot Mode State
// ============================================================================

struct BotModeState {
  BotState state;
  BotFaceState face;
  BotBlinkState blink;
  BotLookAround lookAround;
  BotIMUTracker imuTracker;

  // Timing
  unsigned long lastInteraction;     // Last touch/shake event
  unsigned long lastFrameTime;
  unsigned long nextRandomExpr;      // Next random idle expression change
  unsigned long stateEnteredTime;    // When current state was entered
  unsigned long lastMotionMag;       // For motion detection

  // Sleeping animation
  float sleepBreathPhase;            // Breathing animation phase
  unsigned long lastZzzTime;         // Zzz particle timing

  // Shake reaction
  bool shakeReacting;
  unsigned long shakeReactEnd;

  bool initialized;

  void init() {
    state = BOT_ACTIVE;
    face.init();
    blink.init();
    lookAround.init();
    imuTracker.init();
    lastInteraction = millis();
    lastFrameTime = millis();
    nextRandomExpr = millis() + random(BOT_RANDOM_EXPR_MIN_MS, BOT_RANDOM_EXPR_MAX_MS);
    stateEnteredTime = millis();
    sleepBreathPhase = 0;
    lastZzzTime = 0;
    shakeReacting = false;
    initialized = true;
  }

  // Register an interaction (resets idle timers)
  void registerInteraction() {
    lastInteraction = millis();
    if (state != BOT_ACTIVE) {
      wake();
    }
  }

  // Wake from any sleep/idle state
  void wake() {
    if (state == BOT_SLEEPING || state == BOT_SLEEPY) {
      // Wake-up: transition to surprised briefly, then neutral
      face.transitionTo(EXPR_SURPRISED, 200);
      shakeReacting = true;
      shakeReactEnd = millis() + 800;
    }
    state = BOT_ACTIVE;
    stateEnteredTime = millis();
    lastInteraction = millis();
    nextRandomExpr = millis() + random(BOT_RANDOM_EXPR_MIN_MS, BOT_RANDOM_EXPR_MAX_MS);
  }

  // Called when bot receives a tap
  void onTap() {
    registerInteraction();

    // Pick a random reaction expression
    uint8_t reactions[] = { EXPR_SURPRISED, EXPR_HAPPY, EXPR_EXCITED, EXPR_MISCHIEF };
    uint8_t pick = reactions[random(0, 4)];
    face.transitionTo(pick, 150);

    // Schedule return to neutral
    shakeReacting = true;
    shakeReactEnd = millis() + 2000;
  }

  // Called on strong shake
  void onShake() {
    registerInteraction();
    face.transitionTo(EXPR_DIZZY, 150);
    shakeReacting = true;
    shakeReactEnd = millis() + 2500;
  }

  // Set expression from external source (web UI, etc.)
  void setExpression(uint8_t exprIndex, uint16_t duration = 0) {
    registerInteraction();
    face.transitionTo(exprIndex, duration);
    // Don't auto-return for externally set expressions
    shakeReacting = false;
  }
};

// Global bot mode state
BotModeState botMode;

// ============================================================================
// Bot Mode Update (called each frame when in Bot Mode)
// ============================================================================

void updateBotMode() {
  if (!botMode.initialized) {
    botMode.init();
  }

  unsigned long now = millis();
  unsigned long timeSinceInteraction = now - botMode.lastInteraction;

  // Skip rendering if menu is visible
  if (menuVisible) return;

  // ---- State machine: activity level transitions ----
  switch (botMode.state) {
    case BOT_ACTIVE:
      if (timeSinceInteraction > BOT_SLEEPY_TIMEOUT_MS) {
        botMode.state = BOT_SLEEPY;
        botMode.stateEnteredTime = now;
        botMode.face.transitionTo(EXPR_SLEEPY, 1000);
      } else if (timeSinceInteraction > BOT_IDLE_TIMEOUT_MS) {
        botMode.state = BOT_IDLE;
        botMode.stateEnteredTime = now;
      }
      break;

    case BOT_IDLE:
      if (timeSinceInteraction > BOT_SLEEPY_TIMEOUT_MS) {
        botMode.state = BOT_SLEEPY;
        botMode.stateEnteredTime = now;
        botMode.face.transitionTo(EXPR_SLEEPY, 1000);
      }
      break;

    case BOT_SLEEPY:
      if (timeSinceInteraction > BOT_SLEEP_TIMEOUT_MS) {
        botMode.state = BOT_SLEEPING;
        botMode.stateEnteredTime = now;
      }
      break;

    case BOT_SLEEPING:
      // Check for wake-up via motion
      {
        float mag = sqrtf(accelX * accelX + accelY * accelY + accelZ * accelZ);
        if (mag > BOT_WAKE_THRESHOLD) {
          botMode.wake();
        }
      }
      break;
  }

  // ---- Shake reaction timeout (return to neutral) ----
  if (botMode.shakeReacting && now >= botMode.shakeReactEnd) {
    botMode.shakeReacting = false;
    if (botMode.state == BOT_ACTIVE) {
      botMode.face.transitionTo(EXPR_NEUTRAL, 400);
    }
  }

  // ---- Random idle expression changes (only in ACTIVE state) ----
  if (botMode.state == BOT_ACTIVE && !botMode.shakeReacting && now >= botMode.nextRandomExpr) {
    // Pick a random friendly expression
    uint8_t idleExprs[] = { EXPR_NEUTRAL, EXPR_HAPPY, EXPR_THINKING, EXPR_NEUTRAL, EXPR_NEUTRAL };
    uint8_t pick = idleExprs[random(0, 5)];
    botMode.face.transitionTo(pick, 500);
    botMode.nextRandomExpr = now + random(BOT_RANDOM_EXPR_MIN_MS, BOT_RANDOM_EXPR_MAX_MS);
  }

  // ---- Update animation systems ----

  // Blink (not while sleeping or during special eye modes)
  if (botMode.state != BOT_SLEEPING &&
      botMode.face.eyeMode == EYE_NORMAL) {
    botMode.face.blinkAmount = botMode.blink.update();
  } else if (botMode.state == BOT_SLEEPING) {
    botMode.face.blinkAmount = 1.0f;  // Eyes fully closed
  }

  // Look-around (only when active or idle, and not reacting)
  int16_t lookX = 0, lookY = 0;
  if ((botMode.state == BOT_ACTIVE || botMode.state == BOT_IDLE) &&
      !botMode.shakeReacting) {
    botMode.lookAround.update(lookX, lookY);
  }

  // IMU tracking (always active except sleeping)
  int16_t imuX = 0, imuY = 0;
  if (botMode.state != BOT_SLEEPING) {
    botMode.imuTracker.update(imuX, imuY);
  }

  // Combine dynamic pupil offsets
  botMode.face.dynamicPupilX = lookX + imuX;
  botMode.face.dynamicPupilY = lookY + imuY;

  // Update expression transition
  botMode.face.update();
}

// ============================================================================
// Bot Mode Render (called each frame after update)
// ============================================================================

void renderBotMode() {
  if (gfx == nullptr) return;
  if (menuVisible) return;

  // Clear to black
  gfx->fillScreen(BOT_COLOR_BG);

  // Render the face
  renderBotFace(botMode.face);

  // ---- Sleeping: draw Zzz animation ----
  if (botMode.state == BOT_SLEEPING) {
    unsigned long now = millis();
    // Floating Z characters
    float t = (float)(now % 3000) / 3000.0f;  // 3-second cycle

    int16_t zBaseX = BOT_FACE_CX + 50;
    int16_t zBaseY = BOT_FACE_CY - 40;

    gfx->setTextSize(2);
    gfx->setTextColor(botFaceColor);

    // Three Z's at different phases, floating upward
    for (int i = 0; i < 3; i++) {
      float phase = fmodf(t + i * 0.33f, 1.0f);
      int16_t zx = zBaseX + i * 12 + (int16_t)(sinf(phase * PI * 2) * 4);
      int16_t zy = zBaseY - (int16_t)(phase * 50);
      uint8_t alpha = (phase < 0.8f) ? 255 : (uint8_t)((1.0f - phase) * 5 * 255);

      if (alpha > 128) {
        gfx->setCursor(zx, zy);
        gfx->setTextSize(1 + i);
        gfx->print("Z");
      }
    }
  }

  // ---- Sleepy: slow blink animation ----
  if (botMode.state == BOT_SLEEPY) {
    // Override blink to do slow half-blinks
    float t = (float)(millis() % 4000) / 4000.0f;
    // Slow sine wave between 0.3 and 0.7 (half-lidded)
    botMode.face.blinkAmount = 0.3f + 0.4f * (sinf(t * TWO_PI) * 0.5f + 0.5f);
  }
}

// ============================================================================
// Bot Mode Entry/Exit
// ============================================================================

void enterBotMode() {
  if (!botMode.initialized) {
    botMode.init();
  } else {
    // Re-entering bot mode: reset to active
    botMode.state = BOT_ACTIVE;
    botMode.lastInteraction = millis();
    botMode.face.transitionTo(EXPR_HAPPY, 300);
    botMode.shakeReacting = true;
    botMode.shakeReactEnd = millis() + 1500;
  }

  // Clear screen for bot mode
  if (gfx != nullptr) {
    gfx->fillScreen(BOT_COLOR_BG);
  }
}

void exitBotMode() {
  // Clean up — clear screen for next mode
  if (gfx != nullptr) {
    gfx->fillScreen(BOT_COLOR_BG);
  }
}

// ============================================================================
// Combined Bot Mode loop function (update + render)
// ============================================================================

void runBotMode() {
  updateBotMode();
  renderBotMode();
}

// ============================================================================
// Bot Mode accessors for web/touch control
// ============================================================================

uint8_t getBotExpression() {
  return botMode.face.targetExpr;
}

uint8_t getBotState() {
  return (uint8_t)botMode.state;
}

void setBotExpression(uint8_t index) {
  botMode.setExpression(index);
}

void setBotFaceColor(uint16_t color) {
  botFaceColor = color;
}

#else

// Stubs when LCD is not available
inline void runBotMode() {}
inline void enterBotMode() {}
inline void exitBotMode() {}
inline void setBotExpression(uint8_t index) {}
inline void setBotFaceColor(uint16_t color) {}

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // BOT_MODE_H
