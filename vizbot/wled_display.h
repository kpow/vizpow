#ifndef WLED_DISPLAY_H
#define WLED_DISPLAY_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "system_status.h"

// ============================================================================
// WLED Display — Forward speech bubble text to WLED LED matrix
// ============================================================================
// Sends scrolling text to a WLED-controlled 32x8 LED pixel grid on the LAN.
// Uses WLED's HTTP JSON API with scrolling text effect (FX 122).
//
// Cross-core design (matches wifi_provisioning.h pattern):
//   Core 1 (render loop) calls wledQueueText() from speechBubble.show()
//   Core 0 (WiFi task) calls pollWledDisplay() to send HTTP requests
//
// Uses raw WiFiClient — no HTTPClient library (~30KB flash savings).
// ============================================================================

// WLED scrolling text effect index
#define WLED_FX_SCROLL_TEXT 122

// Default segment ID to target
#define WLED_SEGMENT_ID 0

// Timeouts
#define WLED_HTTP_TIMEOUT_MS 500

// Retry backoff after failure (don't spam unreachable device)
#define WLED_RETRY_BACKOFF_MS 30000

// Scroll time: base + per-character (ms). Gives text time to scroll across 32px.
#define WLED_SCROLL_BASE_MS 12000
#define WLED_SCROLL_PER_CHAR_MS 1200

// ============================================================================
// WLED State — shared between cores
// ============================================================================

enum WledSendState : uint8_t {
  WLED_IDLE = 0,
  WLED_SEND_REQUESTED,
};

struct WledDisplayData {
  // Configuration (persisted in NVS)
  char ip[16];
  bool enabled;
  uint8_t scrollSpeed;     // 0-255
  uint8_t r, g, b;         // text color

  // Cross-core text buffer (Core 1 writes, Core 0 reads)
  volatile WledSendState sendState;
  char textBuffer[32];
  uint16_t textDurationMs;

  // Saved segment state for restore (Core 0 only)
  int savedFx;
  int savedSx;
  int savedIx;
  int savedPal;
  char savedSegName[32];
  bool hasSavedState;

  // Restore timer (Core 0 only)
  unsigned long restoreAtMs;

  // Runtime state (Core 0 only)
  bool reachable;
  unsigned long lastFailTime;
};

static WledDisplayData wledData = {};

// ============================================================================
// NVS Persistence
// ============================================================================

void loadWledSettings() {
  Preferences prefs;
  if (!prefs.begin("vizbot", true)) return;

  String ip = prefs.getString("wledIP", "10.0.0.226");
  if (ip.length() > 0 && ip.length() < 16) {
    strncpy(wledData.ip, ip.c_str(), 15);
    wledData.ip[15] = '\0';
  }
  wledData.enabled     = prefs.getBool("wledOn", true);
  wledData.scrollSpeed = prefs.getUChar("wledSpd", 255);
  wledData.r           = prefs.getUChar("wledR", 255);
  wledData.g           = prefs.getUChar("wledG", 255);
  wledData.b           = prefs.getUChar("wledB", 255);

  prefs.end();

  wledData.reachable     = true;
  wledData.sendState     = WLED_IDLE;
  wledData.hasSavedState = false;
  wledData.restoreAtMs   = 0;
  wledData.savedFx       = -1;

  DBG("WLED: ");
  DBG(wledData.enabled ? "ON" : "OFF");
  DBG(" IP=");
  DBGLN(wledData.ip);
}

void saveWledSettings() {
  Preferences prefs;
  if (!prefs.begin("vizbot", false)) return;

  prefs.putString("wledIP", wledData.ip);
  prefs.putBool("wledOn", wledData.enabled);
  prefs.putUChar("wledSpd", wledData.scrollSpeed);
  prefs.putUChar("wledR", wledData.r);
  prefs.putUChar("wledG", wledData.g);
  prefs.putUChar("wledB", wledData.b);

  prefs.end();
  DBGLN("WLED settings saved");
}

// ============================================================================
// Raw HTTP helpers — WiFiClient (no HTTPClient library)
// ============================================================================

