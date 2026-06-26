#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include "config.h"
#include "gfx_mono.h"   // for the shared u8g2 object (icon drawing)

// ============================================================================
// Weather — Open-Meteo (free, no API key, plain HTTP)
// ============================================================================
// Same source/approach as the full vizbot (weather_data.h), trimmed for the C3:
// fetched synchronously when the info screen opens (and at most every
// WEATHER_REFRESH_MS). We only need current temp + condition + today's hi/lo.
// ============================================================================

enum WeatherIcon : uint8_t {
  WX_CLEAR = 0, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_THUNDER
};

struct Weather {
  bool   valid = false;
  int    tempF = 0;
  int    hi = 0, lo = 0;
  uint8_t code = 0;          // WMO code
  char   condition[16] = "";
  unsigned long lastFetchMs = 0;
};
Weather weather;

// ---- WMO code -> icon + text (subset of vizbot's wmoToIcon/wmoToText) ------
inline WeatherIcon wmoIcon(uint8_t c) {
  if (c == 0) return WX_CLEAR;
  if (c <= 2)  return WX_PARTLY;
  if (c == 3)  return WX_CLOUDY;
  if (c == 45 || c == 48) return WX_FOG;
  if (c >= 51 && c <= 57) return WX_DRIZZLE;
  if (c >= 61 && c <= 67) return WX_RAIN;
  if (c >= 71 && c <= 77) return WX_SNOW;
  if (c >= 80 && c <= 82) return WX_RAIN;
  if (c >= 85 && c <= 86) return WX_SNOW;
  if (c >= 95)            return WX_THUNDER;
  return WX_PARTLY;
}
inline const char* wmoText(uint8_t c) {
  if (c == 0) return "Clear";
  if (c <= 2) return "Part Cloud";
  if (c == 3) return "Overcast";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 65) return "Rain";
  if (c == 66 || c == 67) return "Frz Rain";
  if (c >= 71 && c <= 77) return "Snow";
  if (c >= 80 && c <= 82) return "Showers";
  if (c >= 85 && c <= 86) return "Snow Shwr";
  if (c >= 95) return "Storms";
  return "--";
}

// ---- tiny JSON scanners (manual, matches vizbot pattern) -------------------
static bool jsonFloat(const char* json, const char* key, float& out) {
  const char* p = strstr(json, key);
  if (!p) return false;
  p += strlen(key);
  while (*p && (*p == '"' || *p == ':' || *p == ' ')) p++;
  out = atof(p);
  return true;
}
static bool jsonFirstInArray(const char* json, const char* key, float& out) {
  const char* p = strstr(json, key);
  if (!p) return false;
  p = strchr(p, '[');
  if (!p) return false;
  out = atof(p + 1);
  return true;
}

// ---- fetch (blocking, ~<1s) ------------------------------------------------
inline bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  client.setTimeout(5000);
  if (!client.connect("api.open-meteo.com", 80)) {
    Serial.println("[weather] connect failed");
    return false;
  }

  char req[256];
  snprintf(req, sizeof(req),
    "GET /v1/forecast?latitude=%s&longitude=%s"
    "&current=temperature_2m,weather_code"
    "&daily=temperature_2m_max,temperature_2m_min"
    "&temperature_unit=%s&forecast_days=1&timezone=auto HTTP/1.1",
    WEATHER_LAT, WEATHER_LON, WEATHER_UNIT_F ? "fahrenheit" : "celsius");
  client.println(req);
  client.println("Host: api.open-meteo.com");
  client.println("Connection: close");
  client.println();

  unsigned long t0 = millis();
  while (client.available() == 0) {
    if (millis() - t0 > 6000) { Serial.println("[weather] timeout"); client.stop(); return false; }
    delay(10);
  }

  static char resp[1100];
  int n = 0;
  while (client.available() && n < (int)sizeof(resp) - 1) resp[n++] = client.read();
  resp[n] = '\0';
  client.stop();

  const char* body = strstr(resp, "\r\n\r\n");
  body = body ? body + 4 : resp;

  const char* cur = strstr(body, "\"current\"");
  float f;
  if (cur && jsonFloat(cur, "\"temperature_2m\"", f)) weather.tempF = (int)lroundf(f);
  if (cur && jsonFloat(cur, "\"weather_code\"", f)) {
    weather.code = (uint8_t)(f + 0.5f);
    strncpy(weather.condition, wmoText(weather.code), sizeof(weather.condition) - 1);
    weather.condition[sizeof(weather.condition) - 1] = '\0';
  }
  const char* daily = strstr(body, "\"daily\"");
  if (daily && jsonFirstInArray(daily, "\"temperature_2m_max\"", f)) weather.hi = (int)lroundf(f);
  if (daily && jsonFirstInArray(daily, "\"temperature_2m_min\"", f)) weather.lo = (int)lroundf(f);

  weather.lastFetchMs = millis();
  weather.valid = true;
  Serial.printf("[weather] %d%c %s  H%d L%d\n",
                weather.tempF, WEATHER_UNIT_F ? 'F' : 'C', weather.condition,
                weather.hi, weather.lo);
  return true;
}

