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
    // Text size 2 = 12x16 per char
    bubbleW = textLen * 12 + 20;  // 10px padding each side
    if (bubbleW > 234) bubbleW = 234;  // Max width (near full screen)
    bubbleH = 36;  // 16px text + 20px padding
    bubbleX = (LCD_WIDTH - bubbleW) / 2;  // Centered
    bubbleY = 220;  // Below the face
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
      gfx->setTextSize(2);
      gfx->setTextColor(OVERLAY_TEXT);

      // Center text in bubble (text size 2 = 12x16 per char)
      uint8_t textLen = strlen(text);
      int16_t textW = textLen * 12;
      int16_t textX = sx + (sw - textW) / 2;
      int16_t textY = sy + (sh - 16) / 2;

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

    // Large time display — text size 3 = 18x24 per char, "00:00" = 90px wide
    gfx->fillRoundRect(LCD_WIDTH - 100, 2, 96, 30, 6, 0x2104);  // Dark gray bg

    gfx->setTextSize(3);
    gfx->setTextColor(0x07FF);  // Cyan text
    gfx->setCursor(LCD_WIDTH - 96, 5);
    gfx->print(buf);
  }
};

// ============================================================================
// Weather Overlay
// ============================================================================
// Fetches weather from Open-Meteo API (free, no key needed).
// Displays temperature and procedural weather icon in top-left corner.
// Only works in STA (station) mode with internet access.
// ============================================================================

#include <HTTPClient.h>

// Weather condition codes (WMO)
enum WeatherIcon : uint8_t {
  WEATHER_CLEAR = 0,
  WEATHER_CLOUDY,
  WEATHER_RAIN,
  WEATHER_SNOW,
  WEATHER_STORM,
  WEATHER_UNKNOWN
};

struct BotWeatherOverlay {
  bool enabled;
  float temperature;              // Current temp in Celsius
  WeatherIcon icon;
  unsigned long lastFetchTime;
  unsigned long fetchIntervalMs;  // Default 30 minutes
  float latitude;
  float longitude;
  bool hasLocation;
  bool fetching;

  void init() {
    enabled = false;
    temperature = 0;
    icon = WEATHER_UNKNOWN;
    lastFetchTime = 0;
    fetchIntervalMs = 1800000;  // 30 minutes
    latitude = 0;
    longitude = 0;
    hasLocation = false;
    fetching = false;
  }

  void setLocation(float lat, float lon) {
    latitude = lat;
    longitude = lon;
    hasLocation = true;
    lastFetchTime = 0;  // Force immediate fetch
  }

  // Map WMO weather code to icon
  WeatherIcon wmoToIcon(int code) {
    if (code <= 1) return WEATHER_CLEAR;
    if (code <= 3) return WEATHER_CLOUDY;
    if (code <= 67 || (code >= 80 && code <= 82)) return WEATHER_RAIN;
    if (code <= 77 || (code >= 85 && code <= 86)) return WEATHER_SNOW;
    if (code >= 95) return WEATHER_STORM;
    return WEATHER_CLOUDY;
  }

  // Non-blocking fetch (called from update)
  void fetchWeather() {
    if (!hasLocation || fetching) return;

    // Check if WiFi is in STA mode with internet
    if (WiFi.status() != WL_CONNECTED) return;

    fetching = true;
    HTTPClient http;

    char url[128];
    snprintf(url, sizeof(url),
      "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
      latitude, longitude);

    http.begin(url);
    http.setTimeout(5000);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();

      // Simple JSON parsing (avoid full JSON library for memory)
      int tempIdx = payload.indexOf("\"temperature_2m\":");
      if (tempIdx > 0) {
        int start = tempIdx + 17;
        temperature = payload.substring(start).toFloat();
      }

      int codeIdx = payload.indexOf("\"weather_code\":");
      if (codeIdx > 0) {
        int start = codeIdx + 15;
        int wmoCode = payload.substring(start).toInt();
        icon = wmoToIcon(wmoCode);
      }
    }

