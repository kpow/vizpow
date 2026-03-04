#ifndef BOT_MODE_H
#define BOT_MODE_H

#include <Arduino.h>
#include "config.h"
#include "layout.h"
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
extern GfxDevice *gfx;
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
#define WLED_SAY_PRE_DELAY_MS  50        // ms WLED gets head-start before LCD bubble appears

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
  // CHILL: lively and expressive, default behavior
  { "Chill", 90000, 240000, 360000, 4000, 10000, 16000, 40000, 30,
    { EXPR_NEUTRAL, EXPR_HAPPY, EXPR_THINKING, EXPR_MISCHIEF, EXPR_BLISS } },

  // HYPER: energetic, constant expressions and sayings
  { "Hyper", 180000, 360000, 600000, 2000, 6000, 8000, 24000, 45,
    { EXPR_HAPPY, EXPR_EXCITED, EXPR_SURPRISED, EXPR_LOVE, EXPR_PROUD } },

  // GRUMPY: annoyed but still chatty and expressive
  { "Grumpy", 45000, 120000, 240000, 6000, 18000, 20000, 55000, 30,
    { EXPR_ANGRY, EXPR_ANNOYED, EXPR_MISCHIEF, EXPR_SKEPTICAL, EXPR_ANGRY } },

  // SLEEPY: drowsy but still talks a bit
  { "Sleepy", 30000, 60000, 120000, 10000, 25000, 30000, 70000, 15,
    { EXPR_SLEEPY, EXPR_BLISS, EXPR_NEUTRAL, EXPR_THINKING, EXPR_SHY } },
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

  // Audio reaction cooldown (Core S3)
  unsigned long lastAudioReactionMs;

  // Proximity reaction cooldown (Core S3)
  unsigned long lastProxReactionMs;
  uint8_t peekCount;               // Cover/uncover cycles for peek-a-boo
  unsigned long firstPeekMs;       // When first cover/uncover started
  bool lastNearState;              // Previous nearDetected state
  bool lastCoverState;             // Previous coverDetected state
  unsigned long coverStartMs;      // When cover started (for sustained cover)

  // Personality
  uint8_t personalityIndex;
  const BotPersonality* personality;

  // Custom saying from web
  char customSaying[MAX_SAY_LEN];
  bool hasCustomSaying;

  // Pending say: LCD bubble delayed by WLED_SAY_PRE_DELAY_MS for WLED sync
  char pendingSayText[MAX_SAY_LEN];
  uint16_t pendingSayDuration;
  unsigned long pendingSayAt;  // millis() when LCD bubble should fire; 0 = none

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
    lastAudioReactionMs = 0;
    lastProxReactionMs = 0;
    peekCount = 0;
    firstPeekMs = 0;
    lastNearState = false;
    lastCoverState = false;
    coverStartMs = 0;
    hasCustomSaying = false;
    customSaying[0] = '\0';
    pendingSayText[0] = '\0';
    pendingSayDuration = 0;
    pendingSayAt = 0;
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
      char buf[MAX_SAY_LEN];
      getRandomSayingText(SAY_WAKE, buf, sizeof(buf));
      speechBubble.show(buf, 2000);

      #ifdef TARGET_CORES3
      botSounds.play(SFX_WAKE_CHIME);
      #endif
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
    uint8_t reactions[] = { EXPR_SURPRISED, EXPR_HAPPY, EXPR_EXCITED, EXPR_MISCHIEF, EXPR_LOVE, EXPR_SHY, EXPR_CONFUSED, EXPR_PROUD };
    uint8_t pick = reactions[random(0, 8)];
    face.transitionTo(pick, 150);

    #ifdef TARGET_CORES3
    botSounds.play(SFX_TAP_BOOP);
    #endif

    // Maybe show a tap saying
    if (random(100) < personality->sayChancePercent) {
      char buf[MAX_SAY_LEN];
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

    #ifdef TARGET_CORES3
    botSounds.play(SFX_SHAKE_RATTLE);
    #endif

    // Show shake saying
    char buf[MAX_SAY_LEN];
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

  // Schedule LCD bubble after WLED pre-delay (called from showBotSaying after wledQueueText)
  void scheduleSaying(const char* text, uint16_t durationMs) {
    registerInteraction();
    strncpy(pendingSayText, text, MAX_SAY_LEN - 1);
    pendingSayText[MAX_SAY_LEN - 1] = '\0';
    pendingSayDuration = durationMs;
    pendingSayAt = millis() + WLED_SAY_PRE_DELAY_MS;
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
  // Sleep is disabled for now — bot stays active or idle, never sleeps.
  const BotPersonality* p = botMode.personality;
  switch (botMode.state) {
    case BOT_ACTIVE:
      if (timeSinceInteraction > p->idleTimeoutMs) {
        if (botMode.state != BOT_IDLE) {
          botMode.state = BOT_IDLE;
          botMode.stateEnteredTime = now;
        }
      }
      break;

    case BOT_IDLE:
      // Stay idle — no sleep transitions
      break;

    case BOT_SLEEPY:
      #ifdef TARGET_CORES3
      // Play descending lullaby when entering sleepy state
      if (botMode.stateEnteredTime == now) {
        botSounds.play(SFX_SLEEP_DESCEND);
      }
      #endif
      // fall through
    case BOT_SLEEPING:
      // If somehow in a sleep state, wake immediately
      botMode.wake();
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
    uint8_t pick;
    if (random(100) < 35) {
      // 35% chance: pick from full expression range for variety
      pick = random(0, BOT_NUM_EXPRESSIONS);
    } else {
      // 65% chance: pick from personality favorites
      pick = p->favoriteExprs[random(0, 5)];
    }
    botMode.face.transitionTo(pick, 500);
    botMode.nextRandomExpr = now + random(p->exprMinMs, p->exprMaxMs);
  }

  // ---- Random idle sayings (personality-driven) ----
  if ((botMode.state == BOT_ACTIVE || botMode.state == BOT_IDLE) &&
      !botMode.speechBubble.active && now >= botMode.nextIdleSaying) {
    char buf[MAX_SAY_LEN];
    getRandomSayingText(SAY_IDLE, buf, sizeof(buf));
    botMode.speechBubble.show(buf, 3500);
    botMode.nextIdleSaying = now + random(p->sayMinMs, p->sayMaxMs);
  }

  // ---- Core S3: Audio-reactive expressions ----
  #ifdef TARGET_CORES3
  if (sysStatus.micReady && !botMode.shakeReacting &&
      (now - botMode.lastAudioReactionMs > 3000)) {

    // Spike (clap/bang): SURPRISED + sound + saying
    if (audioAnalysis.spikeDetected) {
      botMode.face.transitionTo(EXPR_SURPRISED, 100);
      botSounds.play(SFX_CLAP_REACT);
      char buf[MAX_SAY_LEN];
      getRandomSayingText(SAY_REACT_SOUND, buf, sizeof(buf));
      botMode.speechBubble.show(buf, 2000);
      botMode.shakeReacting = true;
      botMode.shakeReactEnd = now + 2000;
      botMode.lastAudioReactionMs = now;
      botMode.registerInteraction();
    }
    // Speech (someone talking): FOCUSED, hold while speech continues
    else if (audioAnalysis.speechDetected) {
      botMode.face.transitionTo(EXPR_FOCUSED, 300);
      botMode.lastAudioReactionMs = now;
      botMode.registerInteraction();
    }
    // Extended silence: sleepy expression
    else if (audioAnalysis.silenceExtended && botMode.state == BOT_IDLE) {
      botMode.face.transitionTo(EXPR_SLEEPY, 500);
      botMode.lastAudioReactionMs = now;
    }
  }
  #endif

  // ---- Core S3: Proximity-reactive expressions ----
  #ifdef TARGET_CORES3
  if (sysStatus.proxLightReady && !botMode.shakeReacting &&
      (now - botMode.lastProxReactionMs > 2000)) {

    bool nearNow = proxLight.nearDetected;
    bool coverNow = proxLight.coverDetected;

    // Peek-a-boo: 3+ cover/uncover cycles in 4 seconds
    if (botMode.lastCoverState && !coverNow) {
      // Uncover event
      if (botMode.peekCount == 0) {
        botMode.firstPeekMs = now;
      }
      botMode.peekCount++;
      if (botMode.peekCount >= 3 && (now - botMode.firstPeekMs < 4000)) {
        botMode.face.transitionTo(EXPR_HAPPY, 150);
        char buf[MAX_SAY_LEN];
        strncpy(buf, "Peekaboo", sizeof(buf));
        botMode.speechBubble.show(buf, 2500);
        botMode.shakeReacting = true;
        botMode.shakeReactEnd = now + 2500;
        botMode.lastProxReactionMs = now;
        botMode.peekCount = 0;
        botMode.registerInteraction();
      }
    }
    // Reset peek counter after 4s window
    if (botMode.peekCount > 0 && (now - botMode.firstPeekMs > 4000)) {
      botMode.peekCount = 0;
    }

    // Sustained cover (>2s): WORRIED expression
    if (coverNow) {
      if (!botMode.lastCoverState) {
        botMode.coverStartMs = now;  // Cover just started
      }
      if (now - botMode.coverStartMs > 2000 && botMode.lastProxReactionMs < botMode.coverStartMs) {
        botMode.face.transitionTo(EXPR_WORRIED, 300);
        char buf[MAX_SAY_LEN];
        strncpy(buf, "Dark", sizeof(buf));
        botMode.speechBubble.show(buf, 2000);
        botMode.lastProxReactionMs = now;
      }
    }

    // Hand approaching (nearDetected rising edge): SURPRISED or SHY
    if (nearNow && !botMode.lastNearState && !coverNow) {
      uint8_t pick = random(2) == 0 ? EXPR_SURPRISED : EXPR_SHY;
      botMode.face.transitionTo(pick, 150);
      char buf[MAX_SAY_LEN];
      getRandomSayingText(SAY_REACT_PROXIMITY, buf, sizeof(buf));
      botMode.speechBubble.show(buf, 2000);
      botMode.shakeReacting = true;
      botMode.shakeReactEnd = now + 2000;
      botMode.lastProxReactionMs = now;
      botMode.registerInteraction();
    }

    botMode.lastNearState = nearNow;
    botMode.lastCoverState = coverNow;
  }
  #endif

  // ---- Fire pending say when WLED pre-delay has elapsed ----
  // skipWled=true: wledQueueText was already called in showBotSaying(), don't double-queue
  if (botMode.pendingSayAt > 0 && now >= botMode.pendingSayAt) {
    botMode.speechBubble.show(botMode.pendingSayText, botMode.pendingSayDuration, true);
    botMode.pendingSayAt = 0;
  }

  // ---- Update animation systems ----

  // Blink (not while sleeping or during special eye modes)
  if (botMode.state != BOT_SLEEPING &&
      botMode.face.eyeMode == EYE_NORMAL) {
    botMode.face.blinkAmount = botMode.blink.update();
  } else if (botMode.state == BOT_SLEEPING) {
    botMode.face.blinkAmount = 0.0f;  // Don't squish — EYE_CLOSED handles it
    botMode.face.eyeMode = EYE_CLOSED;
  }

  // Look-around (only when active or idle, and not reacting)
  int16_t lookX = 0, lookY = 0;
  if ((botMode.state == BOT_ACTIVE || botMode.state == BOT_IDLE) &&
      !botMode.shakeReacting) {
    botMode.lookAround.update(lookX, lookY);
  }

  // Dynamic pupil offsets — random look-around only (IMU tracking disabled for now)
  botMode.face.dynamicPupilX = lookX;
  botMode.face.dynamicPupilY = lookY;

  // Update expression transition
  botMode.face.update();

  // Update overlays
  botMode.speechBubble.update();
  botMode.notification.update();
}

// ============================================================================
// Bot Mode Render (called each frame after update)
// ============================================================================

// Background style (declared before renderBotMode which uses it)
uint8_t botBackgroundStyle = 0;  // 0=solid black, 1=subtle gradient, 2=breathing, 3=starfield, 4=ambient

// External references for ambient background
extern CRGBPalette16 currentPalette;
extern CRGBPalette16 palettes[];
extern uint8_t paletteIndex;
extern uint8_t effectIndex;
extern CRGB leds[];

// Use the function pointer tables from effects_ambient.h
#if defined(HIRES_ENABLED)
extern const AmbientFunc ambientHiResFuncs[];
#endif
extern const AmbientFunc ambientLedFuncs[];

// Render ambient effect as bot background (respects hiResMode)
void renderBotAmbientBackground() {
  uint8_t idx = effectIndex % NUM_AMBIENT_EFFECTS;

  #if defined(HIRES_ENABLED)
  if (hiResMode) {
    // Hi-res: render effect directly to LCD canvas
    ambientHiResFuncs[idx]();
  } else {
  #endif
    // Pixel mode: run LED effect, then render leds[] as blocky background
    ambientLedFuncs[idx]();
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        uint16_t ledIndex = XY(x, y);
        CRGB color = leds[ledIndex];
        uint16_t c565 = crgbToRgb565(color);
        int16_t screenX = GRID_OFFSET_X + x * (PIXEL_SIZE + PIXEL_GAP);
        int16_t screenY = GRID_OFFSET_Y + y * (PIXEL_SIZE + PIXEL_GAP);
        gfx->fillRect(screenX, screenY, PIXEL_SIZE, PIXEL_SIZE, c565);
      }
    }
  #if defined(HIRES_ENABLED)
  }
  #endif
}

