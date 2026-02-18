#ifndef WEATHER_DATA_H
#define WEATHER_DATA_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "system_status.h"

// ============================================================================
// Weather Data — Open-Meteo API integration
// ============================================================================
// Fetches current conditions + 3-day forecast from Open-Meteo (free, no key).
// Uses raw WiFiClient (same pattern as wled_display.h — no HTTPClient lib).
//
// Thread safety: Core 1 (render) sets fetchRequested = true.
// Core 0 (WiFi task) checks it, fetches, writes results.
// Core 1 reads results only when valid == true && fetching == false.
// ============================================================================

// ============================================================================
// WMO Weather Code → condition text mapping
// ============================================================================

// Weather icon types (for procedural drawing)
enum WeatherIconType : uint8_t {
  WEATHER_ICON_CLEAR = 0,
  WEATHER_ICON_PARTLY_CLOUDY,
  WEATHER_ICON_CLOUDY,
  WEATHER_ICON_FOG,
  WEATHER_ICON_DRIZZLE,
  WEATHER_ICON_RAIN,
  WEATHER_ICON_SNOW,
  WEATHER_ICON_THUNDER,
};

// Map WMO code to icon type
WeatherIconType wmoToIcon(uint8_t wmoCode) {
  if (wmoCode == 0) return WEATHER_ICON_CLEAR;
  if (wmoCode <= 2) return WEATHER_ICON_PARTLY_CLOUDY;
  if (wmoCode == 3) return WEATHER_ICON_CLOUDY;
  if (wmoCode == 45 || wmoCode == 48) return WEATHER_ICON_FOG;
  if (wmoCode >= 51 && wmoCode <= 57) return WEATHER_ICON_DRIZZLE;
  if (wmoCode >= 61 && wmoCode <= 67) return WEATHER_ICON_RAIN;
  if (wmoCode >= 71 && wmoCode <= 77) return WEATHER_ICON_SNOW;
  if (wmoCode >= 80 && wmoCode <= 82) return WEATHER_ICON_RAIN;
  if (wmoCode >= 85 && wmoCode <= 86) return WEATHER_ICON_SNOW;
  if (wmoCode >= 95 && wmoCode <= 99) return WEATHER_ICON_THUNDER;
  return WEATHER_ICON_PARTLY_CLOUDY;  // Default fallback
}

// Map WMO code to human-readable text
void wmoToText(uint8_t wmoCode, char* buf, uint8_t bufSize) {
  const char* text;
  if (wmoCode == 0) text = "Clear";
  else if (wmoCode <= 2) text = "Partly Cloudy";
  else if (wmoCode == 3) text = "Overcast";
  else if (wmoCode == 45 || wmoCode == 48) text = "Foggy";
  else if (wmoCode >= 51 && wmoCode <= 57) text = "Drizzle";
  else if (wmoCode >= 61 && wmoCode <= 65) text = "Rain";
  else if (wmoCode == 66 || wmoCode == 67) text = "Freezing Rain";
  else if (wmoCode >= 71 && wmoCode <= 77) text = "Snow";
  else if (wmoCode >= 80 && wmoCode <= 82) text = "Showers";
  else if (wmoCode >= 85 && wmoCode <= 86) text = "Snow Showers";
  else if (wmoCode >= 95 && wmoCode <= 99) text = "Storms";
  else text = "Unknown";
  strncpy(buf, text, bufSize - 1);
  buf[bufSize - 1] = '\0';
}

// Temperature → color (RGB565) for bar graph
uint16_t getTempColor(int tempF) {
  if (tempF < 32) return 0x001F;       // Blue (freezing)
  if (tempF < 50) return 0x03FF;       // Cyan (cold)
  if (tempF < 65) return 0x07E0;       // Green (cool)
  if (tempF < 80) return 0xFFE0;       // Yellow (warm)
  if (tempF < 95) return 0xFBE0;       // Orange (hot)
  return 0xF800;                        // Red (very hot)
}

// ============================================================================
// Data Structures
// ============================================================================

struct WeatherCurrent {
  float tempF;                   // Current temperature (Fahrenheit)
  uint8_t weatherCode;           // WMO weather code (0-99)
  char conditionText[20];        // "Sunny", "Cloudy", etc.
  bool isDay;                    // Day/night indicator
};

