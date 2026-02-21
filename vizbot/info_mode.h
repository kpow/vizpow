#ifndef INFO_MODE_H
#define INFO_MODE_H

#include <Arduino.h>
#ifndef TARGET_CORES3
#include <Arduino_GFX_Library.h>
#endif
#include "config.h"
#include "layout.h"
#include "bot_faces.h"
#include "bot_eyes.h"
#include "weather_data.h"
#include "weather_icons.h"

// ============================================================================
// Info Mode — Multi-page information display with mini bot eyes
// ============================================================================
// Triggered by sustained shake (~2s). Bot face shrinks into mini eyes in the
// top-right corner while weather/info content fills the screen.
// Shake again to exit (mini eyes expand back to full bot face).
//
// Architecture: weather is page 0. Tap to cycle future pages.
// Uses the same double-buffered canvas as bot mode for flicker-free rendering.
// ============================================================================

#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

// External references
extern Arduino_GFX *gfx;
extern Arduino_Canvas *botCanvas;
extern Arduino_GFX *gfxReal;
extern BotModeState botMode;
extern uint16_t botFaceColor;
extern bool menuVisible;

// ============================================================================
// Constants
// ============================================================================

// Mini eye position — MINI_EYE_CX and MINI_EYE_CY come from layout.h (derived from LCD_WIDTH)
#define MINI_EYE_W        18      // White ellipse half-width
#define MINI_EYE_H        15      // White ellipse half-height
#define MINI_EYE_SPACING  21      // Distance from center to each eye
#define MINI_PUPIL_R      6       // Pupil radius

// Transition timing
#define INFO_TRANSITION_MS     600   // Shrink/expand animation duration
#define INFO_PRE_TRANSITION_MS 500   // Time for thinking expression before shrink

// ============================================================================
// Page System
// ============================================================================

enum InfoPage : uint8_t {
  INFO_PAGE_WEATHER = 0,
  INFO_PAGE_COUNT              // Add future pages before this
};

// ============================================================================
// Info Mode States
// ============================================================================

enum InfoState : uint8_t {
  INFO_INACTIVE = 0,           // Normal bot mode
  INFO_PRE_ENTER,              // Bot showing thinking expression + saying
  INFO_ENTERING,               // Face shrinking to corner
  INFO_ACTIVE,                 // Info page displayed with mini eyes
  INFO_EXITING,                // Mini eyes expanding back to face
};

// ============================================================================
// Mini Eye Animation (simplified blink + look-around)
// ============================================================================

struct MiniEyeState {
  // Blink
  float blinkAmount;           // 0.0 = open, 1.0 = closed
  unsigned long nextBlinkMs;
  bool blinking;
  unsigned long blinkStartMs;
  static const uint16_t BLINK_DURATION_MS = 120;

  // Look-around
  float pupilX, pupilY;
  float targetPupilX, targetPupilY;
  unsigned long nextLookMs;

  void init() {
    blinkAmount = 0.0f;
    nextBlinkMs = millis() + random(2000, 5000);
    blinking = false;
    pupilX = pupilY = 0;
    targetPupilX = targetPupilY = 0;
    nextLookMs = millis() + random(1500, 3000);
  }

  void update() {
    unsigned long now = millis();

    // Blink
    if (!blinking && now >= nextBlinkMs) {
      blinking = true;
      blinkStartMs = now;
    }
    if (blinking) {
      float elapsed = (float)(now - blinkStartMs);
      float t = elapsed / BLINK_DURATION_MS;
      if (t >= 1.0f) {
        blinking = false;
        blinkAmount = 0.0f;
        nextBlinkMs = now + random(3000, 6000);
      } else {
        // Triangle wave: close then open
        blinkAmount = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
      }
    }

    // Look-around
    if (now >= nextLookMs) {
      targetPupilX = random(-30, 31) / 10.0f;  // -3.0 to 3.0
      targetPupilY = random(-20, 21) / 10.0f;  // -2.0 to 2.0
      nextLookMs = now + random(2000, 4000);
    }
    // Smooth interpolation
    pupilX += (targetPupilX - pupilX) * 0.15f;
    pupilY += (targetPupilY - pupilY) * 0.15f;
  }
};

// ============================================================================
// Info Mode State
// ============================================================================

struct InfoModeData {
  InfoState state;
  InfoPage currentPage;

  // Transition animation
  unsigned long transitionStartMs;

  // Mini eyes
  MiniEyeState miniEyes;

  // Weather refresh timer
  unsigned long lastWeatherRequestMs;

