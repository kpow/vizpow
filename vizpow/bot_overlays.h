#ifndef BOT_OVERLAYS_H
#define BOT_OVERLAYS_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "config.h"

// ============================================================================
// Bot Overlays — Speech Bubbles, Notifications, Time Display
// ============================================================================
// Overlay elements drawn on top of the bot face.
// Speech bubbles pop in, linger, then fade out.
// Notification banners slide in from top.
// ============================================================================

#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

extern Arduino_GFX *gfx;

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
  char text[32];               // Current text content
  bool active;                 // Whether bubble is showing
  unsigned long showTime;      // When the bubble appeared
  uint16_t duration;           // How long to show (ms)
  uint8_t animPhase;           // 0=pop-in, 1=visible, 2=fade-out

  // Animation timing
  static const uint16_t POP_IN_MS = 150;
  static const uint16_t FADE_OUT_MS = 200;
  static const uint16_t DEFAULT_DURATION = 3500;  // 3.5 seconds visible

  // Bubble position and size
  int16_t bubbleX, bubbleY, bubbleW, bubbleH;

  void init() {
    active = false;
    text[0] = '\0';
    animPhase = 0;
  }

  // Show a text bubble
  void show(const char* msg, uint16_t durationMs = DEFAULT_DURATION) {
    strncpy(text, msg, 31);
    text[31] = '\0';
    active = true;
    showTime = millis();
    duration = durationMs;
    animPhase = 0;

    // Calculate bubble dimensions based on text
    uint8_t textLen = strlen(text);
    // Text size 1 = 6x8 per char
    bubbleW = textLen * 6 + 16;  // 8px padding each side
    if (bubbleW > 220) bubbleW = 220;  // Max width
    bubbleH = 24;  // Single line + padding
    bubbleX = (LCD_WIDTH - bubbleW) / 2;  // Centered
    bubbleY = 210;  // Below the face, above screen bottom
  }

  // Show from PROGMEM string
  void showP(const char* progmemStr, uint16_t durationMs = DEFAULT_DURATION) {
    char buf[32];
    strncpy_P(buf, progmemStr, 31);
    buf[31] = '\0';
    show(buf, durationMs);
  }

  // Update animation state
  void update() {
    if (!active) return;

    unsigned long elapsed = millis() - showTime;

    if (elapsed < POP_IN_MS) {
      animPhase = 0;  // Pop-in
    } else if (elapsed < POP_IN_MS + duration) {
      animPhase = 1;  // Visible
    } else if (elapsed < POP_IN_MS + duration + FADE_OUT_MS) {
      animPhase = 2;  // Fade-out
    } else {
      active = false;  // Done
    }
  }

  // Render the bubble
  void render() {
    if (!active || gfx == nullptr) return;

    float scale = 1.0f;

    if (animPhase == 0) {
      // Pop-in: scale from 0 to 1
      unsigned long elapsed = millis() - showTime;
      float t = (float)elapsed / POP_IN_MS;
      // Overshoot ease: goes to 1.1 then settles to 1.0
      scale = t * (2.0f - t) * 1.05f;
      if (scale > 1.05f) scale = 1.05f;
    } else if (animPhase == 2) {
      // Fade-out: scale from 1 to 0
      unsigned long elapsed = millis() - showTime - POP_IN_MS - duration;
      float t = (float)elapsed / FADE_OUT_MS;
      scale = 1.0f - t;
      if (scale < 0.0f) scale = 0.0f;
    }

    // Calculate scaled dimensions
    int16_t sw = (int16_t)(bubbleW * scale);
    int16_t sh = (int16_t)(bubbleH * scale);
    int16_t sx = bubbleX + (bubbleW - sw) / 2;
    int16_t sy = bubbleY + (bubbleH - sh) / 2;

    if (sw < 4 || sh < 4) return;

    // Draw bubble background (rounded rect)
    gfx->fillRoundRect(sx, sy, sw, sh, 6, OVERLAY_BG);
    gfx->drawRoundRect(sx, sy, sw, sh, 6, OVERLAY_BORDER);

    // Draw small triangle pointer (pointing up toward face)
    int16_t triCX = sx + sw / 2;
    int16_t triTop = sy - 5;
    gfx->fillTriangle(triCX - 5, sy, triCX + 5, sy, triCX, triTop, OVERLAY_BG);

    // Draw text (only when fully visible or popping in past 50%)
    if (scale > 0.5f) {
      gfx->setTextSize(1);
      gfx->setTextColor(OVERLAY_TEXT);

      // Center text in bubble
      uint8_t textLen = strlen(text);
      int16_t textW = textLen * 6;
      int16_t textX = sx + (sw - textW) / 2;
      int16_t textY = sy + (sh - 8) / 2;

      gfx->setCursor(textX, textY);
      gfx->print(text);
    }
  }
};

