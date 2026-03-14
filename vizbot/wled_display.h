#ifndef WLED_DISPLAY_H
#define WLED_DISPLAY_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include "config.h"
#include "system_status.h"
#include "wled_font.h"
#include "emoji_sprites.h"

// WLED ownership gate — set by cloud_client.h after parsing sync response.
// True = this bot is allowed to send emoji/weather DDP frames.
// Default true (standalone mode, no group constraint).
static bool wledStreamAllowed = true;

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

// WLED debug logging — comment out to silence WLED serial chatter
// #define WLED_DEBUG
#ifdef WLED_DEBUG
  #define WLED_DBG(...)   DBG(__VA_ARGS__)
  #define WLED_DBGLN(...) DBGLN(__VA_ARGS__)
#else
  #define WLED_DBG(...)
  #define WLED_DBGLN(...)
#endif

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

// How often to poll WLED palette when idle (faster than WLED's typical ~5s cycle)
#define WLED_PAL_POLL_INTERVAL_MS 1000

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
  bool hologramMode;         // horizontal mirror for Pepper's ghost prism

  // Pixel buffer — 256 pixels × 3 bytes RGB (BSS, not stack)
  uint8_t pixelBuffer[WLED_PIXEL_BYTES];

  // DDP transport (Core 0 only)
  WiFiUDP udp;
  uint8_t ddpSequence;

  // Cross-core signaling (Core 1 writes, Core 0 reads)
  volatile WledSendState sendState;
  uint16_t frameDurationMs;

  // Text buffer for wledQueueText compatibility (Core 1 writes, Core 0 reads)
  char textBuffer[MAX_SAY_LEN];

  // Word sequence state (for multi-word phrases)
  char words[8][12];            // Up to 8 words, 11 chars each
  uint8_t wordCount;            // Total words in current sequence
  uint8_t currentWord;          // Index of word being displayed
  uint16_t perWordDurationMs;   // Hold time per word

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

  // Palette sync (Core 0 writes, Core 1 reads)
  volatile int8_t pendingPalSync;   // -1 = none; ≥0 = local palette index to apply

  // Runtime state (Core 0 only)
  bool reachable;
  unsigned long lastFailTime;
  unsigned long lastPalPollMs;
};

static WledDisplayData wledData = {};

// Parse IPv4 string "x.x.x.x" to uint32_t (network byte order)
static uint32_t wledParseIPv4(const char* ip) {
  uint8_t octets[4] = {0};
  int n = sscanf(ip, "%hhu.%hhu.%hhu.%hhu", &octets[0], &octets[1], &octets[2], &octets[3]);
  if (n != 4) return 0;
  return ((uint32_t)octets[0] << 24) | ((uint32_t)octets[1] << 16) |
         ((uint32_t)octets[2] << 8) | octets[3];
}

uint32_t wledGetIPAsU32() {
  return wledParseIPv4(wledData.ip);
}

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
  wledData.hologramMode = prefs.getBool("hologram", false);
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
  extern bool hologramMirrorLCD;
  hologramMirrorLCD = wledData.hologramMode;
  #endif

  prefs.end();

  wledData.reachable     = true;   // assume reachable until proven otherwise
  wledData.sendState     = WLED_IDLE;
  wledData.hasSavedState = false;
  wledData.restoreAtMs   = 0;
  wledData.savedFx       = -1;
  wledData.phase         = WLED_PHASE_NONE;
  wledData.phaseEndMs    = 0;
  wledData.ddpSequence   = 0;
  wledData.pendingPalSync = -1;
  wledData.wordCount     = 0;
  wledData.currentWord   = 0;

  memset(wledData.pixelBuffer, 0, WLED_PIXEL_BYTES);

  WLED_DBG("WLED: ");
  WLED_DBG(wledData.enabled ? "ON" : "OFF");
  WLED_DBG(" IP=");
  WLED_DBGLN(wledData.ip);
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
  prefs.putBool("hologram", wledData.hologramMode);

  prefs.end();
  WLED_DBGLN("WLED settings saved");
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

