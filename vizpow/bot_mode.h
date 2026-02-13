#ifndef BOT_MODE_H
#define BOT_MODE_H

#include <Arduino.h>
#include "config.h"
#include "bot_faces.h"
#include "bot_eyes.h"
#include "bot_sayings.h"
#include "bot_overlays.h"

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

#define BOT_WAKE_THRESHOLD     1.8f      // Acceleration magnitude to wake from sleep
#define BOT_FRAME_DELAY_MS     33        // ~30 FPS target

// ============================================================================
// Personality Presets
// ============================================================================
// Each personality adjusts idle behavior, saying frequency, and expression
// distribution. Configurable via web UI.

#define BOT_NUM_PERSONALITIES 4
#define PERSONALITY_CHILL   0
#define PERSONALITY_HYPER   1
#define PERSONALITY_GRUMPY  2
#define PERSONALITY_SLEEPY  3

struct BotPersonality {
  const char* name;
  uint32_t idleTimeoutMs;       // Time before idle state
  uint32_t sleepyTimeoutMs;     // Time before sleepy state
  uint32_t sleepTimeoutMs;      // Time before sleeping state
  uint32_t exprMinMs;           // Min time between random expressions
  uint32_t exprMaxMs;           // Max time between random expressions
  uint32_t sayMinMs;            // Min time between idle sayings
  uint32_t sayMaxMs;            // Max time between idle sayings
  uint8_t  sayChancePercent;    // % chance of saying on reaction
  uint8_t  favoriteExprs[5];    // Weighted idle expression pool
};

const BotPersonality botPersonalities[BOT_NUM_PERSONALITIES] = {
  // CHILL: relaxed, balanced, default behavior
  { "Chill", 60000, 180000, 300000, 15000, 45000, 25000, 70000, 35,
    { EXPR_NEUTRAL, EXPR_HAPPY, EXPR_THINKING, EXPR_NEUTRAL, EXPR_NEUTRAL } },

  // HYPER: energetic, frequent expressions and sayings
  { "Hyper", 120000, 300000, 600000, 5000, 15000, 8000, 25000, 70,
    { EXPR_HAPPY, EXPR_EXCITED, EXPR_HAPPY, EXPR_SURPRISED, EXPR_EXCITED } },

  // GRUMPY: annoyed, slow to engage, sarcastic
  { "Grumpy", 30000, 90000, 180000, 20000, 60000, 30000, 90000, 50,
    { EXPR_ANGRY, EXPR_NEUTRAL, EXPR_MISCHIEF, EXPR_NEUTRAL, EXPR_ANGRY } },

  // SLEEPY: drowsy, falls asleep quickly, minimal energy
  { "Sleepy", 20000, 45000, 90000, 30000, 60000, 40000, 90000, 20,
    { EXPR_SLEEPY, EXPR_NEUTRAL, EXPR_SLEEPY, EXPR_NEUTRAL, EXPR_THINKING } },
};

// ============================================================================
// Bot Mode State
// ============================================================================

struct BotModeState {
  BotState state;
  BotFaceState face;
  BotBlinkState blink;
  BotLookAround lookAround;
  BotIMUTracker imuTracker;

  // Overlays
  BotSpeechBubble speechBubble;
  BotNotification notification;
  BotTimeOverlay timeOverlay;
  BotWeatherOverlay weatherOverlay;

  // Timing
  unsigned long lastInteraction;     // Last touch/shake event
  unsigned long lastFrameTime;
  unsigned long nextRandomExpr;      // Next random idle expression change
  unsigned long nextIdleSaying;      // Next random idle saying
  unsigned long stateEnteredTime;    // When current state was entered
  unsigned long lastMotionMag;       // For motion detection

  // Sleeping animation
  float sleepBreathPhase;            // Breathing animation phase
  unsigned long lastZzzTime;         // Zzz particle timing

  // Shake reaction
  bool shakeReacting;
  unsigned long shakeReactEnd;

  // Personality
  uint8_t personalityIndex;
  const BotPersonality* personality;

  // Custom saying from web
  char customSaying[32];
  bool hasCustomSaying;

  bool initialized;