// Fetch only when due. While we have no valid data, retry every 30s (so a
// transient failure recovers) instead of hammering — fetchWeather() can block
// for several seconds, so we must not call it every frame. Once valid, refresh
// at WEATHER_REFRESH_MS.
inline void refreshWeatherIfStale() {
  static unsigned long lastAttempt = 0;
  bool due = weather.valid ? (millis() - weather.lastFetchMs) > WEATHER_REFRESH_MS
                           : (lastAttempt == 0 || millis() - lastAttempt > 30000);
  if (due) { lastAttempt = millis(); fetchWeather(); }
}

// ============================================================================
// 1-bit weather icons (~16px, centered on cx,cy)
// ============================================================================
inline void wxCloud(int cx, int cy) {
  u8g2.drawDisc(cx - 4, cy + 1, 3);
  u8g2.drawDisc(cx + 4, cy + 1, 3);
  u8g2.drawDisc(cx, cy - 2, 4);
  u8g2.drawBox(cx - 6, cy + 1, 13, 4);
}
inline void wxSun(int cx, int cy) {
  u8g2.drawDisc(cx, cy, 3);
  for (int i = 0; i < 8; i++) {
    float a = i * (PI / 4.0f);
    u8g2.drawLine(cx + lroundf(cosf(a) * 5), cy + lroundf(sinf(a) * 5),
                  cx + lroundf(cosf(a) * 7), cy + lroundf(sinf(a) * 7));
  }
}
inline void drawWeatherIcon(int cx, int cy, uint8_t code) {
  u8g2.setDrawColor(1);
  switch (wmoIcon(code)) {
    case WX_CLEAR:  wxSun(cx, cy); break;
    case WX_PARTLY: wxSun(cx - 3, cy - 3); wxCloud(cx + 1, cy + 2); break;
    case WX_CLOUDY: wxCloud(cx, cy); break;
    case WX_FOG:
      wxCloud(cx, cy - 2);
      for (int i = 0; i < 3; i++) u8g2.drawHLine(cx - 6, cy + 5 + i * 2, 13);
      break;
    case WX_DRIZZLE:
      wxCloud(cx, cy - 2);
      for (int i = -4; i <= 4; i += 4) u8g2.drawPixel(cx + i, cy + 6);
      break;
    case WX_RAIN:
      wxCloud(cx, cy - 2);
      for (int i = -4; i <= 4; i += 4) u8g2.drawLine(cx + i, cy + 5, cx + i - 1, cy + 8);
      break;
    case WX_SNOW:
      wxCloud(cx, cy - 2);
      for (int i = -4; i <= 4; i += 4) { u8g2.drawPixel(cx + i, cy + 6); u8g2.drawPixel(cx + i, cy + 8); }
      break;
    case WX_THUNDER:
      wxCloud(cx, cy - 2);
      u8g2.drawLine(cx, cy + 4, cx - 3, cy + 8);
      u8g2.drawLine(cx - 3, cy + 8, cx + 2, cy + 7);
      break;
  }
}

#endif // WEATHER_H