// Remap logical row-major pixel buffer to physical LED order for DDP.
// Physical strip: all rows run right-to-left (no serpentine).
//   LED 0 = top-right (x=31, y=0)
//   LED 31 = top-left (x=0, y=0)
//   LED 32 = second-row right (x=31, y=1)  ← same direction, not reversed
static void wledRemapPixels(uint8_t* ddpOut, const uint8_t* logicalBuf) {
  for (uint8_t y = 0; y < WLED_DISPLAY_HEIGHT; y++) {
    for (uint8_t x = 0; x < WLED_DISPLAY_WIDTH; x++) {
      uint16_t srcOff = (y * WLED_DISPLAY_WIDTH + x) * 3;
      uint16_t ledIdx = y * WLED_DISPLAY_WIDTH + (WLED_DISPLAY_WIDTH - 1 - x); // all rows: right→left
      uint16_t dstOff = ledIdx * 3;
      ddpOut[dstOff]     = logicalBuf[srcOff];
      ddpOut[dstOff + 1] = logicalBuf[srcOff + 1];
      ddpOut[dstOff + 2] = logicalBuf[srcOff + 2];
    }
  }
}

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

  uint8_t ddpPayload[WLED_PIXEL_BYTES];
  wledRemapPixels(ddpPayload, wledData.pixelBuffer);

  if (!wledData.udp.beginPacket(targetIP, WLED_DDP_PORT)) return false;
  wledData.udp.write(header, WLED_DDP_HEADER_SIZE);
  wledData.udp.write(ddpPayload, WLED_PIXEL_BYTES);
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

  // Read until 100ms of silence — handles keep-alive connections that stay open
  // after the response body is sent, without blocking for a full 2-second timeout.
  String response = "";
  unsigned long lastDataMs = millis();
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += (char)client.read();
      lastDataMs = millis();
    } else if (millis() - lastDataMs > 100) {
      break;  // No new data for 100ms — response complete
    }
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

// Map WLED palette ID → local palettes[] index (best visual match)
// WLED IDs 0-5 are meta/dynamic; 6-11 are FastLED standard palettes.
static const int8_t WLED_TO_LOCAL_PAL[] = {
  0,  // 0  Default          → Rainbow
  0,  // 1  Random Cycle     → Rainbow
  0,  // 2  Color 1          → Rainbow
  0,  // 3  Colors 1&2       → Rainbow
  7,  // 4  Color Gradient   → Sunset
  0,  // 5  Colors Only      → Rainbow
  4,  // 6  Party            → PartyColors_p
  6,  // 7  Cloud            → CloudColors_p
  2,  // 8  Lava             → LavaColors_p
  1,  // 9  Ocean            → OceanColors_p
  3,  // 10 Forest           → ForestColors_p
  0,  // 11 Rainbow          → RainbowColors_p
  0,  // 12 Rainbow Bands    → Rainbow
  7,  // 13 Sunset           → Sunset
  8,  // 14 Rivendell        → Cyber (closest cool-green)
  1,  // 15 Analogous        → Ocean
  9,  // 16 Splash           → Toxic
  10, // 17 Pastel           → Ice
  5,  // 18 Sunset 2         → HeatColors_p
  11, // 19 Beech            → Blood
  12, // 20 Vintage          → Vaporwave
};
#define WLED_TO_LOCAL_PAL_SIZE (sizeof(WLED_TO_LOCAL_PAL)/sizeof(WLED_TO_LOCAL_PAL[0]))

inline int8_t wledMapPalette(int wledPal) {
  if (wledPal < 0) return 0;
  if ((uint8_t)wledPal < WLED_TO_LOCAL_PAL_SIZE) return WLED_TO_LOCAL_PAL[wledPal];
  return 0;  // Unknown WLED palette → Rainbow
}

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
  wledData.pendingPalSync = wledMapPalette(wledData.savedPal);

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

  WLED_DBG("WLED captured: fx=");
  WLED_DBG(wledData.savedFx);
  WLED_DBG(" n=\"");
  WLED_DBG(wledData.savedSegName);
  WLED_DBGLN("\"");

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
// Multi-word text is split into words and sequenced one at a time by pollWledDisplay().
void wledQueueText(const char* text, uint16_t durationMs) {
  if (!wledData.enabled) return;
  if (wledData.ip[0] == '\0') return;
  if (!sysStatus.staConnected) return;

  // Store text for debug logging
  strncpy(wledData.textBuffer, text, MAX_SAY_LEN - 1);
  wledData.textBuffer[MAX_SAY_LEN - 1] = '\0';

  // Check if text has spaces (multi-word)
  bool hasSpace = false;
  for (const char* p = text; *p; p++) {
    if (*p == ' ') { hasSpace = true; break; }
  }

  if (!hasSpace) {
    // Single word — render directly (existing behavior)
    wledData.wordCount = 0;
    wledPixelClear();
    wledPixelDrawText(text, wledData.r, wledData.g, wledData.b);
    wledData.frameDurationMs = durationMs;
    wledData.sendState = WLED_FRAME_REQUESTED;
    return;
  }

  // Multi-word: split by spaces into words[]
  wledData.wordCount = 0;
  const char* p = text;
  while (*p && wledData.wordCount < 8) {
    while (*p == ' ') p++;  // skip leading spaces
    if (!*p) break;
    uint8_t len = 0;
    while (p[len] && p[len] != ' ' && len < 11) len++;
    memcpy(wledData.words[wledData.wordCount], p, len);
    wledData.words[wledData.wordCount][len] = '\0';
    wledData.wordCount++;
    p += len;
  }

  // Per-word duration: total / wordCount, min 800ms
  wledData.perWordDurationMs = max((uint16_t)(durationMs / wledData.wordCount), (uint16_t)800);
  wledData.currentWord = 0;

  // Render first word and queue
  wledPixelClear();
  wledPixelDrawText(wledData.words[0], wledData.r, wledData.g, wledData.b);
  wledData.frameDurationMs = wledData.perWordDurationMs;
  wledData.sendState = WLED_FRAME_REQUESTED;
}

