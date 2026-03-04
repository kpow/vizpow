#ifndef BOT_OVERLAYS_H
#define BOT_OVERLAYS_H

#include <Arduino.h>
#include "config.h"
#include "layout.h"
#include "tween.h"
#ifdef TARGET_CORES3
#include "bot_sounds.h"
#endif

// ============================================================================
// Bot Overlays — Speech Bubbles, Notifications, Time Display
// ============================================================================
// Overlay elements drawn on top of the bot face.
// Speech bubbles pop in, linger, then fade out.
// Notification banners slide in from top.
// ============================================================================

#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

extern GfxDevice *gfx;

// Forward declaration for WLED integration
extern void wledQueueText(const char* text, uint16_t durationMs);

// Colors
#define OVERLAY_BG      0xFFFF  // White bubble background
#define OVERLAY_TEXT    0x0000  // Black text
#define OVERLAY_BORDER  0xC618  // Light gray border
#define NOTIFY_BG       0x001F  // Blue notification background
#define NOTIFY_TEXT     0xFFFF  // White notification text

// ============================================================================
// Speech Bubble
// ============================================================================

struct BotSpeechBubble {
  char text[MAX_SAY_LEN];      // Current text content
  bool active;                 // Whether bubble is showing
  unsigned long fadeOutTime;   // When to start fade-out (0 = not scheduled)
  float scale;                 // Tween-driven: 0→1 pop-in, 1→0 fade-out

  // Word-wrap state
  uint8_t numLines;            // 1-4
  char lines[4][28];           // Wrapped line buffers (wider for proportional font)

  // Animation timing
  static const uint16_t POP_IN_MS = 150;
  static const uint16_t FADE_OUT_MS = 200;
  static const uint16_t DEFAULT_DURATION = 5000;  // 5 seconds visible

  // Bubble position and size
  int16_t bubbleX, bubbleY, bubbleW, bubbleH;

  void init() {
    active = false;
    text[0] = '\0';
    numLines = 0;
    scale = 0.0f;
    fadeOutTime = 0;
  }

  // Word-wrap text into lines[] using proportional font width measurement
  void wrapText() {
    int16_t maxLineW = OVERLAY_BUBBLE_MAX_W - 32;  // 16px padding each side
    numLines = 0;

    // Check if text fits on one line
    gfx->setFont(&fonts::DejaVu18);
    gfx->setTextSize(1);
    if (gfx->textWidth(text) <= maxLineW) {
      strncpy(lines[0], text, 27); lines[0][27] = '\0';
      numLines = 1;
      return;
    }

    // Greedy word-wrap using proportional width
    const char* p = text;
    while (*p && numLines < 4) {
      const char* lastSpace = nullptr;
      const char* scan = p;
      char testBuf[28];
      uint8_t bufIdx = 0;

      while (*scan && bufIdx < 27) {
        testBuf[bufIdx] = *scan;
        testBuf[bufIdx + 1] = '\0';
        if (gfx->textWidth(testBuf) > maxLineW) break;
        if (*scan == ' ') lastSpace = scan;
        bufIdx++; scan++;
      }

      // If remaining text fits, take it all
      if (!*scan) {
        uint8_t take = (uint8_t)(scan - p);
        if (take > 27) take = 27;
        memcpy(lines[numLines], p, take);
        lines[numLines][take] = '\0';
        numLines++;
        break;
      }

      // Break at last space, or force-break
      uint8_t take;
      if (lastSpace && lastSpace > p) {
        take = (uint8_t)(lastSpace - p);
      } else {
        take = bufIdx > 0 ? bufIdx : 1;
      }
      if (take > 27) take = 27;
      memcpy(lines[numLines], p, take);
      lines[numLines][take] = '\0';
      numLines++;
      p += take;
      if (*p == ' ') p++;  // skip the space
    }
    if (numLines == 0) numLines = 1;  // safety
  }

  // Show a text bubble.
  // skipWled=true when wledQueueText was already called upstream (e.g. showBotSaying pre-delay path)
  void show(const char* msg, uint16_t durationMs = DEFAULT_DURATION, bool skipWled = false) {
    strncpy(text, msg, MAX_SAY_LEN - 1);
    text[MAX_SAY_LEN - 1] = '\0';
    active = true;
    scale = 0.0f;
    fadeOutTime = millis() + POP_IN_MS + durationMs;

    // Pop-in: scale 0→1 with overshoot easing
    tweenManager.start(&scale, 0.0f, 1.0f, POP_IN_MS, EASE_OUT_BACK);

    // Set proportional font for measurement
    gfx->setFont(&fonts::DejaVu18);
    gfx->setTextSize(1);

    // Word-wrap into lines
    wrapText();

    // Bubble width based on widest wrapped line (proportional measurement)
    int16_t maxLineW = 0;
    for (uint8_t i = 0; i < numLines; i++) {
      int16_t w = gfx->textWidth(lines[i]);
      if (w > maxLineW) maxLineW = w;
    }
    bubbleW = maxLineW + 32;  // 16px padding each side
    if (bubbleW > OVERLAY_BUBBLE_MAX_W) bubbleW = OVERLAY_BUBBLE_MAX_W;
    bubbleH = numLines * 20 + 16;  // 20px per line + 16px padding
    bubbleX = (LCD_WIDTH - bubbleW) / 2;  // Centered
    bubbleY = OVERLAY_BUBBLE_Y;

    // Forward speech text to WLED display (if configured)
    // Skip when already queued upstream (showBotSaying pre-delay path)
    if (!skipWled) {
      wledQueueText(text, durationMs + POP_IN_MS);
    }
  }