// ============================================================================
// Notification Banner
// ============================================================================

struct BotNotification {
  char text[32];
  bool active;
  unsigned long showTime;
  uint16_t duration;
  uint8_t animPhase;           // 0=slide-in, 1=visible, 2=slide-out

  static const uint16_t SLIDE_MS = 200;
  static const uint16_t DEFAULT_DURATION = 2500;

  void init() {
    active = false;
    text[0] = '\0';
  }

  void show(const char* msg, uint16_t durationMs = DEFAULT_DURATION) {
    strncpy(text, msg, 31);
    text[31] = '\0';
    active = true;
    showTime = millis();
    duration = durationMs;
    animPhase = 0;
  }

  void update() {
    if (!active) return;

    unsigned long elapsed = millis() - showTime;
    if (elapsed < SLIDE_MS) {
      animPhase = 0;
    } else if (elapsed < SLIDE_MS + duration) {
      animPhase = 1;
    } else if (elapsed < SLIDE_MS + duration + SLIDE_MS) {
      animPhase = 2;
    } else {
      active = false;
    }
  }

  void render() {
    if (!active || gfx == nullptr) return;

    // Banner: full width, at top of screen
    int16_t bannerH = 24;
    int16_t bannerY = 0;

    if (animPhase == 0) {
      // Slide in from top
      unsigned long elapsed = millis() - showTime;
      float t = (float)elapsed / SLIDE_MS;
      bannerY = (int16_t)(-bannerH + bannerH * t);
    } else if (animPhase == 2) {
      // Slide out to top
      unsigned long elapsed = millis() - showTime - SLIDE_MS - duration;
      float t = (float)elapsed / SLIDE_MS;
      bannerY = (int16_t)(-bannerH * t);
    }

    // Draw banner
    gfx->fillRect(0, bannerY, LCD_WIDTH, bannerH, NOTIFY_BG);

    // Draw text centered
    uint8_t textLen = strlen(text);
    int16_t textW = textLen * 6;
    int16_t textX = (LCD_WIDTH - textW) / 2;
    int16_t textY = bannerY + (bannerH - 8) / 2;

    gfx->setTextSize(1);
    gfx->setTextColor(NOTIFY_TEXT);
    gfx->setCursor(textX, textY);
    gfx->print(text);
  }
};

// ============================================================================
// Time Overlay (corner clock)
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

    // Show uptime in top-right corner (NTP not yet implemented)
    unsigned long uptimeSec = (millis() - uptimeStart) / 1000;
    uint8_t hours = (uptimeSec / 3600) % 24;
    uint8_t minutes = (uptimeSec / 60) % 60;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hours, minutes);

    // Semi-transparent background
    gfx->fillRoundRect(LCD_WIDTH - 46, 2, 44, 16, 4, 0x2104);  // Dark gray

    gfx->setTextSize(1);
    gfx->setTextColor(0x7BEF);  // Dim gray text
    gfx->setCursor(LCD_WIDTH - 42, 6);
    gfx->print(buf);
  }
};

#else

// Stubs when LCD not available
struct BotSpeechBubble {
  bool active;
  void init() { active = false; }
  void show(const char* msg, uint16_t d = 3500) {}
  void showP(const char* p, uint16_t d = 3500) {}
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