    http.end();
    fetching = false;
    lastFetchTime = millis();
  }

  void update() {
    if (!enabled || !hasLocation) return;

    // Periodic fetch
    if (lastFetchTime == 0 || (millis() - lastFetchTime > fetchIntervalMs)) {
      fetchWeather();
    }
  }

  // Draw procedural weather icon
  void drawWeatherIcon(int16_t cx, int16_t cy, int16_t size) {
    switch (icon) {
      case WEATHER_CLEAR:
        // Sun: filled circle with rays
        gfx->fillCircle(cx, cy, size / 3, 0xFFE0);  // Yellow
        for (int i = 0; i < 8; i++) {
          float angle = i * PI / 4;
          int16_t x1 = cx + (int16_t)(cosf(angle) * size * 0.45f);
          int16_t y1 = cy + (int16_t)(sinf(angle) * size * 0.45f);
          int16_t x2 = cx + (int16_t)(cosf(angle) * size * 0.7f);
          int16_t y2 = cy + (int16_t)(sinf(angle) * size * 0.7f);
          gfx->drawLine(x1, y1, x2, y2, 0xFFE0);
        }
        break;

      case WEATHER_CLOUDY:
        // Cloud: overlapping circles
        gfx->fillCircle(cx - 4, cy, size / 3, 0xC618);
        gfx->fillCircle(cx + 4, cy - 2, size / 4, 0xC618);
        gfx->fillCircle(cx + 2, cy + 2, size / 4, 0xC618);
        break;

      case WEATHER_RAIN:
        // Cloud + rain drops
        gfx->fillCircle(cx - 3, cy - 3, size / 4, 0xC618);
        gfx->fillCircle(cx + 3, cy - 3, size / 5, 0xC618);
        // Rain drops
        for (int i = 0; i < 3; i++) {
          int16_t dx = cx - 4 + i * 4;
          gfx->drawLine(dx, cy + 2, dx - 1, cy + size / 2, 0x001F);
        }
        break;

      case WEATHER_SNOW:
        // Snowflake: asterisk pattern
        for (int i = 0; i < 3; i++) {
          float angle = i * PI / 3;
          int16_t x1 = cx + (int16_t)(cosf(angle) * size / 3);
          int16_t y1 = cy + (int16_t)(sinf(angle) * size / 3);
          int16_t x2 = cx - (int16_t)(cosf(angle) * size / 3);
          int16_t y2 = cy - (int16_t)(sinf(angle) * size / 3);
          gfx->drawLine(x1, y1, x2, y2, 0xFFFF);
        }
        break;

      case WEATHER_STORM:
        // Cloud + lightning bolt
        gfx->fillCircle(cx - 3, cy - 4, size / 4, 0x7BEF);
        gfx->fillCircle(cx + 3, cy - 4, size / 5, 0x7BEF);
        // Lightning bolt
        gfx->fillTriangle(cx, cy - 1, cx - 3, cy + 3, cx + 1, cy + 2, 0xFFE0);
        gfx->fillTriangle(cx - 1, cy + 2, cx + 3, cy - 1, cx, cy + size / 2, 0xFFE0);
        break;

      default:
        // Question mark
        gfx->setTextSize(1);
        gfx->setTextColor(0x7BEF);
        gfx->setCursor(cx - 3, cy - 4);
        gfx->print("?");
        break;
    }
  }

  void render() {
    if (!enabled || !hasLocation || gfx == nullptr) return;
    if (icon == WEATHER_UNKNOWN && lastFetchTime == 0) return;

    // Weather display in top-left corner
    gfx->fillRoundRect(2, 2, 56, 20, 4, 0x2104);  // Dark gray bg

    // Temperature
    char buf[8];
    snprintf(buf, sizeof(buf), "%d°", (int)temperature);
    gfx->setTextSize(1);
    gfx->setTextColor(0xFFFF);
    gfx->setCursor(6, 8);
    gfx->print(buf);

    // Weather icon
    drawWeatherIcon(46, 12, 14);
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

struct BotWeatherOverlay {
  bool enabled;
  bool hasLocation;
  void init() { enabled = false; hasLocation = false; }
  void setLocation(float lat, float lon) {}
  void update() {}
  void render() {}
};

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // BOT_OVERLAYS_H