  // Show from PROGMEM string
  void showP(const char* progmemStr, uint16_t durationMs = DEFAULT_DURATION) {
    char buf[MAX_SAY_LEN];
    strncpy_P(buf, progmemStr, MAX_SAY_LEN - 1);
    buf[MAX_SAY_LEN - 1] = '\0';
    show(buf, durationMs);
  }

  // Update animation state (tweens drive scale automatically)
  void update() {
    if (!active) return;

    // Start fade-out when duration expires
    if (fadeOutTime > 0 && millis() >= fadeOutTime) {
      tweenManager.start(&scale, scale, 0.0f, FADE_OUT_MS, EASE_IN_QUAD);
      fadeOutTime = 0;  // Only trigger once
    }

    // Deactivate when fully faded out (and fade-out already started)
    if (fadeOutTime == 0 && scale <= 0.01f && !tweenManager.isActive(&scale)) {
      active = false;
    }
  }

  // Render the bubble (scale is driven by tween system)
  void render() {
    if (!active || gfx == nullptr) return;

    float s = scale;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.1f) s = 1.1f;  // Allow slight overshoot from EASE_OUT_BACK

    // ---- Sensor-aware positioning ----
    int16_t renderY = bubbleY;
    int16_t renderX = bubbleX;
    bool flipPointer = false;

    #if defined(TARGET_CORES3) || defined(TARGET_LCD)
    {
      extern float accelX, accelY;
      // Upside-down detection: move bubble to top, flip pointer
      if (accelX < -0.3f) {
        renderY = 8;
        flipPointer = true;
      }
      // Tilt shift: subtle horizontal offset (max ±15px)
      int16_t tiltOffset = (int16_t)constrain(-accelY * 15.0f, -15.0f, 15.0f);
      renderX = bubbleX + tiltOffset;
      // Clamp to screen
      if (renderX < 2) renderX = 2;
      if (renderX + bubbleW > LCD_WIDTH - 2) renderX = LCD_WIDTH - 2 - bubbleW;
    }
    #endif

    // Calculate scaled dimensions
    int16_t sw = (int16_t)(bubbleW * s);
    int16_t sh = (int16_t)(bubbleH * s);
    int16_t sx = renderX + (bubbleW - sw) / 2;
    int16_t sy = renderY + (bubbleH - sh) / 2;

    if (sw < 4 || sh < 4) return;

    // Draw bubble background (rounded rect)
    gfx->fillRoundRect(sx, sy, sw, sh, 10, OVERLAY_BG);
    gfx->drawRoundRect(sx, sy, sw, sh, 10, OVERLAY_BORDER);

    // Draw triangle pointer
    int16_t triCX = sx + sw / 2;
    if (!flipPointer) {
      // Pointing up toward face
      int16_t triTop = sy - 8;
      gfx->fillTriangle(triCX - 6, sy, triCX + 6, sy, triCX, triTop, OVERLAY_BG);
      // Pointer border outline
      gfx->drawLine(triCX - 6, sy, triCX, triTop, OVERLAY_BORDER);
      gfx->drawLine(triCX + 6, sy, triCX, triTop, OVERLAY_BORDER);
    } else {
      // Pointing down (upside-down mode)
      int16_t triBot = sy + sh + 8;
      gfx->fillTriangle(triCX - 6, sy + sh, triCX + 6, sy + sh, triCX, triBot, OVERLAY_BG);
      gfx->drawLine(triCX - 6, sy + sh, triCX, triBot, OVERLAY_BORDER);
      gfx->drawLine(triCX + 6, sy + sh, triCX, triBot, OVERLAY_BORDER);
    }