// ============================================================================
// Lightweight palette poll — reads only `pal` from WLED, no saved-state touch
// ============================================================================

void wledPollPalette() {
  String json = wledHttpGet("/json/state");
  if (json.length() == 0) {
    wledData.reachable = false;
    wledData.lastFailTime = millis();
    return;
  }

  wledData.reachable = true;

  int segIdx = json.indexOf("\"seg\"");
  if (segIdx < 0) return;
  int segStart = json.indexOf('{', segIdx);
  if (segStart < 0) return;
  int segEnd = json.indexOf('}', segStart);
  if (segEnd < 0) return;

  String seg = json.substring(segStart, segEnd + 1);
  int palIdx = seg.indexOf("\"pal\":");
  if (palIdx < 0) return;

  int pal = seg.substring(palIdx + 6).toInt();
  wledData.pendingPalSync = wledMapPalette(pal);

  WLED_DBG("WLED: pal poll wled=");
  WLED_DBG(pal);
  WLED_DBG(" local=");
  WLED_DBGLN(wledData.pendingPalSync);
}

// ============================================================================
// WLED Emoji Display — sprite slideshow on 32x8 matrix
// ============================================================================
#include "wled_emoji.h"

// ============================================================================
// Poll — called from Core 0 (WiFi task)
// ============================================================================