  void init() {
    state = BOT_ACTIVE;
    face.init();
    blink.init();
    lookAround.init();
    imuTracker.init();
    speechBubble.init();
    notification.init();
    timeOverlay.init();
    weatherOverlay.init();
    personalityIndex = PERSONALITY_CHILL;
    personality = &botPersonalities[PERSONALITY_CHILL];
    lastInteraction = millis();
    lastFrameTime = millis();
    nextRandomExpr = millis() + random(personality->exprMinMs, personality->exprMaxMs);
    nextIdleSaying = millis() + random(personality->sayMinMs, personality->sayMaxMs);
    stateEnteredTime = millis();
    sleepBreathPhase = 0;
    lastZzzTime = 0;
    shakeReacting = false;
    hasCustomSaying = false;
    customSaying[0] = '\0';
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

      // Show wake-up saying
      char buf[32];
      getRandomSayingText(SAY_WAKE, buf, sizeof(buf));
      speechBubble.show(buf, 2000);
    }
    state = BOT_ACTIVE;
    stateEnteredTime = millis();
    lastInteraction = millis();
    nextRandomExpr = millis() + random(personality->exprMinMs, personality->exprMaxMs);
    nextIdleSaying = millis() + random(personality->sayMinMs, personality->sayMaxMs);
  }

  // Called when bot receives a tap
  void onTap() {
    registerInteraction();

    // Pick a random reaction expression
    uint8_t reactions[] = { EXPR_SURPRISED, EXPR_HAPPY, EXPR_EXCITED, EXPR_MISCHIEF };
    uint8_t pick = reactions[random(0, 4)];
    face.transitionTo(pick, 150);

    // Maybe show a tap saying
    if (random(100) < personality->sayChancePercent) {
      char buf[32];
      getRandomSayingText(SAY_REACT_TAP, buf, sizeof(buf));
      speechBubble.show(buf, 2500);
    }

    // Schedule return to neutral
    shakeReacting = true;
    shakeReactEnd = millis() + 2000;
  }

  // Called on strong shake
  void onShake() {
    registerInteraction();
    face.transitionTo(EXPR_DIZZY, 150);

    // Show shake saying
    char buf[32];
    getRandomSayingText(SAY_REACT_SHAKE, buf, sizeof(buf));
    speechBubble.show(buf, 2500);

    shakeReacting = true;
    shakeReactEnd = millis() + 2500;
  }

  // Set expression from external source (web UI, etc.)
  void setExpression(uint8_t exprIndex, uint16_t duration = 0) {
    registerInteraction();
    face.transitionTo(exprIndex, duration);
    shakeReacting = false;
  }

  // Show a custom saying (from web UI)
  void showSaying(const char* text, uint16_t durationMs = 4000) {
    registerInteraction();
    speechBubble.show(text, durationMs);
  }

  // Show a notification banner
  void showNotification(const char* text, uint16_t durationMs = 2500) {
    notification.show(text, durationMs);
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

  // Skip if menu is visible
  if (menuVisible) return;

  // ---- State machine: activity level transitions (personality-driven) ----
  const BotPersonality* p = botMode.personality;
  switch (botMode.state) {
    case BOT_ACTIVE:
      if (timeSinceInteraction > p->sleepyTimeoutMs) {
        botMode.state = BOT_SLEEPY;
        botMode.stateEnteredTime = now;
        botMode.face.transitionTo(EXPR_SLEEPY, 1000);

        char buf[32];
        getRandomSayingText(SAY_SLEEP, buf, sizeof(buf));
        botMode.speechBubble.show(buf, 3000);
      } else if (timeSinceInteraction > p->idleTimeoutMs) {
        if (botMode.state != BOT_IDLE) {
          botMode.state = BOT_IDLE;
          botMode.stateEnteredTime = now;
        }
      }
      break;

    case BOT_IDLE:
      if (timeSinceInteraction > p->sleepyTimeoutMs) {
        botMode.state = BOT_SLEEPY;
        botMode.stateEnteredTime = now;
        botMode.face.transitionTo(EXPR_SLEEPY, 1000);

        char buf[32];
        getRandomSayingText(SAY_SLEEP, buf, sizeof(buf));
        botMode.speechBubble.show(buf, 3000);
      }
      break;

    case BOT_SLEEPY:
      if (timeSinceInteraction > p->sleepTimeoutMs) {
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

  // ---- Random idle expression changes (personality-driven) ----
  if (botMode.state == BOT_ACTIVE && !botMode.shakeReacting && now >= botMode.nextRandomExpr) {
    uint8_t pick = p->favoriteExprs[random(0, 5)];
    botMode.face.transitionTo(pick, 500);
    botMode.nextRandomExpr = now + random(p->exprMinMs, p->exprMaxMs);
  }

  // ---- Random idle sayings (personality-driven) ----
  if ((botMode.state == BOT_ACTIVE || botMode.state == BOT_IDLE) &&
      !botMode.speechBubble.active && now >= botMode.nextIdleSaying) {
    char buf[32];
    getRandomSayingText(SAY_IDLE, buf, sizeof(buf));
    botMode.speechBubble.show(buf, 3500);
    botMode.nextIdleSaying = now + random(p->sayMinMs, p->sayMaxMs);
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

  // Update overlays
  botMode.speechBubble.update();
  botMode.notification.update();
  botMode.weatherOverlay.update();
}

// ============================================================================
// Bot Mode Render (called each frame after update)
// ============================================================================

// Background style (declared before renderBotMode which uses it)
uint8_t botBackgroundStyle = 0;  // 0=solid black, 1=subtle gradient, 2=breathing, 3=starfield

// Track previous frame's face bounds for minimal-clear approach
static int16_t prevFaceTop = 0;
static int16_t prevFaceBot = 0;
static bool botFirstFrame = true;

void renderBotMode() {
  if (gfx == nullptr) return;
  if (menuVisible) return;

  // Anti-flicker strategy: only clear the area where the face was drawn
  // on the previous frame, then immediately redraw. This minimizes the
  // time any pixel is black, reducing perceived flicker on SPI displays.
  if (botFirstFrame) {
    gfx->fillScreen(BOT_COLOR_BG);
    botFirstFrame = false;
  }

  // ---- Draw background based on style ----
  switch (botBackgroundStyle) {
    case 0:
      // Solid black — just clear the face region
      {
        int16_t clearTop = BOT_FACE_CY - 90;
        if (clearTop < 0) clearTop = 0;
        gfx->fillRect(0, clearTop, LCD_WIDTH, 250 - clearTop, BOT_COLOR_BG);
      }
      break;

    case 1:
      // Subtle dark gradient — dark blue at top fading to black
      for (int16_t y = 0; y < LCD_HEIGHT; y += 4) {
        uint8_t b = (uint8_t)((1.0f - (float)y / LCD_HEIGHT) * 12);
        uint16_t c = ((b >> 3) << 11) | ((b >> 2) << 5) | (b >> 1);
        gfx->fillRect(0, y, LCD_WIDTH, 4, c);
      }
      break;

    case 2:
      // Breathing color — slow pulse of dark accent color
      {
        float breathT = (float)(millis() % 6000) / 6000.0f;
        uint8_t intensity = (uint8_t)(sinf(breathT * TWO_PI) * 4.0f + 4.0f);
        uint16_t bgColor = ((intensity >> 3) << 11) | ((intensity >> 2) << 5) | (intensity >> 1);
        int16_t clearTop = BOT_FACE_CY - 90;
        if (clearTop < 0) clearTop = 0;
        gfx->fillRect(0, clearTop, LCD_WIDTH, 250 - clearTop, bgColor);
      }
      break;

    case 3:
      // Starfield — black with a few dim white dots
      {
        int16_t clearTop = BOT_FACE_CY - 90;
        if (clearTop < 0) clearTop = 0;
        gfx->fillRect(0, clearTop, LCD_WIDTH, 250 - clearTop, BOT_COLOR_BG);
        // Draw a few "stars" that twinkle
        for (int i = 0; i < 8; i++) {
          // Deterministic positions based on index (not random per frame)
          int16_t sx = (i * 31 + 17) % LCD_WIDTH;
          int16_t sy = (i * 47 + 11) % LCD_HEIGHT;
          // Twinkle: brightness varies with time offset
          float twinkle = sinf((float)(millis() + i * 500) / 1500.0f);
          if (twinkle > 0.3f) {
            uint8_t bright = (uint8_t)(twinkle * 8);
            uint16_t starColor = ((bright >> 3) << 11) | ((bright >> 2) << 5) | (bright >> 3);
            gfx->fillRect(sx, sy, 2, 2, starColor);
          }
        }
      }
      break;

    default:
      gfx->fillRect(0, BOT_FACE_CY - 90, LCD_WIDTH, 250 - (BOT_FACE_CY - 90), BOT_COLOR_BG);
      break;
  }

  // Render the face
  renderBotFace(botMode.face);

  // ---- Sleeping: draw Zzz animation ----
  if (botMode.state == BOT_SLEEPING) {
    unsigned long now = millis();
    float t = (float)(now % 3000) / 3000.0f;

    int16_t zBaseX = BOT_FACE_CX + 50;
    int16_t zBaseY = BOT_FACE_CY - 40;

    gfx->setTextColor(botFaceColor);

    for (int i = 0; i < 3; i++) {
      float phase = fmodf(t + i * 0.33f, 1.0f);
      int16_t zx = zBaseX + i * 12 + (int16_t)(sinf(phase * PI * 2) * 4);
      int16_t zy = zBaseY - (int16_t)(phase * 50);

      if (phase < 0.8f) {
        gfx->setCursor(zx, zy);
        gfx->setTextSize(1 + i);
        gfx->print("Z");
      }
    }
  }

  // ---- Sleepy: slow blink animation ----
  if (botMode.state == BOT_SLEEPY) {
    float t = (float)(millis() % 4000) / 4000.0f;
    botMode.face.blinkAmount = 0.3f + 0.4f * (sinf(t * TWO_PI) * 0.5f + 0.5f);
  }

  // ---- Render overlays (on top of face) ----
  botMode.speechBubble.render();
  botMode.notification.render();
  botMode.timeOverlay.render();
  botMode.weatherOverlay.render();
}

// ============================================================================
// Bot Mode Entry/Exit
// ============================================================================

void enterBotMode() {
  botFirstFrame = true;  // Force full clear on entry

  if (!botMode.initialized) {
    botMode.init();

    // Show greeting on first entry
    char buf[32];
    getRandomSayingText(SAY_GREETING, buf, sizeof(buf));
    botMode.speechBubble.show(buf, 2500);
  } else {
    // Re-entering bot mode: reset to active
    botMode.state = BOT_ACTIVE;
    botMode.lastInteraction = millis();
    botMode.face.transitionTo(EXPR_HAPPY, 300);
    botMode.shakeReacting = true;
    botMode.shakeReactEnd = millis() + 1500;

    char buf[32];
    getRandomSayingText(SAY_GREETING, buf, sizeof(buf));
    botMode.speechBubble.show(buf, 2000);
  }

  // Clear screen for bot mode
  if (gfx != nullptr) {
    gfx->fillScreen(BOT_COLOR_BG);
  }
}

void exitBotMode() {
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

void setBotBackgroundStyle(uint8_t style) {
  botBackgroundStyle = style;
}

uint8_t getBotBackgroundStyle() {
  return botBackgroundStyle;
}

void showBotSaying(const char* text, uint16_t durationMs) {
  botMode.showSaying(text, durationMs);
}

void toggleBotTimeOverlay() {
  botMode.timeOverlay.enabled = !botMode.timeOverlay.enabled;
}

bool isBotTimeOverlayEnabled() {
  return botMode.timeOverlay.enabled;
}

void setBotPersonality(uint8_t index) {
  if (index >= BOT_NUM_PERSONALITIES) index = 0;
  botMode.personalityIndex = index;
  botMode.personality = &botPersonalities[index];
  botMode.registerInteraction();

  // Show notification
  char buf[32];
  snprintf(buf, sizeof(buf), "Personality: %s", botMode.personality->name);
  botMode.showNotification(buf, 2000);
}

uint8_t getBotPersonality() {
  return botMode.personalityIndex;
}

void setBotWeatherEnabled(bool enabled) {
  botMode.weatherOverlay.enabled = enabled;
}

void setBotWeatherLocation(float lat, float lon) {
  botMode.weatherOverlay.setLocation(lat, lon);
}

#else

// Stubs when LCD is not available
inline void runBotMode() {}
inline void enterBotMode() {}
inline void exitBotMode() {}
inline void setBotExpression(uint8_t index) {}
inline void setBotFaceColor(uint16_t color) {}
inline void showBotSaying(const char* text, uint16_t durationMs) {}
inline void toggleBotTimeOverlay() {}
inline bool isBotTimeOverlayEnabled() { return false; }
inline void setBotPersonality(uint8_t index) {}
inline uint8_t getBotPersonality() { return 0; }
inline void setBotWeatherEnabled(bool enabled) {}
inline void setBotWeatherLocation(float lat, float lon) {}
inline void setBotBackgroundStyle(uint8_t style) {}
inline uint8_t getBotBackgroundStyle() { return 0; }

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // BOT_MODE_H
