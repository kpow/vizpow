#ifndef WLED_WEATHER_VIEW_H
#define WLED_WEATHER_VIEW_H

#include "wled_display.h"
#include "weather_data.h"

// Timing
#define WLED_WEATHER_CARD_MS        4000   // How long each weather card is fully visible
#define WLED_WEATHER_GAP_MS         2000   // How long WLED shows its own effect between cards
#define WLED_WEATHER_NUM_CARDS      5

// Fade: 8 steps × 50ms = ~400ms per transition
#define WLED_WEATHER_FADE_STEPS     8
#define WLED_WEATHER_FADE_STEP_MS   50
#define WLED_WEATHER_FADE_FRAME_MS  150    // DDP hold per fade frame (> step interval)

// ─── State machine ────────────────────────────────────────────────────────────
//
//   IDLE
//    │  (first update, build card 0)
//    ▼
//   FADE_IN  ──────────────►  SHOWING  ──────────────►  FADE_OUT
//    ▲   (scale 0→255, DDP)             (keepalive)      (scale 255→0, DDP)
//    │                                                         │
//    │   (build next card)                                     │ (last frame hold=150ms
//    └────────────── WLED_GAP ◄──────────────────────────────┘   → auto-restore fires;
//                   (no DDP; WLED                                   wait GAP_MS)
//                    runs freely)
//
// ─────────────────────────────────────────────────────────────────────────────

enum WledWeatherState : uint8_t {
  WWEATHER_IDLE     = 0,
  WWEATHER_FADE_IN,
  WWEATHER_SHOWING,
  WWEATHER_FADE_OUT,
  WWEATHER_WLED_GAP,
};

static uint8_t          wledWeatherCard       = 0;
static WledWeatherState wledWeatherState       = WWEATHER_IDLE;
static uint32_t         wledWeatherStepMs      = 0;
static uint8_t          wledWeatherFadeStep    = 0;
static uint32_t         wledWeatherKeepaliveMs = 0;

// Full-brightness snapshot of current card — source for brightness-scaled fades
static uint8_t          wledWeatherCardBuf[WLED_PIXEL_BYTES];

// ─── Helpers ──────────────────────────────────────────────────────────────────

inline void wledTempColor(float f, uint8_t& r, uint8_t& g, uint8_t& b) {
  if      (f < 32)  { r=0;   g=0;   b=255; }
  else if (f < 50)  { r=0;   g=255; b=255; }
  else if (f < 65)  { r=0;   g=255; b=0;   }
  else if (f < 80)  { r=255; g=255; b=0;   }
  else if (f < 95)  { r=255; g=128; b=0;   }
  else              { r=255; g=0;   b=0;   }
}

// Render the current card into the pixel buffer and snapshot at full brightness
static void wledWeatherBuildCard() {
  uint8_t r, g, b;
  char buf[9];
  wledPixelClear();

  switch (wledWeatherCard) {
    case 0:
      snprintf(buf, sizeof(buf), "%dF", (int)weatherData.current.tempF);
      wledTempColor(weatherData.current.tempF, r, g, b);
      wledPixelDrawText(buf, r, g, b);
      break;
    case 1:
      strncpy(buf, weatherData.current.conditionText, 8);
      buf[8] = '\0';
      wledPixelDrawText(buf, 255, 255, 255);
      break;
    case 2: case 3: case 4: {
      uint8_t d = wledWeatherCard - 2;
      snprintf(buf, sizeof(buf), "%s %dF",
               weatherData.forecast[d].dayName,
               (int)weatherData.forecast[d].highF);
      wledTempColor(weatherData.forecast[d].highF, r, g, b);
      wledPixelDrawText(buf, r, g, b);
      break;
    }
  }

  memcpy(wledWeatherCardBuf, wledData.pixelBuffer, WLED_PIXEL_BYTES);
}