void pollWledDisplay() {
  // Emoji display mode — continuous DDP stream (gated by ownership)
  if (wledStreamAllowed) {
    wledEmojiUpdate();
  }
  if (wledEmoji.active) return;  // emoji mode owns WLED, skip normal logic

  // ---- Hold complete → advance word or restore ----
  if (wledData.phase == WLED_PHASE_HOLD && millis() >= wledData.phaseEndMs) {
    // New content already queued — skip restore, let new frame take priority
    if (wledData.sendState == WLED_FRAME_REQUESTED) {
      WLED_DBGLN("WLED: hold expired but new frame queued — skipping restore");
      wledData.phase = WLED_PHASE_NONE;
      wledData.wordCount = 0;
      wledData.restoreAtMs = 0;
      // Falls through to FRAME_REQUESTED handling below
    }
    // Multi-word sequence: advance to next word
    else if (wledData.wordCount > 0 && wledData.currentWord + 1 < wledData.wordCount) {
      wledData.currentWord++;
      wledPixelClear();
      wledPixelDrawText(wledData.words[wledData.currentWord],
                        wledData.r, wledData.g, wledData.b);

      if (wledSendDDP()) {
        wledData.phaseEndMs = millis() + wledData.perWordDurationMs;
        WLED_DBG("WLED: word ");
        WLED_DBG(wledData.currentWord);
        WLED_DBG("/");
        WLED_DBGLN(wledData.wordCount);
      } else {
        // DDP failed — fall through to restore
        wledData.phase = WLED_PHASE_NONE;
        wledData.restoreAtMs = millis();
      }
    } else {
      // No more words — restore
      wledData.wordCount = 0;
      wledData.phase = WLED_PHASE_NONE;
      wledData.restoreAtMs = millis();
      WLED_DBGLN("WLED: hold done, restoring");
    }
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
        WLED_DBGLN("WLED: restored");
        extern void meshSetWledActive(bool);
        meshSetWledActive(false);
      } else {
        WLED_DBGLN("WLED: restore failed");
        extern void meshSetWledActive(bool);
        meshSetWledActive(false);
      }
      // Keep hasSavedState=true — WLED just resumed these exact values, so
      // the saved state is still valid for the next say (skips inline capture).
      // The idle poll will refresh it in the background within 1 second.
    } else {
      // No saved state to restore — just clear wledActive
      extern void meshSetWledActive(bool);
      meshSetWledActive(false);
    }

    // Resume scheduled content after speech completes
    extern void schedOnSpeechEnd();
    schedOnSpeechEnd();
  }

  // ---- Send DDP frame (checked before idle poll — urgent requests skip blocking HTTP) ----
  if (wledData.sendState != WLED_FRAME_REQUESTED) {
    // Skip idle poll if WLED unreachable (30s backoff, same as DDP sends)
    if (!wledData.reachable &&
        millis() - wledData.lastFailTime < WLED_RETRY_BACKOFF_MS) {
      return;
    }
    // Nothing urgent — run periodic state capture so hasSavedState is ready for next say
    if (wledData.enabled && wledData.ip[0] != '\0' && sysStatus.staConnected &&
        wledData.phase == WLED_PHASE_NONE &&
        millis() - wledData.lastPalPollMs >= WLED_PAL_POLL_INTERVAL_MS) {
      wledData.lastPalPollMs = millis();
      bool ok = wledCaptureState();  // captures palette AND full segment state
      wledData.reachable = ok;
      if (!ok) wledData.lastFailTime = millis();
    }
    return;
  }

  // Mesh coordination: defer if a peer is sending DDP.
  // Don't consume the request — leave sendState as WLED_FRAME_REQUESTED
  // so we retry on the next poll cycle (~2ms) until the peer finishes.
  extern bool meshAnyPeerWledActiveForIP(uint32_t);
  extern uint32_t wledGetIPAsU32();
  extern void meshSetWledActive(bool);
  if (meshAnyPeerWledActiveForIP(wledGetIPAsU32())) {
    WLED_DBGLN("WLED: deferred — mesh peer active");
    return;  // sendState stays WLED_FRAME_REQUESTED → retries next cycle
  }

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
    WLED_DBG("WLED: replacing frame \"");
    WLED_DBG(wledData.textBuffer);
    WLED_DBGLN("\"");
  } else {
    // First frame — capture current state for later restore
    if (wledCaptureState()) {
      WLED_DBG("WLED: captured, sending DDP \"");
    } else {
      WLED_DBG("WLED: capture failed, sending DDP \"");
    }
    WLED_DBG(wledData.textBuffer);
    WLED_DBGLN("\"");
  }

  meshSetWledActive(true);

  // Send pixel buffer via DDP
  if (wledSendDDP()) {
    wledData.reachable = true;

    // Hold: frame displayed, wait for duration, then restore
    wledData.phase = WLED_PHASE_HOLD;
    wledData.phaseEndMs = millis() + wledData.frameDurationMs;
    wledData.restoreAtMs = 0;

    WLED_DBG("WLED: DDP frame for ");
    WLED_DBG(wledData.frameDurationMs);
    WLED_DBGLN("ms");
  } else {
    wledData.reachable = false;
    wledData.lastFailTime = millis();
    WLED_DBGLN("WLED: DDP send failed");
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

void wledSetHologram(bool on) {
  wledData.hologramMode = on;
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
  extern bool hologramMirrorLCD;
  hologramMirrorLCD = on;
  #endif
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
  json += ",\"hologram\":";
  json += wledData.hologramMode ? "true" : "false";
  json += "}";
  return json;
}

// True when WLED is configured, connected, and reachable — suppresses local palette auto-cycle.
inline bool wledIsSyncing() {
  return wledData.enabled && wledData.ip[0] != '\0' &&
         sysStatus.staConnected && wledData.reachable;
}

// WLED toggle mode state (emoji/weather alternation)
static uint8_t wledTogglePhase = 0;  // 0=emoji, 1=weather
static unsigned long wledToggleNextMs = 0;
#define WLED_TOGGLE_INTERVAL_MS 30000

// Returns mapped local palette index if a WLED palette sync is pending, else -1.
// Clears the pending flag — call once per loop iteration from Core 1.
int8_t wledConsumePalSync() {
  int8_t v = wledData.pendingPalSync;
  if (v >= 0) wledData.pendingPalSync = -1;
  return v;
}

#endif // WLED_DISPLAY_H
