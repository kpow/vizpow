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
// Same source/approach as the full vizbot (weather_data.h), trimmed for the C3.
// Fetched by a background FreeRTOS task (weatherBegin), so the screen never
// freezes on the network: the info screens only read the cached result.
// Current temp, condition, wind and day/night; today's hi/lo and rain chance.
// ============================================================================

enum WeatherIcon : uint8_t {
  WX_CLEAR = 0, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_THUNDER
};

struct Weather {
  bool   valid = false;
  int    tempF = 0;
  int    hi = 0, lo = 0;
  uint8_t code = 0;          // WMO code
  int    windMph = 0;        // current wind speed
  int    pop = 0;            // today's max precipitation probability, %
  bool   isDay = true;
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

// ---- fetch (blocking: runs only in the weather task) ------------------------
static portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;

inline bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  client.setTimeout(5000);
  if (!client.connect("api.open-meteo.com", 80)) {
    Serial.println("[weather] connect failed");
    return false;
  }

  char req[384];   // the request line is ~270 chars; at 256 it was cut off (404)
  snprintf(req, sizeof(req),
    "GET /v1/forecast?latitude=%s&longitude=%s"
    "&current=temperature_2m,weather_code,wind_speed_10m,is_day"
    "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
    "&temperature_unit=%s&wind_speed_unit=mph&forecast_days=1&timezone=auto HTTP/1.0",   // 1.0: no chunked body
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

  // Read until the server closes (Connection: close). Stopping at the first
  // moment nothing was waiting only got the first packet once the reply grew
  // past it, and every field parsed as 0.
  static char resp[1600];
  int n = 0;
  t0 = millis();
  while ((client.connected() || client.available()) && n < (int)sizeof(resp) - 1 && millis() - t0 < 5000) {
    if (client.available()) resp[n++] = client.read();
    else delay(5);
  }
  resp[n] = '\0';
  client.stop();

  const char* body = strstr(resp, "\r\n\r\n");
  body = body ? body + 4 : resp;

  // Parse into a copy, then swap it in under the lock so the renderer never
  // reads half an update
  Weather w = weather;
  const char* cur = strstr(body, "\"current\"");
  float f;
  if (!cur || !jsonFloat(cur, "\"temperature_2m\"", f)) {
    Serial.printf("[weather] no current block in %d bytes: %.160s\n", n, resp);
    return false;
  }
  w.tempF = (int)lroundf(f);
  if (cur && jsonFloat(cur, "\"weather_code\"", f)) {
    w.code = (uint8_t)(f + 0.5f);
    strncpy(w.condition, wmoText(w.code), sizeof(w.condition) - 1);
    w.condition[sizeof(w.condition) - 1] = '\0';
  }
  if (cur && jsonFloat(cur, "\"wind_speed_10m\"", f)) w.windMph = (int)lroundf(f);
  if (cur && jsonFloat(cur, "\"is_day\"", f)) w.isDay = f > 0.5f;
  const char* daily = strstr(body, "\"daily\"");
  if (daily && jsonFirstInArray(daily, "\"temperature_2m_max\"", f)) w.hi = (int)lroundf(f);
  if (daily && jsonFirstInArray(daily, "\"temperature_2m_min\"", f)) w.lo = (int)lroundf(f);
  if (daily && jsonFirstInArray(daily, "\"precipitation_probability_max\"", f)) w.pop = (int)lroundf(f);
  w.lastFetchMs = millis();
  w.valid = true;
  portENTER_CRITICAL(&weatherMux);
  weather = w;
  portEXIT_CRITICAL(&weatherMux);
  Serial.printf("[weather] %d%c %s  H%d L%d  wind %d  pop %d%%  %s\n",
                w.tempF, WEATHER_UNIT_F ? 'F' : 'C', w.condition, w.hi, w.lo,
                w.windMph, w.pop, w.isDay ? "day" : "night");
  return true;
}

// A copy of the latest weather, safe to read from the render loop
inline Weather weatherNow() {
  portENTER_CRITICAL(&weatherMux);
  Weather w = weather;
  portEXIT_CRITICAL(&weatherMux);
  return w;
}

// Background task: fetch at boot, then every WEATHER_REFRESH_MS; while there
// is no valid data, retry every 30 s so a transient failure recovers
static void weatherTask(void *) {
  unsigned long lastAttempt = 0;
  for (;;) {
    bool due = weather.valid ? (millis() - weather.lastFetchMs) > WEATHER_REFRESH_MS
                             : (lastAttempt == 0 || millis() - lastAttempt > 30000);
    if (due && WiFi.status() == WL_CONNECTED) { lastAttempt = millis(); fetchWeather(); }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

inline void weatherBegin() { xTaskCreate(weatherTask, "weather", 6144, nullptr, 1, nullptr); }

// The ink weather scenes (ink_time.h): one of 7 behaviours of the drop
enum InkSky : uint8_t { SKY_CLEAR = 0, SKY_CLOUDY, SKY_RAIN, SKY_SNOW, SKY_STORM, SKY_FOG, SKY_WIND, SKY_NUM };
inline uint8_t inkSky(const Weather &w) {
  WeatherIcon ic = wmoIcon(w.code);
  if (ic == WX_THUNDER) return SKY_STORM;
  if (ic == WX_RAIN || ic == WX_DRIZZLE) return SKY_RAIN;
  if (ic == WX_SNOW) return SKY_SNOW;
  if (ic == WX_FOG) return SKY_FOG;
  if (w.windMph >= INK_WINDY_MPH) return SKY_WIND;     // WMO codes have no wind
  if (ic == WX_CLEAR) return SKY_CLEAR;
  return SKY_CLOUDY;
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