  bool active;                 // Shortcut: state != INFO_INACTIVE

  void init() {
    state = INFO_INACTIVE;
    currentPage = INFO_PAGE_WEATHER;
    active = false;
    miniEyes.init();
    lastWeatherRequestMs = 0;
  }

  // Begin enter transition (called when sustained shake detected)
  void beginEnterTransition() {
    if (state != INFO_INACTIVE) return;

    state = INFO_PRE_ENTER;
    active = true;
    transitionStartMs = millis();

    // Show thinking expression + info saying on the bot face
    botMode.face.transitionTo(EXPR_THINKING, 150);
    char buf[32];
    getRandomSayingText(SAY_INFO_ENTER, buf, sizeof(buf));
    botMode.speechBubble.show(buf, INFO_PRE_TRANSITION_MS);

    // Request weather data fetch
    requestWeatherFetch();
    lastWeatherRequestMs = millis();

    miniEyes.init();
  }

  // Begin exit transition (called when sustained shake detected while active)
  void beginExitTransition() {
    if (state != INFO_ACTIVE) return;

    state = INFO_EXITING;
    transitionStartMs = millis();
  }

  // Cycle to next info page
  void nextPage() {
    if (state != INFO_ACTIVE) return;
    currentPage = (InfoPage)((currentPage + 1) % INFO_PAGE_COUNT);
  }

  // Update state machine
  void update() {
    unsigned long now = millis();

    switch (state) {
      case INFO_PRE_ENTER:
        // Wait for thinking expression to show, then start shrinking
        if (now - transitionStartMs >= INFO_PRE_TRANSITION_MS) {
          state = INFO_ENTERING;
          transitionStartMs = now;
          // Clear speech bubble before transition
          botMode.speechBubble.active = false;
        }
        // Keep updating bot mode during pre-enter
        break;

      case INFO_ENTERING: {
        float elapsed = (float)(now - transitionStartMs);
        if (elapsed >= INFO_TRANSITION_MS) {
          state = INFO_ACTIVE;
        }
        break;
      }

      case INFO_ACTIVE:
        miniEyes.update();
        // Auto-refresh weather
        if (now - lastWeatherRequestMs >= WEATHER_REFRESH_MS) {
          requestWeatherFetch();
          lastWeatherRequestMs = now;
        }
        break;

      case INFO_EXITING: {
        float elapsed = (float)(now - transitionStartMs);
        if (elapsed >= INFO_TRANSITION_MS) {
          state = INFO_INACTIVE;
          active = false;
          // Restore bot to neutral
          botMode.face.transitionTo(EXPR_NEUTRAL, 300);
          botMode.registerInteraction();
        }
        break;
      }

      default:
        break;
    }
  }
};

// Global info mode state
InfoModeData infoMode;

// ============================================================================
// Smoothstep easing (same as BotLookAround)
// ============================================================================

