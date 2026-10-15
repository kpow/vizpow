#ifndef ALBUM_ART_H
#define ALBUM_ART_H

// ============================================================================
// Album art from vizbrain
// ============================================================================
// POST /bot/art?ms=8000 with one multipart file field: a baseline JPEG, at
// most ART_SIZE x ART_SIZE (vizbrain sends exactly 240x240). The brain pushes
// the picture, so the bot never opens a TLS connection for it.
//
// The web task (core 0) only stores the bytes. The render loop (core 1)
// decodes them once into a PSRAM sprite and draws that instead of the face
// until it expires, a voice turn starts (he's being talked to), or
// GET /bot/art/hide.
// ============================================================================

#ifdef VOICE_ENABLED

#include <WebServer.h>
#include "display_lcd.h"

extern WebServer server;

#define ART_SIZE        240
#define ART_MAX_BYTES   (96 * 1024)
#define ART_DEFAULT_MS  8000
#define ART_MAX_MS      30000

static uint8_t* artBuf = nullptr;            // PSRAM, ART_MAX_BYTES, allocated on first upload
static size_t artLen = 0;
static bool artTooBig = false;
static volatile bool artPending = false;      // bytes waiting for the render loop
static volatile uint32_t artPendingMs = 0;
static volatile bool artHideRequested = false;
static LGFX_Sprite* artSprite = nullptr;      // the decoded picture, PSRAM
static uint32_t artUntil = 0;                 // millis() when it goes away; 0 = not showing

struct ArtStats {
  uint32_t shown = 0, rejected = 0, lastDecodeMs = 0;
} artStats;

static void handleArtUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    artTooBig = false;
    artLen = 0;
    if (artPending) { artTooBig = true; return; }   // the last one isn't drawn yet
    if (!artBuf) artBuf = (uint8_t*)heap_caps_malloc(ART_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (!artBuf) artTooBig = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (artTooBig) return;
    if (artLen + up.currentSize > ART_MAX_BYTES) { artTooBig = true; return; }
    memcpy(artBuf + artLen, up.buf, up.currentSize);
    artLen += up.currentSize;
  }
}

static void handleArtDone() {
  bool jpeg = !artTooBig && artLen > 4 && artBuf[0] == 0xFF && artBuf[1] == 0xD8;
  if (!jpeg) {
    artStats.rejected++;
    server.send(artPending ? 409 : 400, "application/json",
                artPending ? "{\"ok\":false,\"error\":\"busy\"}" : "{\"ok\":false,\"error\":\"not a JPEG under 96 KB\"}");
    return;
  }
  uint32_t ms = server.hasArg("ms") ? (uint32_t)server.arg("ms").toInt() : ART_DEFAULT_MS;
  artPendingMs = constrain(ms, 1000, ART_MAX_MS);
  artPending = true;   // last: the render loop reads artBuf/artLen only after this
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleArtHide() {
  artHideRequested = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

// Called by the render loop after beginCanvas(). True = the picture was drawn
// this frame, so the face is skipped.
static bool artRender() {
  if (artPending) {
    uint32_t t0 = millis();
    if (!artSprite) {
      artSprite = new LGFX_Sprite(&M5.Display);
      artSprite->setPsram(true);
      artSprite->setColorDepth(16);
      if (!artSprite->createSprite(ART_SIZE, ART_SIZE)) { delete artSprite; artSprite = nullptr; }
    }
    bool ok = false;
    if (artSprite) {
      artSprite->fillScreen(TFT_BLACK);
      ok = artSprite->drawJpg(artBuf, artLen, 0, 0, ART_SIZE, ART_SIZE);
    }
    artStats.lastDecodeMs = millis() - t0;
    if (ok) { artUntil = millis() + artPendingMs; artStats.shown++; }
    else artStats.rejected++;
    artPending = false;
  }
  if (artHideRequested) { artHideRequested = false; artUntil = 0; }
  if (!artUntil || !artSprite) return false;
  if ((int32_t)(millis() - artUntil) >= 0) { artUntil = 0; return false; }
  gfx->fillScreen(TFT_BLACK);
  const int32_t x = (LCD_WIDTH - ART_SIZE) / 2, y = (LCD_HEIGHT - ART_SIZE) / 2;
  if (_dp_canvas_active) artSprite->pushSprite(_dp_canvas, x, y);
  else artSprite->pushSprite(&M5.Display, x, y);
  return true;
}

static inline void artHide() { artHideRequested = true; }
static inline bool artShowing() { return artUntil != 0; }

#else

static inline bool artRender() { return false; }
static inline void artHide() {}
static inline bool artShowing() { return false; }

#endif  // VOICE_ENABLED
#endif  // ALBUM_ART_H
