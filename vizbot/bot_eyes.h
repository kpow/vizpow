#ifndef BOT_EYES_H
#define BOT_EYES_H

#include <Arduino.h>
#include "config.h"
#include "bot_faces.h"

// ============================================================================
// Bot Face Rendering — eyes, brows, mouth
// ============================================================================
// Eyes and brows come from the parametric eye model (bot_eye_model.h): one
// eye shape per side with lids, driven by springs, drawn as one vertical span
// per pixel column. Mouths are drawn here from the BotFaceState tween.
//
// Art direction: solid eye shapes on black. Pupils are black cut-outs.
// Maximum 4 colors on screen. Bold, simple, high contrast.
// ============================================================================

// External GFX object from display_lcd.h
extern GfxDevice *gfx;

// External IMU data from vizpow.ino
extern float accelX, accelY, accelZ;

// Colors (RGB565)
#define BOT_COLOR_BG      0x0000  // Black background
#define BOT_COLOR_WHITE   0xFFFF  // Eye whites (default)
#define BOT_COLOR_PUPIL   0x0000  // Pupils (same as BG — cuts into white)
#define BOT_COLOR_ACCENT  0x07FF  // Cyan accent (for effects)

// Configurable face color (can be changed via palette)
uint16_t botFaceColor = BOT_COLOR_WHITE;

// Stroke thickness for face outline on ambient backgrounds
#define BOT_STROKE_PX 5

// External background style (from bot_mode.h) — used for stroke rendering
extern uint8_t botBackgroundStyle;

#include "bot_eye_model.h"

// ============================================================================
// IMU Look Input
// ============================================================================
// Tilt becomes the eye model's look pad (-1..1): while engaged, the gaze
// pursues it (±0.35 R across, ±0.25 R up/down).

struct BotIMUTracker {
  float smoothX, smoothY;
  static constexpr float SMOOTH_FACTOR = 0.2f;    // Low-pass filter (0-1, lower = smoother)
  static constexpr float TILT_SCALE = 35.0f;      // Pad units x 24 per g of tilt (high = sensitive)
  static constexpr float MAX_OFFSET = 24.0f;      // Tilt that reaches the edge of the pad

  void init() {
    smoothX = 0;
    smoothY = 0;
  }

  void update(float &padX, float &padY) {
    // Map accelerometer tilt to the look pad
    // Neutral position is standing upright (90 degrees), so subtract 1g from
    // the vertical axis to zero out gravity when the device faces the user
    float rawX = constrain(-accelY * TILT_SCALE, -MAX_OFFSET, MAX_OFFSET);
    float rawY = constrain((accelX - 1.0f) * TILT_SCALE, -MAX_OFFSET, MAX_OFFSET);

    // Smooth with exponential filter
    smoothX += (rawX - smoothX) * SMOOTH_FACTOR;
    smoothY += (rawY - smoothY) * SMOOTH_FACTOR;

    padX = smoothX / MAX_OFFSET;
    padY = smoothY / MAX_OFFSET;
  }
};

// ============================================================================
// Drawing helpers
// ============================================================================

// Draw a thick line (mouths, info mode brows)
void drawThickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness, uint16_t color) {
  for (int16_t t = -thickness / 2; t <= thickness / 2; t++) {
    // Offset perpendicular to line direction
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) len = 1.0f;
    float nx = -dy / len * t;
    float ny = dx / len * t;
    gfx->drawLine(x0 + (int16_t)nx, y0 + (int16_t)ny,
                   x1 + (int16_t)nx, y1 + (int16_t)ny, color);
  }
}

// ============================================================================
// Mouth
// ============================================================================