// Scale the card snapshot into the pixel buffer and queue a DDP frame
static void wledWeatherSendScaled(uint8_t scale, uint16_t holdMs) {
  for (uint16_t i = 0; i < WLED_PIXEL_BYTES; i++) {
    wledData.pixelBuffer[i] = ((uint16_t)wledWeatherCardBuf[i] * scale) >> 8;
  }
  wledQueueFrame(holdMs);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void wledWeatherViewReset() {
  wledWeatherCard       = 0;
  wledWeatherState      = WWEATHER_IDLE;
  wledWeatherStepMs     = 0;
  wledWeatherFadeStep   = 0;
  wledWeatherKeepaliveMs = 0;
}

// Call on info mode exit: if a card is visible, linger 2.5s then let restore fire.
// If we're already in the WLED gap, nothing to do — WLED is already restored.
void wledWeatherViewOnExit() {
  if (!weatherData.valid) return;
  if (wledWeatherState == WWEATHER_WLED_GAP ||
      wledWeatherState == WWEATHER_IDLE) return;
  wledWeatherSendScaled(255, 2500);
}

void wledWeatherViewUpdate() {
  if (!weatherData.valid) return;

  uint32_t now = millis();

  switch (wledWeatherState) {

    // ── IDLE: first call — build card 0 and start fading in immediately ───────
    case WWEATHER_IDLE:
      wledWeatherBuildCard();
      wledWeatherFadeStep = 0;
      wledWeatherStepMs   = 0;   // 0 forces immediate first step (now - 0 > STEP_MS)
      wledWeatherState    = WWEATHER_FADE_IN;
      break;

    // ── FADE_IN: scale 0→255, send DDP frames every FADE_STEP_MS ─────────────
    case WWEATHER_FADE_IN:
      if (now - wledWeatherStepMs < WLED_WEATHER_FADE_STEP_MS) return;
      wledWeatherStepMs = now;
      {
        // Ramp: ~31, 63, 95, 127, 159, 191, 223, 255
        uint8_t scale = ((uint16_t)(wledWeatherFadeStep + 1) * 255) / WLED_WEATHER_FADE_STEPS;
        wledWeatherSendScaled(scale, WLED_WEATHER_FADE_FRAME_MS);
        wledWeatherFadeStep++;
        if (wledWeatherFadeStep >= WLED_WEATHER_FADE_STEPS) {
          wledWeatherKeepaliveMs = now;
          wledWeatherStepMs      = now;
          wledWeatherState       = WWEATHER_SHOWING;
          wledWeatherSendScaled(255, 2500);   // initial full-brightness hold
        }
      }
      break;

    // ── SHOWING: keep WLED in realtime, wait CARD_MS then fade out ───────────
    case WWEATHER_SHOWING:
      // Keepalive every 2s — WLED exits realtime after 2.5s without DDP frames
      if (now - wledWeatherKeepaliveMs >= 2000) {
        wledWeatherKeepaliveMs = now;
        wledWeatherSendScaled(255, 2500);
      }
      if (now - wledWeatherStepMs < WLED_WEATHER_CARD_MS) return;
      wledWeatherFadeStep = 0;
      wledWeatherStepMs   = now;
      wledWeatherState    = WWEATHER_FADE_OUT;
      break;

    // ── FADE_OUT: scale 255→0, last frame's short hold triggers auto-restore ─
    case WWEATHER_FADE_OUT:
      if (now - wledWeatherStepMs < WLED_WEATHER_FADE_STEP_MS) return;
      wledWeatherStepMs = now;
      {
        // Ramp: ~223, 191, 159, 127, 95, 63, 31, 0
        uint8_t scale = ((uint16_t)(WLED_WEATHER_FADE_STEPS - wledWeatherFadeStep - 1) * 255)
                        / WLED_WEATHER_FADE_STEPS;
        wledWeatherFadeStep++;
        if (wledWeatherFadeStep >= WLED_WEATHER_FADE_STEPS) {
          // Send a black frame with a short hold — when it expires (~150ms),
          // pollWledDisplay() fires the HTTP restore and WLED resumes its effect.
          wledWeatherSendScaled(0, WLED_WEATHER_FADE_FRAME_MS);
          // Advance card and sit in gap for GAP_MS
          wledWeatherCard = (wledWeatherCard + 1) % WLED_WEATHER_NUM_CARDS;
          wledWeatherStepMs = now;
          wledWeatherState  = WWEATHER_WLED_GAP;
        } else {
          wledWeatherSendScaled(scale, WLED_WEATHER_FADE_FRAME_MS);
        }
      }
      break;

    // ── WLED_GAP: no DDP — WLED runs its own effect freely ───────────────────
    case WWEATHER_WLED_GAP:
      if (now - wledWeatherStepMs < WLED_WEATHER_GAP_MS) return;
      // Gap done — build next card and fade it in
      wledWeatherBuildCard();
      wledWeatherFadeStep = 0;
      wledWeatherStepMs   = 0;   // force immediate first fade-in step
      wledWeatherState    = WWEATHER_FADE_IN;
      break;
  }
}

#endif // WLED_WEATHER_VIEW_H
