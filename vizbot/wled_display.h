#ifndef WLED_DISPLAY_H
#define WLED_DISPLAY_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include "config.h"
#include "system_status.h"
#include "wled_font.h"

// ============================================================================
// WLED Display — Direct pixel control via DDP (Distributed Display Protocol)
// ============================================================================
// Renders a 32x8 pixel buffer on the ESP32 and transmits raw RGB data to a
// WLED-controlled LED matrix over UDP using DDP. This gives full creative
// freedom — custom fonts, pixel art, sprites, animations.
//
// Cross-core design (matches wifi_provisioning.h pattern):
//   Core 1 (render loop) calls wledQueueText() or wledQueueFrame()
//   Core 0 (WiFi task)   calls pollWledDisplay() to send DDP + manage restore
//
// DDP is first-class in WLED (enabled by default since 0.13). WLED auto-enters
// "realtime mode" on DDP frames and auto-resumes its previous effect when
// frames stop (2.5s timeout). We use HTTP restore for instant cut-back.
// ============================================================================

// Display geometry
#define WLED_DISPLAY_WIDTH   32
#define WLED_DISPLAY_HEIGHT  8
#define WLED_NUM_PIXELS      (WLED_DISPLAY_WIDTH * WLED_DISPLAY_HEIGHT)  // 256
#define WLED_PIXEL_BYTES     (WLED_NUM_PIXELS * 3)                       // 768

// DDP protocol constants
#define WLED_DDP_PORT        4048
#define WLED_DDP_HEADER_SIZE 10

// Default segment ID to target (for HTTP restore)
#define WLED_SEGMENT_ID 0

// Timeouts
#define WLED_HTTP_TIMEOUT_MS 500

// Retry backoff after failure (don't spam unreachable device)
#define WLED_RETRY_BACKOFF_MS 30000

// ============================================================================
// WLED State — shared between cores
// ============================================================================

enum WledSendState : uint8_t {
  WLED_IDLE = 0,
  WLED_FRAME_REQUESTED,       // DDP frame ready in pixel buffer
};

// Animation phases for hold timer
enum WledPhase : uint8_t {
  WLED_PHASE_NONE = 0,        // Idle — nothing active
  WLED_PHASE_HOLD,            // Frame displayed, waiting for hold to expire
};

struct WledDisplayData {
  // Configuration (persisted in NVS)
  char ip[16];
  bool enabled;
  uint8_t scrollSpeed;       // 0-255 (kept for NVS compatibility)
  uint8_t textIx;            // 0-255 (kept for NVS compatibility)
  uint8_t r, g, b;           // text color

  // Pixel buffer — 256 pixels × 3 bytes RGB (BSS, not stack)
  uint8_t pixelBuffer[WLED_PIXEL_BYTES];

  // DDP transport (Core 0 only)
  WiFiUDP udp;
  uint8_t ddpSequence;

  // Cross-core signaling (Core 1 writes, Core 0 reads)
  volatile WledSendState sendState;
  uint16_t frameDurationMs;

  // Text buffer for wledQueueText compatibility (Core 1 writes, Core 0 reads)
  char textBuffer[32];

  // Saved segment state for restore (Core 0 only)
  int savedFx;
  int savedSx;
  int savedIx;
  int savedPal;
  char savedSegName[32];
  bool hasSavedState;

  // Restore timer (Core 0 only)
  unsigned long restoreAtMs;

  // Animation phase state (Core 0 only)
  WledPhase phase;
  unsigned long phaseEndMs;

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
  wledData.scrollSpeed = prefs.getUChar("wledSpd", 200);
  wledData.textIx      = prefs.getUChar("wledIx", 128);
  wledData.r           = prefs.getUChar("wledR", 255);
  wledData.g           = prefs.getUChar("wledG", 255);
  wledData.b           = prefs.getUChar("wledB", 255);

  prefs.end();

  wledData.reachable     = true;
  wledData.sendState     = WLED_IDLE;
  wledData.hasSavedState = false;
  wledData.restoreAtMs   = 0;
  wledData.savedFx       = -1;
  wledData.phase         = WLED_PHASE_NONE;
  wledData.phaseEndMs    = 0;
  wledData.ddpSequence   = 0;

  memset(wledData.pixelBuffer, 0, WLED_PIXEL_BYTES);

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
  prefs.putUChar("wledIx", wledData.textIx);
  prefs.putUChar("wledR", wledData.r);
  prefs.putUChar("wledG", wledData.g);
  prefs.putUChar("wledB", wledData.b);

  prefs.end();
  DBGLN("WLED settings saved");
}

// ============================================================================
// Pixel buffer drawing functions — called from Core 1
// ============================================================================

void wledPixelClear() {
  memset(wledData.pixelBuffer, 0, WLED_PIXEL_BYTES);
}

void wledPixelSet(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
  if (x >= WLED_DISPLAY_WIDTH || y >= WLED_DISPLAY_HEIGHT) return;
  uint16_t offset = (y * WLED_DISPLAY_WIDTH + x) * 3;
  wledData.pixelBuffer[offset]     = r;
  wledData.pixelBuffer[offset + 1] = g;
  wledData.pixelBuffer[offset + 2] = b;
}

void wledPixelFill(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < WLED_PIXEL_BYTES; i += 3) {
    wledData.pixelBuffer[i]     = r;
    wledData.pixelBuffer[i + 1] = g;
    wledData.pixelBuffer[i + 2] = b;
  }
}

// Render centered text into the pixel buffer using the 3x5 font
void wledPixelDrawText(const char* text, uint8_t r, uint8_t g, uint8_t b) {
  wledFontDrawString(wledData.pixelBuffer,
                     WLED_DISPLAY_WIDTH, WLED_DISPLAY_HEIGHT,
                     text, r, g, b);
}

