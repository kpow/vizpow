#ifndef BOT_OVERLAYS_H
#define BOT_OVERLAYS_H

#include <Arduino.h>
#ifndef TARGET_CORES3
#include <Arduino_GFX_Library.h>
#endif
#include "config.h"
#include "layout.h"

// ============================================================================
// Bot Overlays — Speech Bubbles, Notifications, Time Display
// ============================================================================
// Overlay elements drawn on top of the bot face.
// Speech bubbles pop in, linger, then fade out.
// Notification banners slide in from top.
// ============================================================================

#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

extern Arduino_GFX *gfx;

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
  unsigned long showTime;      // When the bubble appeared
  uint16_t duration;           // How long to show (ms)
  uint8_t animPhase;           // 0=pop-in, 1=visible, 2=fade-out

  // Word-wrap state
  uint8_t numLines;            // 1-3
  char lines[3][20];           // Wrapped line buffers (17 chars + null + margin)

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
    animPhase = 0;
  }

  // Word-wrap text into lines[] for multi-line rendering
  void wrapText() {
    uint8_t maxChars = (OVERLAY_BUBBLE_MAX_W - 24) / 12;  // 17 for 240px LCD
    numLines = 0;

    uint8_t textLen = strlen(text);
    if (textLen <= maxChars) {
      strncpy(lines[0], text, 19); lines[0][19] = '\0';
      numLines = 1;
      return;
    }

    // Greedy word-wrap
    const char* p = text;
    while (*p && numLines < 3) {
      uint8_t lineLen = 0;
      const char* lastSpace = nullptr;
      const char* scan = p;

      while (*scan && lineLen < maxChars) {
        if (*scan == ' ') lastSpace = scan;
        lineLen++; scan++;
      }

      // If remaining text fits, take it all
      if (!*scan) {
        strncpy(lines[numLines], p, 19);
        lines[numLines][19] = '\0';
        numLines++;
        break;
      }

      // Break at last space, or force-break if no space
      uint8_t take = lastSpace ? (uint8_t)(lastSpace - p) : maxChars;
      if (take > 19) take = 19;
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
    showTime = millis();
    duration = durationMs;
    animPhase = 0;

    // Word-wrap into lines
    wrapText();

    // Bubble width based on longest wrapped line
    uint8_t maxLineLen = 0;
    for (uint8_t i = 0; i < numLines; i++) {
      uint8_t len = strlen(lines[i]);
      if (len > maxLineLen) maxLineLen = len;
    }
    bubbleW = maxLineLen * 12 + 24;  // 12px padding each side
    if (bubbleW > OVERLAY_BUBBLE_MAX_W) bubbleW = OVERLAY_BUBBLE_MAX_W;
    bubbleH = numLines * 16 + 14;  // 16px per line + 14px padding
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
      gfx->setTextSize(2);
      gfx->setTextColor(OVERLAY_TEXT);

      int16_t totalTextH = numLines * 16;
      int16_t startY = sy + (sh - totalTextH) / 2;

      for (uint8_t i = 0; i < numLines; i++) {
        uint8_t lineLen = strlen(lines[i]);
        int16_t textW = lineLen * 12;
        int16_t textX = sx + (sw - textW) / 2;  // center each line
        int16_t textY = startY + i * 16;

        // Bold: draw twice with 1px horizontal offset
        gfx->setCursor(textX, textY);
        gfx->print(lines[i]);
        gfx->setCursor(textX + 1, textY);
        gfx->print(lines[i]);
      }
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