struct WeatherForecastDay {
  float highF;                   // Daily max temp
  float lowF;                    // Daily min temp
  uint8_t weatherCode;           // WMO code for the day
  char dayName[4];               // "Mon", "Tue", etc.
};

struct WeatherData {
  WeatherCurrent current;
  WeatherForecastDay forecast[3];      // 3-day forecast
  bool valid;                           // Data is populated and usable
  bool fetching;                        // Fetch in progress on Core 0
  unsigned long lastFetchMs;            // When data was last refreshed
  char errorMsg[24];                    // Error description if fetch failed
  volatile bool fetchRequested;         // Core 1 sets, Core 0 reads/clears
};

WeatherData weatherData = {};

// ============================================================================
// JSON Parsing Helpers (manual — no ArduinoJson, matches project pattern)
// ============================================================================

// Find a key in JSON and extract the numeric value after it
// Returns true if found, sets outVal
static bool jsonExtractFloat(const char* json, const char* key, float& outVal) {
  const char* pos = strstr(json, key);
  if (!pos) return false;
  pos += strlen(key);
  // Skip to colon and whitespace
  while (*pos && (*pos == '"' || *pos == ':' || *pos == ' ')) pos++;
  outVal = atof(pos);
  return true;
}

// Find a key and extract integer value
static bool jsonExtractInt(const char* json, const char* key, int& outVal) {
  float f;
  if (!jsonExtractFloat(json, key, f)) return false;
  outVal = (int)(f + 0.5f);
  return true;
}

// Extract an array of floats from JSON: finds key, then reads comma-separated values
// "key":[val0,val1,val2,...]
static int jsonExtractFloatArray(const char* json, const char* key, float* outArr, int maxCount) {
  const char* pos = strstr(json, key);
  if (!pos) return 0;
  // Find the opening bracket
  pos = strchr(pos, '[');
  if (!pos) return 0;
  pos++; // skip '['

  int count = 0;
  while (*pos && *pos != ']' && count < maxCount) {
    // Skip whitespace
    while (*pos == ' ' || *pos == ',') pos++;
    if (*pos == ']') break;
    outArr[count++] = atof(pos);
    // Skip to next comma or end
    while (*pos && *pos != ',' && *pos != ']') pos++;
  }
  return count;
}

// Extract integer array
static int jsonExtractIntArray(const char* json, const char* key, int* outArr, int maxCount) {
  float fArr[8];
  int count = jsonExtractFloatArray(json, key, fArr, min(maxCount, 8));
  for (int i = 0; i < count; i++) {
    outArr[i] = (int)(fArr[i] + 0.5f);
  }
  return count;
}

// Get day-of-week name from date string "YYYY-MM-DD"
static void dateToDayName(const char* dateStr, char* dayBuf, uint8_t bufSize) {
  // Parse date
  int year = 0, month = 0, day = 0;
  sscanf(dateStr, "%d-%d-%d", &year, &month, &day);

  // Use struct tm + mktime to get day of week
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  mktime(&t);

  const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  strncpy(dayBuf, days[t.tm_wday], bufSize - 1);
  dayBuf[bufSize - 1] = '\0';
}

// Extract date strings from "time" array in daily block
static int jsonExtractDateArray(const char* json, const char* key, char dayNames[][4], int maxCount) {
  const char* pos = strstr(json, key);
  if (!pos) return 0;
  pos = strchr(pos, '[');
  if (!pos) return 0;
  pos++; // skip '['

  int count = 0;
  while (*pos && *pos != ']' && count < maxCount) {
    // Skip to opening quote
    while (*pos && *pos != '"') pos++;
    if (*pos != '"') break;
    pos++; // skip opening quote

    // Read date string (10 chars: YYYY-MM-DD)
    char dateStr[12] = {};
    int di = 0;
    while (*pos && *pos != '"' && di < 10) {
      dateStr[di++] = *pos++;
    }
    dateStr[di] = '\0';
    if (*pos == '"') pos++; // skip closing quote

    dateToDayName(dateStr, dayNames[count], 4);
    count++;
  }
  return count;
}

// ============================================================================
// Parse the full Open-Meteo response
// ============================================================================