    // Draw text (only when fully visible or popping in past 50%)
    if (s > 0.5f) {
      gfx->setFont(&fonts::DejaVu18);
      gfx->setTextSize(1);
      gfx->setTextColor(OVERLAY_TEXT);

      int16_t totalTextH = numLines * 20;
      int16_t startY = sy + (sh - totalTextH) / 2;

      for (uint8_t i = 0; i < numLines; i++) {
        int16_t textW = gfx->textWidth(lines[i]);
        int16_t textX = sx + (sw - textW) / 2;  // center each line
        int16_t textY = startY + i * 20;

        // Bold: draw twice with 1px horizontal offset
        gfx->setCursor(textX, textY);
        gfx->print(lines[i]);
        gfx->setCursor(textX + 1, textY);
        gfx->print(lines[i]);
      }

      // Reset font so notification/time overlays use default sizing
      gfx->setFont(&fonts::Font0);
      gfx->setTextSize(1);
    }
  }
};

// ============================================================================
// Notification Banner
// ============================================================================

struct BotNotification {
  char text[32];
  bool active;
  unsigned long slideOutTime;   // When to start slide-out (0 = not scheduled)
  float slideY;                 // Tween-driven: banner Y offset (0 = visible, -24 = hidden)

  static const uint16_t SLIDE_MS = 200;
  static const uint16_t DEFAULT_DURATION = 2500;
  static const int16_t BANNER_H = 24;

  void init() {
    active = false;
    text[0] = '\0';
    slideY = (float)-BANNER_H;
  }

  void show(const char* msg, uint16_t durationMs = DEFAULT_DURATION) {
    strncpy(text, msg, 31);
    text[31] = '\0';
    active = true;
    slideY = (float)-BANNER_H;
    slideOutTime = millis() + SLIDE_MS + durationMs;

    // Slide in: Y from -24 (hidden) to 0 (visible)
    tweenManager.start(&slideY, (float)-BANNER_H, 0.0f, SLIDE_MS, EASE_OUT_QUAD);
  }

  void update() {
    if (!active) return;

    // Start slide-out when duration expires
    if (slideOutTime > 0 && millis() >= slideOutTime) {
      tweenManager.start(&slideY, slideY, (float)-BANNER_H, SLIDE_MS, EASE_IN_QUAD);
      slideOutTime = 0;
    }

    // Deactivate when fully slid out
    if (slideOutTime == 0 && slideY <= (float)(-BANNER_H + 1) && !tweenManager.isActive(&slideY)) {
      active = false;
    }
  }

  void render() {
    if (!active || gfx == nullptr) return;

    int16_t bannerY = (int16_t)slideY;

    // Draw banner
    gfx->fillRect(0, bannerY, LCD_WIDTH, BANNER_H, NOTIFY_BG);

    // Draw text centered
    uint8_t textLen = strlen(text);
    int16_t textW = textLen * 6;
    int16_t textX = (LCD_WIDTH - textW) / 2;
    int16_t textY = bannerY + (BANNER_H - 8) / 2;

    gfx->setTextSize(1);
    gfx->setTextColor(NOTIFY_TEXT);
    gfx->setCursor(textX, textY);
    gfx->print(text);
  }
};

// ============================================================================
// Time Overlay (centered, compact clock)
// ============================================================================

struct BotTimeOverlay {
  bool enabled;
  bool ntpSynced;
  unsigned long uptimeStart;

  void init() {
    enabled = false;
    ntpSynced = false;
    uptimeStart = millis();
  }

  void render() {
    if (!enabled || gfx == nullptr) return;

    uint8_t hours, minutes;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
      hours = timeinfo.tm_hour;
      minutes = timeinfo.tm_min;
      ntpSynced = true;
    } else {
      // Fallback to uptime if NTP hasn't synced
      unsigned long uptimeSec = (millis() - uptimeStart) / 1000;
      hours = (uptimeSec / 3600) % 24;
      minutes = (uptimeSec / 60) % 60;
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hours, minutes);

    // Compact centered time — text size 2 = 12x16 per char, "00:00" = 60px wide
    int16_t pillW = 72;   // 60px text + 12px padding
    int16_t pillH = 24;   // 16px text + 8px padding
    int16_t pillX = (LCD_WIDTH - pillW) / 2;   // Centered horizontally
    int16_t pillY = 4;                          // Near top of screen

    gfx->fillRoundRect(pillX, pillY, pillW, pillH, 6, 0x2104);  // Dark gray bg

    gfx->setTextSize(2);
    gfx->setTextColor(0x07FF);  // Cyan text
    gfx->setCursor(pillX + 6, pillY + 4);
    gfx->print(buf);
  }
};

#else

// Stubs when LCD not available
struct BotSpeechBubble {
  bool active;
  void init() { active = false; }
  void show(const char* msg, uint16_t d = 5000, bool skipWled = false) {}
  void showP(const char* p, uint16_t d = 5000) {}
  void update() {}
  void render() {}
};

struct BotNotification {
  bool active;
  void init() { active = false; }
  void show(const char* msg, uint16_t d = 2500) {}
  void update() {}
  void render() {}
};

struct BotTimeOverlay {
  bool enabled;
  void init() { enabled = false; }
  void render() {}
};

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // BOT_OVERLAYS_H