// POST JSON to /json/state. Returns true on HTTP 200.
bool wledHttpPost(const char* jsonBody) {
  WiFiClient client;
  client.setTimeout(WLED_HTTP_TIMEOUT_MS);

  if (!client.connect(wledData.ip, 80)) {
    return false;
  }

  int bodyLen = strlen(jsonBody);
  client.printf("POST /json/state HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                wledData.ip, bodyLen, jsonBody);

  // Read status line
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > WLED_HTTP_TIMEOUT_MS) {
      client.stop();
      return false;
    }
  }

  String statusLine = client.readStringUntil('\n');

  // Drain response
  while (client.available()) client.read();
  client.stop();

  // Check for "200" in status line
  return (statusLine.indexOf("200") > 0);
}

// GET path, return response body (after headers)
String wledHttpGet(const char* path) {
  WiFiClient client;
  client.setTimeout(WLED_HTTP_TIMEOUT_MS);

  if (!client.connect(wledData.ip, 80)) {
    return "";
  }

  client.printf("GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: close\r\n"
                "\r\n",
                path, wledData.ip);

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > WLED_HTTP_TIMEOUT_MS) {
      client.stop();
      return "";
    }
  }

  String response = "";
  timeout = millis();
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += (char)client.read();
    }
    if (millis() - timeout > 2000) break;
  }
  client.stop();

  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart >= 0) {
    return response.substring(bodyStart + 4);
  }
  return response;
}

// ============================================================================
// Capture current WLED segment state (for restore)
// ============================================================================

bool wledCaptureState() {
  String json = wledHttpGet("/json/state");
  if (json.length() == 0) return false;

  int segIdx = json.indexOf("\"seg\"");
  if (segIdx < 0) return false;

  // Find first segment object
  int segStart = json.indexOf('{', segIdx);
  if (segStart < 0) return false;
  int segEnd = json.indexOf('}', segStart);
  if (segEnd < 0) return false;

  String seg = json.substring(segStart, segEnd + 1);

  // Parse int fields
  auto findInt = [&](const char* key, int& out) -> bool {
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    int idx = seg.indexOf(pattern);
    if (idx >= 0) {
      out = seg.substring(idx + strlen(pattern)).toInt();
      return true;
    }
    return false;
  };

  wledData.savedFx = -1;
  findInt("fx", wledData.savedFx);
  findInt("sx", wledData.savedSx);
  findInt("ix", wledData.savedIx);
  findInt("pal", wledData.savedPal);

  // Parse segment name
  wledData.savedSegName[0] = '\0';
  int nIdx = seg.indexOf("\"n\":\"");
  if (nIdx >= 0) {
    nIdx += 5;
    int nEnd = seg.indexOf("\"", nIdx);
    if (nEnd > nIdx && (nEnd - nIdx) < 31) {
      seg.substring(nIdx, nEnd).toCharArray(wledData.savedSegName, 32);
    }
  }

  wledData.hasSavedState = (wledData.savedFx >= 0);

  DBG("WLED captured: fx=");
  DBG(wledData.savedFx);
  DBG(" n=\"");
  DBG(wledData.savedSegName);
  DBGLN("\"");

  return wledData.hasSavedState;
}

// ============================================================================
// Queue Text — called from Core 1 (render loop) via speechBubble.show()
// ============================================================================

void wledQueueText(const char* text, uint16_t durationMs) {
  if (!wledData.enabled) return;
  if (wledData.ip[0] == '\0') return;
  if (!sysStatus.staConnected) return;

  strncpy(wledData.textBuffer, text, 31);
  wledData.textBuffer[31] = '\0';
  wledData.textDurationMs = durationMs;
  wledData.sendState = WLED_SEND_REQUESTED;
}

// ============================================================================
// Poll — called from Core 0 (WiFi task)
// ============================================================================