static void parseWeatherResponse(const char* response) {
  // Find the JSON body (after HTTP headers — empty line)
  const char* body = strstr(response, "\r\n\r\n");
  if (!body) body = response;  // Fallback: maybe no headers
  else body += 4;

  // Current conditions
  // Look for "current" block
  const char* currentBlock = strstr(body, "\"current\"");
  if (currentBlock) {
    float tempVal;
    if (jsonExtractFloat(currentBlock, "\"temperature_2m\"", tempVal)) {
      weatherData.current.tempF = tempVal;
    }

    int codeVal;
    if (jsonExtractInt(currentBlock, "\"weather_code\"", codeVal)) {
      weatherData.current.weatherCode = (uint8_t)codeVal;
      wmoToText(codeVal, weatherData.current.conditionText,
                sizeof(weatherData.current.conditionText));
    }

    int isDayVal;
    if (jsonExtractInt(currentBlock, "\"is_day\"", isDayVal)) {
      weatherData.current.isDay = (isDayVal == 1);
    }
  }

  // Daily forecast
  const char* dailyBlock = strstr(body, "\"daily\"");
  if (dailyBlock) {
    // Extract arrays
    float highs[3], lows[3];
    int codes[3];
    char dayNames[3][4];

    int nHighs = jsonExtractFloatArray(dailyBlock, "\"temperature_2m_max\"", highs, 3);
    int nLows = jsonExtractFloatArray(dailyBlock, "\"temperature_2m_min\"", lows, 3);
    int nCodes = jsonExtractIntArray(dailyBlock, "\"weather_code\"", codes, 3);
    int nDays = jsonExtractDateArray(dailyBlock, "\"time\"", dayNames, 3);

    int forecastCount = min(min(nHighs, nLows), min(nCodes, nDays));
    if (forecastCount > 3) forecastCount = 3;

    for (int i = 0; i < forecastCount; i++) {
      weatherData.forecast[i].highF = highs[i];
      weatherData.forecast[i].lowF = lows[i];
      weatherData.forecast[i].weatherCode = (uint8_t)codes[i];
      strncpy(weatherData.forecast[i].dayName, dayNames[i], 3);
      weatherData.forecast[i].dayName[3] = '\0';
    }
  }
}

// ============================================================================
// Weather Fetch (runs on Core 0 — WiFi task)
// ============================================================================

void fetchWeather() {
  if (!sysStatus.staConnected) {
    strncpy(weatherData.errorMsg, "No WiFi", sizeof(weatherData.errorMsg));
    weatherData.fetching = false;
    return;
  }

  weatherData.fetching = true;
  weatherData.errorMsg[0] = '\0';

  WiFiClient client;
  client.setTimeout(5000);

  if (!client.connect("api.open-meteo.com", 80)) {
    strncpy(weatherData.errorMsg, "Connect failed", sizeof(weatherData.errorMsg));
    weatherData.fetching = false;
    return;
  }

  // Build request path with configured lat/lon
  char request[256];
  snprintf(request, sizeof(request),
    "GET /v1/forecast?latitude=%s&longitude=%s"
    "&current=temperature_2m,weather_code,is_day"
    "&daily=temperature_2m_max,temperature_2m_min,weather_code"
    "&temperature_unit=fahrenheit"
    "&forecast_days=3"
    "&timezone=auto HTTP/1.1",
    WEATHER_LAT, WEATHER_LON);

  client.println(request);
  client.println("Host: api.open-meteo.com");
  client.println("Connection: close");
  client.println();

  // Wait for response (same pattern as POC)
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 8000) {
      strncpy(weatherData.errorMsg, "Timeout", sizeof(weatherData.errorMsg));
      weatherData.fetching = false;
      client.stop();
      return;
    }
    delay(10);
  }

  // Read response into stack buffer
  char response[1200];
  int responseLen = 0;
  while (client.available() && responseLen < (int)(sizeof(response) - 1)) {
    response[responseLen++] = client.read();
  }
  response[responseLen] = '\0';
  client.stop();

  // Parse
  parseWeatherResponse(response);

  weatherData.lastFetchMs = millis();
  weatherData.fetching = false;
  weatherData.valid = true;

  DBGLN("Weather fetch OK");
}

// ============================================================================
// Poll — called from Core 0 WiFi task each iteration
// ============================================================================

void pollWeatherFetch() {
  if (!weatherData.fetchRequested) return;
  weatherData.fetchRequested = false;
  fetchWeather();
}

// Request a weather fetch (called from Core 1)
void requestWeatherFetch() {
  weatherData.fetchRequested = true;
}

#endif // WEATHER_DATA_H