void drawBotMouth(const BotFaceState &face, int16_t mouthCX, int16_t mouthCY, bool drawStroke, uint16_t bgColor) {
  switch (face.mouthType) {
    case MOUTH_NONE:
      break;

    case MOUTH_LINE: {
      if (drawStroke) {
        drawThickLine(mouthCX - face.mouthWidth, mouthCY,
                       mouthCX + face.mouthWidth, mouthCY, 3 + BOT_STROKE_PX * 2, BOT_COLOR_BG);
      }
      drawThickLine(mouthCX - face.mouthWidth, mouthCY,
                     mouthCX + face.mouthWidth, mouthCY, 3, botFaceColor);
      break;
    }

    case MOUTH_SMILE: {
      // Smile: edges at mouthCY, center dips down to mouthCY + mouthCurve
      if (drawStroke) {
        for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
          float t = (float)x / face.mouthWidth;
          int16_t y = (int16_t)(face.mouthCurve * (1.0f - t * t));
          gfx->fillCircle(mouthCX + x, mouthCY + y, 2 + BOT_STROKE_PX, BOT_COLOR_BG);
        }
      }
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = (int16_t)(face.mouthCurve * (1.0f - t * t));
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      break;
    }

    case MOUTH_FROWN: {
      // Frown: edges at mouthCY, center rises to mouthCY - mouthCurve
      if (drawStroke) {
        for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
          float t = (float)x / face.mouthWidth;
          int16_t y = -(int16_t)(face.mouthCurve * (1.0f - t * t));
          gfx->fillCircle(mouthCX + x, mouthCY + y, 2 + BOT_STROKE_PX, BOT_COLOR_BG);
        }
      }
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = -(int16_t)(face.mouthCurve * (1.0f - t * t));
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      break;
    }

    case MOUTH_OPEN_O: {
      if (drawStroke) {
        gfx->fillCircle(mouthCX, mouthCY, face.mouthCurve + BOT_STROKE_PX, BOT_COLOR_BG);
      }
      gfx->fillCircle(mouthCX, mouthCY, face.mouthCurve, botFaceColor);
      gfx->fillCircle(mouthCX, mouthCY, face.mouthCurve - 3, BOT_COLOR_BG);
      break;
    }

    case MOUTH_GRIN: {
      // Grin: smile curve with teeth line across
      if (drawStroke) {
        for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
          float t = (float)x / face.mouthWidth;
          int16_t y = (int16_t)(face.mouthCurve * (1.0f - t * t));
          gfx->fillCircle(mouthCX + x, mouthCY + y, 2 + BOT_STROKE_PX, BOT_COLOR_BG);
        }
      }
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = (int16_t)(face.mouthCurve * (1.0f - t * t));
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      drawThickLine(mouthCX - face.mouthWidth + 4, mouthCY + 2,
                     mouthCX + face.mouthWidth - 4, mouthCY + 2, 2, BOT_COLOR_BG);
      break;
    }

    case MOUTH_WAVY: {
      if (drawStroke) {
        for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
          float t = (float)x / face.mouthWidth;
          int16_t y = (int16_t)(sinf(t * PI * 3.0f) * face.mouthCurve);
          gfx->fillCircle(mouthCX + x, mouthCY + y, 2 + BOT_STROKE_PX, BOT_COLOR_BG);
        }
      }
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = (int16_t)(sinf(t * PI * 3.0f) * face.mouthCurve);
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      break;
    }

    case MOUTH_SMIRK: {
      // Smirk: asymmetric — left corner dips, right corner stays up
      if (drawStroke) {
        for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
          float t = (float)x / face.mouthWidth;
          float normalized = (t + 1.0f) / 2.0f;
          int16_t y = (int16_t)(face.mouthCurve * (1.0f - normalized) * (1.0f - normalized));
          gfx->fillCircle(mouthCX + x, mouthCY + y, 2 + BOT_STROKE_PX, BOT_COLOR_BG);
        }
      }
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        float normalized = (t + 1.0f) / 2.0f;
        int16_t y = (int16_t)(face.mouthCurve * (1.0f - normalized) * (1.0f - normalized));
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      break;
    }

    case MOUTH_TEETH: {
      // Smile curve + white rect teeth bar with black vertical gaps
      if (drawStroke) {
        for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
          float t = (float)x / face.mouthWidth;
          int16_t y = (int16_t)(face.mouthCurve * (1.0f - t * t));
          gfx->fillCircle(mouthCX + x, mouthCY + y, 2 + BOT_STROKE_PX, BOT_COLOR_BG);
        }
      }
      // Draw the smile curve (lip line)
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = (int16_t)(face.mouthCurve * (1.0f - t * t));
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      // Teeth bar: below the lip edges
      int16_t teethTop = mouthCY + 2;
      int16_t teethH = 8;
      int16_t teethW = face.mouthWidth * 3 / 2;
      if (drawStroke) {
        gfx->fillRect(mouthCX - teethW - BOT_STROKE_PX, teethTop - BOT_STROKE_PX,
                       teethW * 2 + BOT_STROKE_PX * 2, teethH + BOT_STROKE_PX * 2, BOT_COLOR_BG);
      }
      gfx->fillRect(mouthCX - teethW, teethTop, teethW * 2, teethH, botFaceColor);
      // Black vertical gaps between teeth
      for (int16_t tx = mouthCX - teethW + 6; tx < mouthCX + teethW; tx += 7) {
        gfx->drawFastVLine(tx, teethTop, teethH, BOT_COLOR_BG);
      }
      break;
    }

    case MOUTH_TONGUE: {
      // Bottom semi-circle mouth + pink/red ellipse tongue
      int16_t mouthR = face.mouthCurve + 2;
      if (drawStroke) {
        gfx->fillCircle(mouthCX, mouthCY, mouthR + BOT_STROKE_PX, BOT_COLOR_BG);
      }
      // Open mouth (half circle — top half is face color, bottom is dark)
      gfx->fillCircle(mouthCX, mouthCY, mouthR, botFaceColor);
      gfx->fillCircle(mouthCX, mouthCY, mouthR - 3, BOT_COLOR_BG);
      // Only keep bottom half open
      gfx->fillRect(mouthCX - mouthR - 1, mouthCY - mouthR - 1, mouthR * 2 + 2, mouthR, bgColor);
      // Re-draw the lip line at top
      drawThickLine(mouthCX - face.mouthWidth, mouthCY, mouthCX + face.mouthWidth, mouthCY, 3, botFaceColor);
      // Pink tongue (0xF8A0 = warm pink in RGB565)
      int16_t tongueY = mouthCY + mouthR / 2;
      gfx->fillEllipse(mouthCX, tongueY, face.mouthWidth / 2, mouthR / 3 + 1, 0xF8A0);
      break;
    }

    case MOUTH_ZIGZAG: {
      // Zigzag line (6 segments), nervous/electric feel
      int16_t segW = face.mouthWidth * 2 / 6;  // Width per segment
      int16_t startX = mouthCX - face.mouthWidth;
      if (drawStroke) {
        for (int i = 0; i < 6; i++) {
          int16_t x0 = startX + i * segW;
          int16_t x1 = x0 + segW;
          int16_t y0 = mouthCY + ((i % 2 == 0) ? -face.mouthCurve : face.mouthCurve);
          int16_t y1 = mouthCY + ((i % 2 == 0) ? face.mouthCurve : -face.mouthCurve);
          drawThickLine(x0, y0, x1, y1, 3 + BOT_STROKE_PX * 2, BOT_COLOR_BG);
        }
      }
      for (int i = 0; i < 6; i++) {
        int16_t x0 = startX + i * segW;
        int16_t x1 = x0 + segW;
        int16_t y0 = mouthCY + ((i % 2 == 0) ? -face.mouthCurve : face.mouthCurve);
        int16_t y1 = mouthCY + ((i % 2 == 0) ? face.mouthCurve : -face.mouthCurve);
        drawThickLine(x0, y0, x1, y1, 3, botFaceColor);
      }
      break;
    }

    case MOUTH_POUT: {
      // Small filled circle with inner dark hole (pucker/kiss)
      int16_t poutR = face.mouthCurve;
      if (drawStroke) {
        gfx->fillCircle(mouthCX, mouthCY, poutR + BOT_STROKE_PX, BOT_COLOR_BG);
      }
      gfx->fillCircle(mouthCX, mouthCY, poutR, botFaceColor);
      gfx->fillCircle(mouthCX, mouthCY, poutR - 3, BOT_COLOR_BG);
      break;
    }

    case MOUTH_WHISTLE: {
      // Fixed tiny O shape (radius ~5px)
      int16_t whistleR = 5;
      if (drawStroke) {
        gfx->fillCircle(mouthCX, mouthCY, whistleR + BOT_STROKE_PX, BOT_COLOR_BG);
      }
      gfx->fillCircle(mouthCX, mouthCY, whistleR, botFaceColor);
      gfx->fillCircle(mouthCX, mouthCY, whistleR - 2, BOT_COLOR_BG);
      break;
    }

    case MOUTH_FLAT_FROWN: {
      // Two angled lines forming shallow inverted-V
      int16_t halfW = face.mouthWidth;
      int16_t dip = face.mouthCurve / 2;  // Subtle dip at center
      if (drawStroke) {
        drawThickLine(mouthCX - halfW, mouthCY, mouthCX, mouthCY + dip, 3 + BOT_STROKE_PX * 2, BOT_COLOR_BG);
        drawThickLine(mouthCX, mouthCY + dip, mouthCX + halfW, mouthCY, 3 + BOT_STROKE_PX * 2, BOT_COLOR_BG);
      }
      drawThickLine(mouthCX - halfW, mouthCY, mouthCX, mouthCY + dip, 3, botFaceColor);
      drawThickLine(mouthCX, mouthCY + dip, mouthCX + halfW, mouthCY, 3, botFaceColor);
      break;
    }
  }
}