static float smoothstep(float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// ============================================================================
// Render Mini Eyes
// ============================================================================

void renderMiniEyes(float blinkAmount, float pupilX, float pupilY) {
  int16_t leftCX = MINI_EYE_CX - MINI_EYE_SPACING;
  int16_t rightCX = MINI_EYE_CX + MINI_EYE_SPACING;

  int16_t effectiveH = (int16_t)(MINI_EYE_H * (1.0f - blinkAmount));
  if (effectiveH < 1) effectiveH = 1;

  // White ellipses
  gfx->fillEllipse(leftCX, MINI_EYE_CY, MINI_EYE_W, effectiveH, botFaceColor);
  gfx->fillEllipse(rightCX, MINI_EYE_CY, MINI_EYE_W, effectiveH, botFaceColor);

  // Pupils (only if eyes are open enough)
  if (effectiveH > 3) {
    int16_t px = constrain((int16_t)pupilX, -4, 4);
    int16_t py = constrain((int16_t)pupilY, -3, 3);
    gfx->fillCircle(leftCX + px, MINI_EYE_CY + py, MINI_PUPIL_R, BOT_COLOR_PUPIL);
    gfx->fillCircle(rightCX + px, MINI_EYE_CY + py, MINI_PUPIL_R, BOT_COLOR_PUPIL);
  }
}

// ============================================================================
// Render Page Dots
// ============================================================================

void renderPageDots(uint8_t currentPage, uint8_t totalPages) {
  int16_t dotY = 12;
  int16_t startX = 10;
  for (uint8_t i = 0; i < totalPages; i++) {
    uint16_t color = (i == currentPage) ? 0xFFFF : 0x4208;  // White or dark gray
    gfx->fillCircle(startX + i * 12, dotY, 3, color);
  }
}

// ============================================================================
// Render Current Weather
// ============================================================================

void renderCurrentWeather() {
  if (!weatherData.valid) {
    // Show loading/error state
    gfx->setTextSize(2);
    gfx->setTextColor(0xC618);  // Light gray
    if (weatherData.fetching) {
      gfx->setCursor(INFO_LOADING_X, INFO_LOADING_Y);
      gfx->print("Fetching...");
    } else if (!sysStatus.staConnected) {
      gfx->setCursor(INFO_LOADING_X, INFO_LOADING_Y - 10);
      gfx->print("No WiFi");
      gfx->setTextSize(1);
      gfx->setCursor(INFO_LOADING_X - 20, INFO_LOADING_Y + 20);
      gfx->print("Connect to a network");
      gfx->setCursor(INFO_LOADING_X - 15, INFO_LOADING_Y + 32);
      gfx->print("via the web UI");
    } else {
      gfx->setCursor(INFO_LOADING_X, INFO_LOADING_Y);
      gfx->print(weatherData.errorMsg);
    }
    return;
  }

  // Weather icon (under the mini eyes, right side)
  uint8_t iconType = wmoToIcon(weatherData.current.weatherCode);
  drawWeatherIcon(MINI_EYE_CX, INFO_ICON_Y, 44, iconType);

  // Temperature (big, centered in the left space)
  int tempInt = (int)(weatherData.current.tempF + 0.5f);
  char tempBuf[8];
  snprintf(tempBuf, sizeof(tempBuf), "%d", tempInt);

  uint16_t tempColor = getTempColor(tempInt);
  gfx->setTextSize(7);  // ~42x56 per char
  gfx->setTextColor(tempColor);
  int16_t tempTextW = strlen(tempBuf) * 42;
  // Degree symbol width
  int16_t degreeW = 18;  // textSize 3 "o" = ~18px
  int16_t totalW = tempTextW + degreeW;
  // Center in the space left of the icon (0 to ~165)
  int16_t leftSpace = MINI_EYE_CX - 44 - 10;  // icon left edge minus margin
  int16_t tempX = (leftSpace - totalW) / 2;
  if (tempX < 4) tempX = 4;
  gfx->setCursor(tempX, INFO_TEMP_Y);
  gfx->print(tempBuf);

  // Degree symbol (after temp number, no F)
  int16_t afterTempX = tempX + tempTextW;
  gfx->setTextSize(3);
  gfx->setTextColor(0xC618);
  gfx->setCursor(afterTempX + 2, 40);
  gfx->print("o");

  // Condition text
  gfx->setTextSize(2);
  gfx->setTextColor(0xFFFF);
  int16_t textW = strlen(weatherData.current.conditionText) * 12;
  int16_t textX = (LCD_WIDTH - textW) / 2;
  gfx->setCursor(textX, INFO_CONDITION_Y);
  gfx->print(weatherData.current.conditionText);
}

// ============================================================================
// Render 3-Day Forecast Bar Graph
// ============================================================================

void renderForecastBars() {
  if (!weatherData.valid) return;

  // Divider line between current conditions and forecast
  gfx->drawLine(20, INFO_DIVIDER_Y, LCD_WIDTH - 20, INFO_DIVIDER_Y, 0x4208);
  gfx->drawLine(20, INFO_DIVIDER_Y + 1, LCD_WIDTH - 20, INFO_DIVIDER_Y + 1, 0x4208);

  // Find global min/max across all 3 days for bar scaling
  float globalMin = 200, globalMax = -200;
  for (int i = 0; i < 3; i++) {
    if (weatherData.forecast[i].lowF < globalMin) globalMin = weatherData.forecast[i].lowF;
    if (weatherData.forecast[i].highF > globalMax) globalMax = weatherData.forecast[i].highF;
  }
  float range = globalMax - globalMin;
  if (range < 10) range = 10;  // Minimum range to avoid squished bars

  // Bars grow upward from the very bottom of the screen
  int16_t barBot = LCD_HEIGHT;
  int16_t barMaxH = FORECAST_BAR_MAX_H;
  int16_t barW = FORECAST_BAR_W;
  int16_t dayLabelY = LCD_HEIGHT - 18;

  const int16_t forecastCols[3] = { FORECAST_COL_0, FORECAST_COL_1, FORECAST_COL_2 };
  for (int i = 0; i < 3; i++) {
    int16_t colCX = forecastCols[i];

    // Bar height based on high temp (uneven tops across the 3 bars)
    float highNorm = (weatherData.forecast[i].highF - globalMin) / range;
    int16_t barH = 30 + (int16_t)(highNorm * (barMaxH - 30));  // Min 30px, max barMaxH
    int16_t barTop = barBot - barH;

    // Bar color based on high temp
    uint16_t barColor = getTempColor((int)(weatherData.forecast[i].highF + 0.5f));

    // Draw bar (flat bottom at screen edge, rounded top corners only)
    gfx->fillRect(colCX - barW / 2, barTop + 4, barW, barH - 4, barColor);
    gfx->fillRoundRect(colCX - barW / 2, barTop, barW, 12, 4, barColor);

    // High/Low temps above bar
    char buf[12];
    snprintf(buf, sizeof(buf), "%d/%d",
             (int)(weatherData.forecast[i].highF + 0.5f),
             (int)(weatherData.forecast[i].lowF + 0.5f));
    gfx->setTextSize(1);
    gfx->setTextColor(0xFFFF);
    int16_t labelW = strlen(buf) * 6;
    gfx->setCursor(colCX - labelW / 2, barTop - 14);
    gfx->print(buf);

    // Small weather icon above temps
    uint8_t dayIcon = wmoToIcon(weatherData.forecast[i].weatherCode);
    drawWeatherIcon(colCX, barTop - 30, 14, dayIcon);

    // Day name inside bar at the bottom
    gfx->setTextSize(2);
    gfx->setTextColor(0x0000);  // Black text on colored bar
    int16_t dayTextW = strlen(weatherData.forecast[i].dayName) * 12;
    gfx->setCursor(colCX - dayTextW / 2, dayLabelY);
    gfx->print(weatherData.forecast[i].dayName);
  }
}

// ============================================================================
// Render Weather Page (complete)
// ============================================================================

void renderWeatherPage() {
  renderCurrentWeather();
  renderForecastBars();
}

// ============================================================================
// Render Transition Animation
// ============================================================================
// During enter: face center moves (BOT_FACE_CX, BOT_FACE_CY) → (MINI_EYE_CX, MINI_EYE_CY),
//              scale 1.0 → 0.3. During exit: reverse.
// ============================================================================

void renderInfoTransition(float t, bool entering) {
  float ease = entering ? smoothstep(t) : smoothstep(1.0f - t);

  // Interpolate face center position
  int16_t cx = BOT_FACE_CX + (int16_t)((MINI_EYE_CX - BOT_FACE_CX) * ease);
  int16_t cy = BOT_FACE_CY + (int16_t)((MINI_EYE_CY - BOT_FACE_CY) * ease);

  // Scale: 1.0 → 0.3
  float scale = 1.0f - ease * 0.7f;

  // Scale eye parameters from current face state
  int16_t eyeW = (int16_t)(botMode.face.eyeWhiteW * scale);
  int16_t eyeH = (int16_t)(botMode.face.eyeWhiteH * scale);
  int16_t spacing = (int16_t)(botMode.face.eyeSpacing * scale);
  int16_t pupilR = max((int16_t)2, (int16_t)(botMode.face.pupilRadius * scale));

  // Apply blink
  float blinkAmount = botMode.face.blinkAmount;
  int16_t effectiveH = (int16_t)(eyeH * (1.0f - blinkAmount));
  if (effectiveH < 1) effectiveH = 1;

  // Draw eye whites
  int16_t leftCX = cx - spacing;
  int16_t rightCX = cx + spacing;

  gfx->fillEllipse(leftCX, cy, eyeW, effectiveH, botFaceColor);
  gfx->fillEllipse(rightCX, cy, eyeW, effectiveH, botFaceColor);

  // Draw pupils
  if (effectiveH > 3 && pupilR >= 2) {
    gfx->fillCircle(leftCX, cy, pupilR, BOT_COLOR_PUPIL);
    gfx->fillCircle(rightCX, cy, pupilR, BOT_COLOR_PUPIL);
  }

  // Brows (fade out early in enter, fade in late in exit)
  if (ease < 0.4f && botMode.face.browVisible && scale > 0.5f) {
    int16_t browLen = (int16_t)(botMode.face.browLength * scale);
    int16_t browThick = max((int16_t)1, (int16_t)(botMode.face.browThickness * scale));
    int16_t browY = cy - effectiveH - (int16_t)(botMode.face.browOffsetY * scale);

    float browAngleL = botMode.face.browAngleL * DEG_TO_RAD;
    float browAngleR = botMode.face.browAngleR * DEG_TO_RAD;

    // Left brow
    int16_t lbx0 = leftCX - browLen;
    int16_t lby0 = browY + (int16_t)(sinf(browAngleL) * browLen);
    int16_t lbx1 = leftCX + browLen;
    int16_t lby1 = browY - (int16_t)(sinf(browAngleL) * browLen);
    drawThickLine(lbx0, lby0, lbx1, lby1, browThick, botFaceColor);

    // Right brow
    int16_t rbx0 = rightCX - browLen;
    int16_t rby0 = browY - (int16_t)(sinf(browAngleR) * browLen);
    int16_t rbx1 = rightCX + browLen;
    int16_t rby1 = browY + (int16_t)(sinf(browAngleR) * browLen);
    drawThickLine(rbx0, rby0, rbx1, rby1, browThick, botFaceColor);
  }

  // Mouth (fade out very early in enter)
  if (ease < 0.3f && scale > 0.6f) {
    int16_t mouthCY = cy + (int16_t)(botMode.face.mouthOffsetY * scale);
    int16_t mouthW = (int16_t)(botMode.face.mouthWidth * scale);

    if (botMode.face.mouthType == MOUTH_SMILE || botMode.face.mouthType == MOUTH_GRIN) {
      // Simple curved line
      int16_t curveH = (int16_t)(botMode.face.mouthCurve * scale);
      for (int16_t x = -mouthW; x <= mouthW; x += 2) {
        float normalized = (float)x / mouthW;
        int16_t y = (int16_t)(curveH * normalized * normalized);
        gfx->fillCircle(cx + x, mouthCY + y, 1, botFaceColor);
      }
    } else if (botMode.face.mouthType == MOUTH_LINE) {
      drawThickLine(cx - mouthW, mouthCY, cx + mouthW, mouthCY, 2, botFaceColor);
    }
  }

  // Weather content fades in during second half of enter (or fades out first half of exit)
  if (ease > 0.5f) {
    renderWeatherPage();
  }
}

// ============================================================================
// Main Info Mode Render
// ============================================================================

void renderInfoMode() {
  if (gfx == nullptr) return;
  if (menuVisible) return;

  // Initialize canvas / begin double-buffered frame
  #ifndef TARGET_CORES3
  if (botCanvas == nullptr) {
    gfxReal = gfx;
    botCanvas = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, gfxReal);
    botCanvas->begin();
  }
  gfx = botCanvas;
  #else
  gfx->beginCanvas();
  #endif

  // Clear canvas
  gfx->fillScreen(BOT_COLOR_BG);

  switch (infoMode.state) {
    case INFO_PRE_ENTER:
      // Still showing bot face — update and render bot mode on canvas
      botMode.speechBubble.update();
      botMode.face.update();
      botMode.face.blinkAmount = botMode.blink.update();
      prevFrame.invalidate();
      renderBotFace(botMode.face, BOT_COLOR_BG);
      botMode.speechBubble.render();
      break;

    case INFO_ENTERING: {
      float elapsed = (float)(millis() - infoMode.transitionStartMs);
      float t = elapsed / INFO_TRANSITION_MS;
      if (t > 1.0f) t = 1.0f;
      renderInfoTransition(t, true);
      break;
    }

    case INFO_ACTIVE:
      // Render weather content
      renderWeatherPage();
      // Render mini eyes on top
      renderMiniEyes(infoMode.miniEyes.blinkAmount,
                     infoMode.miniEyes.pupilX,
                     infoMode.miniEyes.pupilY);
      // Page dots
      renderPageDots(infoMode.currentPage, INFO_PAGE_COUNT);
      break;

    case INFO_EXITING: {
      float elapsed = (float)(millis() - infoMode.transitionStartMs);
      float t = elapsed / INFO_TRANSITION_MS;
      if (t > 1.0f) t = 1.0f;
      renderInfoTransition(t, false);
      break;
    }

    default:
      break;
  }

  // Flush canvas to screen atomically
  #ifndef TARGET_CORES3
  botCanvas->flush();
  gfx = gfxReal;
  #else
  gfx->flushCanvas();
  #endif
}

// ============================================================================
// Combined update + render (called from main loop)
// ============================================================================

void runInfoMode() {
  infoMode.update();
  renderInfoMode();
}

#else

// Stubs when LCD not available
struct InfoModeData {
  bool active;
  void init() { active = false; }
  void beginEnterTransition() {}
  void beginExitTransition() {}
  void nextPage() {}
  void update() {}
};
InfoModeData infoMode;
void runInfoMode() {}

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // INFO_MODE_H