// ============================================================================
// Offscreen Canvas — eliminates ALL flicker
// ============================================================================
// Instead of drawing directly to the screen (which flickers when elements are
// erased then redrawn), we draw each frame to an offscreen RAM buffer first,
// then flush the whole buffer to the display in one atomic SPI transfer.
// This is the standard double-buffer / sprite technique for TFT displays.

static bool botFirstFrame = true;

void renderBotMode() {
  if (gfx == nullptr) return;
  if (menuVisible) return;

  // ---- Canvas management (both targets use DisplayProxy with LGFX_Sprite) ----
  gfx->beginCanvas();

  // ---- Clear canvas with background ----
  uint16_t bgColor = BOT_COLOR_BG;

  if (botBackgroundStyle == 0) {
    // Solid black
    gfx->fillScreen(BOT_COLOR_BG);
  } else if (botBackgroundStyle == 1) {
    // Subtle gradient
    for (int16_t y = 0; y < LCD_HEIGHT; y += 4) {
      uint8_t b = (uint8_t)((1.0f - (float)y / LCD_HEIGHT) * 12);
      uint16_t c = ((b >> 3) << 11) | ((b >> 2) << 5) | (b >> 1);
      gfx->fillRect(0, y, LCD_WIDTH, 4, c);
    }
  } else if (botBackgroundStyle == 2) {
    // Breathing
    float breathT = (float)(millis() % 6000) / 6000.0f;
    uint8_t intensity = (uint8_t)(sinf(breathT * TWO_PI) * 4.0f + 4.0f);
    bgColor = ((intensity >> 3) << 11) | ((intensity >> 2) << 5) | (intensity >> 1);
    gfx->fillScreen(bgColor);
  } else if (botBackgroundStyle == 3) {
    // Starfield on black
    gfx->fillScreen(BOT_COLOR_BG);
    for (int i = 0; i < 8; i++) {
      int16_t sx = (i * 31 + 17) % LCD_WIDTH;
      int16_t sy = (i * 47 + 11) % LCD_HEIGHT;
      float twinkle = sinf((float)(millis() + i * 500) / 1500.0f);
      if (twinkle > 0.3f) {
        uint8_t bright = (uint8_t)(twinkle * 8);
        uint16_t starColor = ((bright >> 3) << 11) | ((bright >> 2) << 5) | (bright >> 3);
        gfx->fillRect(sx, sy, 2, 2, starColor);
      }
    }
  } else if (botBackgroundStyle == 4) {
    // Ambient effect as background — face renders on top
    #if defined(HIRES_ENABLED)
    if (!hiResMode)
    #endif
    {
      gfx->fillScreen(BOT_COLOR_BG);  // Clear first — pixel grid doesn't cover full screen
    }
    renderBotAmbientBackground();
    bgColor = 0x0000;  // Face erase uses black (though prevFrame is invalidated)
  }

  // Since we redraw everything fresh each frame, skip the old targeted-erase logic
  prevFrame.invalidate();

  // ---- Render the face ----
  renderBotFace(botMode.face, bgColor);

  // ---- Sleeping: draw Zzz animation ----
  if (botMode.state == BOT_SLEEPING) {
    unsigned long now = millis();
    float t = (float)(now % 3000) / 3000.0f;

    int16_t zBaseX = BOT_FACE_CX + ZZZ_OFFSET_X;
    int16_t zBaseY = BOT_FACE_CY + ZZZ_OFFSET_Y;

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
  // ---- Flush canvas to screen in one atomic transfer — zero flicker ----
  gfx->flushCanvas();
}

// ============================================================================
// Bot Mode Entry/Exit
// ============================================================================

void enterBotMode() {
  botFirstFrame = true;
  prevFrame.invalidate();

  if (!botMode.initialized) {
    botMode.init();

    #ifdef TARGET_CORES3
    botSounds.play(SFX_BOOT_CHIME);
    #endif

    // Show greeting on first entry
    char buf[MAX_SAY_LEN];
    getRandomSayingText(SAY_GREETING, buf, sizeof(buf));
    botMode.speechBubble.show(buf, 2500);
  } else {
    // Re-entering bot mode: reset to active
    botMode.state = BOT_ACTIVE;
    botMode.lastInteraction = millis();
    botMode.face.transitionTo(EXPR_HAPPY, 300);
    botMode.shakeReacting = true;
    botMode.shakeReactEnd = millis() + 1500;

    char buf[MAX_SAY_LEN];
    getRandomSayingText(SAY_GREETING, buf, sizeof(buf));
    botMode.speechBubble.show(buf, 2000);
  }

  gfx->fillScreen(BOT_COLOR_BG);
}

void exitBotMode() {
  gfx->fillScreen(BOT_COLOR_BG);
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
  // Queue WLED first — Core 0 starts capture + DDP while we wait
  wledQueueText(text, WLED_SAY_PRE_DELAY_MS + durationMs);
  // Schedule LCD bubble to appear after pre-delay (synchronized with WLED)
  botMode.scheduleSaying(text, durationMs);
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
  char buf[MAX_SAY_LEN];
  snprintf(buf, sizeof(buf), "Personality: %s", botMode.personality->name);
  botMode.showNotification(buf, 2000);
}

uint8_t getBotPersonality() {
  return botMode.personalityIndex;
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
inline void setBotBackgroundStyle(uint8_t style) {}
inline uint8_t getBotBackgroundStyle() { return 0; }

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // BOT_MODE_H