// ============================================================================
// DDP Transport — called from Core 0 (WiFi task)
// ============================================================================

// Send the pixel buffer to WLED via DDP over UDP.
// DDP header is 10 bytes, followed by 768 bytes of RGB data.
// Total packet: 778 bytes — well within UDP MTU of 1472 bytes.
bool wledSendDDP() {
  IPAddress targetIP;
  if (!targetIP.fromString(wledData.ip)) return false;

  // Build 10-byte DDP header
  uint8_t header[WLED_DDP_HEADER_SIZE];
  header[0] = 0x41;                            // Version 1 + push flag
  header[1] = wledData.ddpSequence & 0x0F;     // Sequence (0-15, wrapping)
  header[2] = 0x01;                            // RGB, 8-bit per channel
  header[3] = 0x01;                            // Device ID
  header[4] = 0x00;                            // Data offset (big-endian)
  header[5] = 0x00;
  header[6] = 0x00;
  header[7] = 0x00;
  header[8] = (WLED_PIXEL_BYTES >> 8) & 0xFF;  // Data length high byte
  header[9] = WLED_PIXEL_BYTES & 0xFF;          // Data length low byte

  wledData.ddpSequence = (wledData.ddpSequence + 1) & 0x0F;

  // Send as single UDP packet: header + pixel data
  if (!wledData.udp.beginPacket(targetIP, WLED_DDP_PORT)) return false;
  wledData.udp.write(header, WLED_DDP_HEADER_SIZE);
  wledData.udp.write(wledData.pixelBuffer, WLED_PIXEL_BYTES);
  return wledData.udp.endPacket();
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
// Queue functions — called from Core 1 (render loop)
// ============================================================================

// Queue a raw pixel frame for DDP transmission.
// The pixel buffer must already be populated before calling this.
void wledQueueFrame(uint16_t durationMs) {
  if (!wledData.enabled) return;
  if (wledData.ip[0] == '\0') return;
  if (!sysStatus.staConnected) return;

  wledData.frameDurationMs = durationMs;
  wledData.sendState = WLED_FRAME_REQUESTED;
}

// Queue text for display — renders into pixel buffer, then queues DDP frame.
// Drop-in replacement for old FX 122 path. All existing callers work unchanged.
void wledQueueText(const char* text, uint16_t durationMs) {
  if (!wledData.enabled) return;
  if (wledData.ip[0] == '\0') return;
  if (!sysStatus.staConnected) return;

  // Store text for debug logging
  strncpy(wledData.textBuffer, text, 31);
  wledData.textBuffer[31] = '\0';

  // Render text into pixel buffer
  wledPixelClear();
  wledPixelDrawText(text, wledData.r, wledData.g, wledData.b);

  // Queue as DDP frame
  wledData.frameDurationMs = durationMs;
  wledData.sendState = WLED_FRAME_REQUESTED;
}

// ============================================================================
// Poll — called from Core 0 (WiFi task)
// ============================================================================

void pollWledDisplay() {
  // ---- Hold complete → restore previous effect immediately ----
  if (wledData.phase == WLED_PHASE_HOLD && millis() >= wledData.phaseEndMs) {
    wledData.phase = WLED_PHASE_NONE;
    wledData.restoreAtMs = millis();
    DBGLN("WLED: hold done, restoring");
  }

  // ---- Restore previous effect via HTTP (instant, no 2.5s DDP timeout) ----
  if (wledData.restoreAtMs > 0 && millis() >= wledData.restoreAtMs) {
    wledData.restoreAtMs = 0;

    if (wledData.hasSavedState) {
      char body[160];
      snprintf(body, sizeof(body),
        "{\"on\":true,\"live\":false,\"transition\":0,\"seg\":{\"id\":%d,\"fx\":%d,\"sx\":%d,\"ix\":%d,\"pal\":%d,\"n\":\"%s\"}}",
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

  // ---- Send DDP frame ----
  if (wledData.sendState != WLED_FRAME_REQUESTED) return;

  // Consume the request
  wledData.sendState = WLED_IDLE;

  // Check backoff
  if (!wledData.reachable) {
    if (millis() - wledData.lastFailTime < WLED_RETRY_BACKOFF_MS) {
      return;
    }
  }

  // Cancel any active phase
  wledData.phase = WLED_PHASE_NONE;
  wledData.phaseEndMs = 0;

  if (wledData.hasSavedState) {
    // Already mid-display — keep original saved state, cancel pending restore
    wledData.restoreAtMs = 0;
    DBG("WLED: replacing frame \"");
    DBG(wledData.textBuffer);
    DBGLN("\"");
  } else {
    // First frame — capture current state for later restore
    if (wledCaptureState()) {
      DBG("WLED: captured, sending DDP \"");
    } else {
      DBG("WLED: capture failed, sending DDP \"");
    }
    DBG(wledData.textBuffer);
    DBGLN("\"");
  }

  // Send pixel buffer via DDP
  if (wledSendDDP()) {
    wledData.reachable = true;

    // Hold: frame displayed, wait for duration, then restore
    wledData.phase = WLED_PHASE_HOLD;
    wledData.phaseEndMs = millis() + wledData.frameDurationMs;
    wledData.restoreAtMs = 0;

    DBG("WLED: DDP frame for ");
    DBG(wledData.frameDurationMs);
    DBGLN("ms");
  } else {
    wledData.reachable = false;
    wledData.lastFailTime = millis();
    DBGLN("WLED: DDP send failed");
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

void wledSetIx(uint8_t ix) {
  wledData.textIx = ix;
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
  json += ",\"ix\":";
  json += wledData.textIx;
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