// ============================================================================
// Main Face Render
// ============================================================================

// Where renderBotFace() centers the face, and its scale (1 = eye R of
// BOT_EYE_R px). The touch UI and the album art corner move and shrink it
// (passing a scaled copy of the face for the mouth); everything else leaves
// it at the board default.
int16_t botFaceDrawCX = BOT_FACE_CX;
int16_t botFaceDrawCY = BOT_FACE_CY;
float botFaceDrawScale = 1.0f;

// Render cost, read and reset by GET /state (web_server.h)
struct BotFaceRenderStats {
  uint32_t sumUs, count, maxUs;   // per face render since the last read
  uint32_t skipped;               // frames pendant mode left on screen (face at rest)
};
BotFaceRenderStats botFaceRenderStats = {0, 0, 0, 0};

// Render the complete face: eye model (eyes + brows), then the mouth
void renderBotFace(BotFaceState &face, uint16_t bgColor) {
  if (gfx == nullptr) return;
  uint32_t t0 = micros();

  int16_t cx = botFaceDrawCX;
  int16_t cy = botFaceDrawCY;

  // ---- Black stroke behind face elements (only on ambient background) ----
  bool drawStroke = (botBackgroundStyle == 4);

  botEyes.render(cx, cy, botFaceDrawScale, botFaceColor, drawStroke, millis());
  drawBotMouth(face, cx, cy + face.mouthOffsetY, drawStroke, bgColor);

  uint32_t us = micros() - t0;
  botFaceRenderStats.sumUs += us;
  botFaceRenderStats.count++;
  if (us > botFaceRenderStats.maxUs) botFaceRenderStats.maxUs = us;
}

#endif // BOT_EYES_H