void pollWledDisplay() {
  // ---- Restore previous effect (jump cut back) ----
  if (wledData.restoreAtMs > 0 && millis() >= wledData.restoreAtMs) {
    wledData.restoreAtMs = 0;

    if (wledData.hasSavedState) {
      char body[160];
      snprintf(body, sizeof(body),
        "{\"transition\":0,\"seg\":{\"id\":%d,\"fx\":%d,\"sx\":%d,\"ix\":%d,\"pal\":%d,\"n\":\"%s\"}}",
        WLED_SEGMENT_ID,
        wledData.savedFx, wledData.savedSx,
        wledData.savedIx, wledData.savedPal,
        wledData.savedSegName);

      if (wledHttpPost(body)) {
        DBGLN("WLED: restored");
      } else {
        DBGLN("WLED: restore failed");
      }
      wledData.hasSavedState = false;
    }
  }

  // ---- Send scrolling text ----
  if (wledData.sendState != WLED_SEND_REQUESTED) return;

  // Consume the request
  wledData.sendState = WLED_IDLE;

  // Check backoff
  if (!wledData.reachable) {
    if (millis() - wledData.lastFailTime < WLED_RETRY_BACKOFF_MS) {
      return;
    }
  }

  if (wledData.hasSavedState) {
    // Already mid-display — keep original saved state, cancel pending restore
    wledData.restoreAtMs = 0;
    DBG("WLED: replacing with \"");
    DBG(wledData.textBuffer);
    DBGLN("\"");

    // Force WLED to reinitialize effect 122 by briefly switching away.
    // WLED only clears effect data when the effect number actually changes.
    char resetBody[80];
    snprintf(resetBody, sizeof(resetBody),
      "{\"transition\":0,\"seg\":{\"id\":%d,\"fx\":0}}",
      WLED_SEGMENT_ID);
    wledHttpPost(resetBody);
  } else {
    // First text — capture current state for later restore
    if (wledCaptureState()) {
      DBG("WLED: captured, sending \"");
    } else {
      DBG("WLED: capture failed, sending \"");
    }
    DBG(wledData.textBuffer);
    DBGLN("\"");
  }

  // Send scrolling text
  char body[160];
  snprintf(body, sizeof(body),
    "{\"transition\":0,\"seg\":{\"id\":%d,\"fx\":%d,\"sx\":%d,\"ix\":0,\"col\":[[%d,%d,%d]],\"n\":\"%s\"}}",
    WLED_SEGMENT_ID,
    WLED_FX_SCROLL_TEXT,
    wledData.scrollSpeed,
    wledData.r, wledData.g, wledData.b,
    wledData.textBuffer);

  if (wledHttpPost(body)) {
    wledData.reachable = true;

    // Static display — use the speech bubble duration directly
    unsigned long displayTime = max((unsigned long)wledData.textDurationMs,
                                    (unsigned long)WLED_SCROLL_BASE_MS);
    wledData.restoreAtMs = millis() + displayTime;

    DBG("WLED: sent OK, restore in ");
    DBG(displayTime);
    DBGLN("ms");
  } else {
    wledData.reachable = false;
    wledData.lastFailTime = millis();
    DBGLN("WLED: send failed");
  }
}

// ============================================================================
// Configuration helpers — called from web handlers (Core 0)
// ============================================================================

void wledSetIP(const char* ip) {
  strncpy(wledData.ip, ip, 15);
  wledData.ip[15] = '\0';
  wledData.reachable = true;
  wledData.lastFailTime = 0;
  saveWledSettings();
}

void wledSetEnabled(bool on) {
  wledData.enabled = on;
  if (on) {
    wledData.reachable = true;
    wledData.lastFailTime = 0;
  }
  saveWledSettings();
}

void wledSetColor(uint8_t r, uint8_t g, uint8_t b) {
  wledData.r = r;
  wledData.g = g;
  wledData.b = b;
  saveWledSettings();
}

void wledSetSpeed(uint8_t spd) {
  wledData.scrollSpeed = spd;
  saveWledSettings();
}

String getWledStatusJson() {
  String json = "{\"enabled\":";
  json += wledData.enabled ? "true" : "false";
  json += ",\"ip\":\"";
  json += wledData.ip;
  json += "\",\"reachable\":";
  json += wledData.reachable ? "true" : "false";
  json += ",\"speed\":";
  json += wledData.scrollSpeed;
  json += ",\"r\":";
  json += wledData.r;
  json += ",\"g\":";
  json += wledData.g;
  json += ",\"b\":";
  json += wledData.b;
  json += "}";
  return json;
}

#endif // WLED_DISPLAY_H
