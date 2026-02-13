#ifndef BOT_EYES_H
#define BOT_EYES_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "config.h"
#include "bot_faces.h"

// ============================================================================
// Bot Eye Animation & Rendering Engine
// ============================================================================
// Handles all procedural eye drawing, blink timing, idle look-around,
// IMU pupil tracking, and special eye modes (hearts, spirals, X-eyes, etc.)
//
// Art direction: Large white ellipses on black. Pupils are black circles
// that "cut into" the white. Brows are thick arcs/bars above eyes.
// Maximum 4 colors on screen. Bold, simple, high contrast.
// ============================================================================

// External GFX object from display_lcd.h
extern Arduino_GFX *gfx;

// External IMU data from vizpow.ino
extern float accelX, accelY, accelZ;

// Colors (RGB565)
#define BOT_COLOR_BG      0x0000  // Black background
#define BOT_COLOR_WHITE   0xFFFF  // Eye whites (default)
#define BOT_COLOR_PUPIL   0x0000  // Pupils (same as BG — cuts into white)
#define BOT_COLOR_ACCENT  0x07FF  // Cyan accent (for effects)

// Configurable face color (can be changed via palette)
uint16_t botFaceColor = BOT_COLOR_WHITE;

// ============================================================================
// Blink System
// ============================================================================

struct BotBlinkState {
  unsigned long nextBlinkTime;
  unsigned long blinkStartTime;
  bool blinking;
  bool doubleBlink;
  uint8_t doubleBlinkPhase;   // 0 = first blink, 1 = gap, 2 = second blink

  // Blink timing
  static const uint16_t BLINK_DURATION = 150;      // ms for one blink
  static const uint16_t DOUBLE_BLINK_GAP = 100;    // ms between double blinks
  static const uint16_t BLINK_MIN_INTERVAL = 3000;  // Min time between blinks
  static const uint16_t BLINK_MAX_INTERVAL = 7000;  // Max time between blinks

  void init() {
    nextBlinkTime = millis() + random(BLINK_MIN_INTERVAL, BLINK_MAX_INTERVAL);
    blinking = false;
    doubleBlink = false;
    doubleBlinkPhase = 0;
  }

  // Returns blink amount 0.0 (open) to 1.0 (closed)
  float update() {
    unsigned long now = millis();

    if (!blinking) {
      if (now >= nextBlinkTime) {
        blinking = true;
        blinkStartTime = now;
        doubleBlink = (random(100) < 15);  // 15% chance of double blink
        doubleBlinkPhase = 0;
      }
      return 0.0f;
    }

    // Currently blinking
    unsigned long elapsed = now - blinkStartTime;

    if (!doubleBlink) {
      // Single blink: triangle wave over BLINK_DURATION
      if (elapsed >= BLINK_DURATION) {
        blinking = false;
        nextBlinkTime = now + random(BLINK_MIN_INTERVAL, BLINK_MAX_INTERVAL);
        return 0.0f;
      }
      float half = BLINK_DURATION / 2.0f;
      float t = elapsed / half;
      return (t < 1.0f) ? t : (2.0f - t);
    }

    // Double blink
    uint16_t totalFirst = BLINK_DURATION;
    uint16_t gapEnd = totalFirst + DOUBLE_BLINK_GAP;
    uint16_t totalAll = gapEnd + BLINK_DURATION;

    if (elapsed >= totalAll) {
      blinking = false;
      nextBlinkTime = now + random(BLINK_MIN_INTERVAL, BLINK_MAX_INTERVAL);
      return 0.0f;
    }

    if (elapsed < totalFirst) {
      float half = BLINK_DURATION / 2.0f;
      float t = elapsed / half;
      return (t < 1.0f) ? t : (2.0f - t);
    }

    if (elapsed < gapEnd) {
      return 0.0f;  // Gap between blinks
    }

    // Second blink
    float e2 = elapsed - gapEnd;
    float half = BLINK_DURATION / 2.0f;
    float t = e2 / half;
    return (t < 1.0f) ? t : (2.0f - t);
  }
};

// ============================================================================
// Idle Look-Around System
// ============================================================================

struct BotLookAround {
  int16_t currentX, currentY;
  int16_t targetX, targetY;
  unsigned long moveStartTime;
  unsigned long nextMoveTime;
  uint16_t moveDuration;

  static const uint16_t LOOK_MIN_INTERVAL = 800;
  static const uint16_t LOOK_MAX_INTERVAL = 2500;
  static const int16_t LOOK_MAX_OFFSET = 20;       // Max pixel offset
  static const uint16_t MOVE_DURATION_MIN = 200;
  static const uint16_t MOVE_DURATION_MAX = 500;

  void init() {
    currentX = currentY = 0;
    targetX = targetY = 0;
    nextMoveTime = millis() + random(1000, 3000);  // Short initial delay
    moveDuration = 400;
    moveStartTime = 0;
  }

  void update(int16_t &outX, int16_t &outY) {
    unsigned long now = millis();

    if (now >= nextMoveTime && moveStartTime == 0) {
      // Start a new look
      targetX = random(-LOOK_MAX_OFFSET, LOOK_MAX_OFFSET + 1);
      targetY = random(-LOOK_MAX_OFFSET / 2, LOOK_MAX_OFFSET / 2 + 1);

      // 15% chance to return to center
      if (random(100) < 15) {
        targetX = 0;
        targetY = 0;
      }

      moveDuration = random(MOVE_DURATION_MIN, MOVE_DURATION_MAX);
      moveStartTime = now;
    }

    if (moveStartTime > 0) {
      unsigned long elapsed = now - moveStartTime;
      if (elapsed >= moveDuration) {
        // Move complete
        currentX = targetX;
        currentY = targetY;
        moveStartTime = 0;
        nextMoveTime = now + random(LOOK_MIN_INTERVAL, LOOK_MAX_INTERVAL);
      } else {
        // Ease-in-out interpolation
        float t = (float)elapsed / moveDuration;
        t = t * t * (3.0f - 2.0f * t);  // Smoothstep
        currentX = (int16_t)(currentX + (targetX - currentX) * t);
        currentY = (int16_t)(currentY + (targetY - currentY) * t);
      }
    }

    outX = currentX;
    outY = currentY;
  }
};

// ============================================================================
// IMU Pupil Tracking
// ============================================================================

struct BotIMUTracker {
  float smoothX, smoothY;
  static constexpr float SMOOTH_FACTOR = 0.2f;    // Low-pass filter (0-1, lower = smoother)
  static constexpr float TILT_SCALE = 35.0f;      // Pixels per g of tilt (high = sensitive)
  static constexpr float MAX_OFFSET = 24.0f;      // Max pupil offset from IMU

  void init() {
    smoothX = 0;
    smoothY = 0;
  }

  void update(int16_t &outX, int16_t &outY) {
    // Map accelerometer tilt to pupil offset
    // accelX tilts left/right, accelY tilts forward/back
    float rawX = constrain(accelY * TILT_SCALE, -MAX_OFFSET, MAX_OFFSET);
    float rawY = constrain(accelX * TILT_SCALE, -MAX_OFFSET, MAX_OFFSET);

    // Smooth with exponential filter
    smoothX += (rawX - smoothX) * SMOOTH_FACTOR;
    smoothY += (rawY - smoothY) * SMOOTH_FACTOR;

    outX = (int16_t)smoothX;
    outY = (int16_t)smoothY;
  }
};

// ============================================================================
// Eye Rendering Functions
// ============================================================================

// Draw a thick line (brow) from angle
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

// Draw a filled heart shape at given center and size
void drawHeart(int16_t cx, int16_t cy, int16_t size, uint16_t color) {
  // Heart from two circles and a triangle
  int16_t r = size * 5 / 12;
  int16_t offsetX = size / 3;

  // Two bumps at top
  gfx->fillCircle(cx - offsetX, cy - r / 3, r, color);
  gfx->fillCircle(cx + offsetX, cy - r / 3, r, color);

  // Triangle pointing down
  gfx->fillTriangle(
    cx - size + 2, cy,
    cx + size - 2, cy,
    cx, cy + size,
    color
  );
}

// Draw a 4-point star at given center and size
void drawStar(int16_t cx, int16_t cy, int16_t outerR, int16_t innerR, uint16_t color) {
  // Draw a simple 4-point star using filled triangles
  // Top point
  gfx->fillTriangle(cx, cy - outerR, cx - innerR, cy, cx + innerR, cy, color);
  // Bottom point
  gfx->fillTriangle(cx, cy + outerR, cx - innerR, cy, cx + innerR, cy, color);
  // Left point
  gfx->fillTriangle(cx - outerR, cy, cx, cy - innerR, cx, cy + innerR, color);
  // Right point
  gfx->fillTriangle(cx + outerR, cy, cx, cy - innerR, cx, cy + innerR, color);
}

// Draw spiral inside an eye (for dizzy state)
void drawSpiral(int16_t cx, int16_t cy, int16_t radius, uint16_t color, float phase) {
  float maxAngle = 3.0f * PI;  // ~1.5 turns
  int steps = 40;
  for (int i = 0; i < steps; i++) {
    float t = (float)i / steps;
    float angle = t * maxAngle + phase;
    float r = t * radius;
    int16_t x = cx + (int16_t)(cosf(angle) * r);
    int16_t y = cy + (int16_t)(sinf(angle) * r);
    gfx->fillCircle(x, y, 2, color);
  }
}

// Draw an X across an eye area
void drawXEye(int16_t cx, int16_t cy, int16_t size, uint16_t color) {
  int16_t half = size / 2;
  int16_t thickness = 5;
  drawThickLine(cx - half, cy - half, cx + half, cy + half, thickness, color);
  drawThickLine(cx - half, cy + half, cx + half, cy - half, thickness, color);
}

// Draw caret eye (^ shape)
void drawCaretEye(int16_t cx, int16_t cy, int16_t width, int16_t height, uint16_t color) {
  int16_t thickness = 5;
  // Left side of caret
  drawThickLine(cx - width, cy + height / 3, cx, cy - height / 3, thickness, color);
  // Right side of caret
  drawThickLine(cx, cy - height / 3, cx + width, cy + height / 3, thickness, color);
}

// ============================================================================
// Previous-frame tracking for flicker-free rendering
// ============================================================================
// Instead of clearing the whole face region each frame (which causes flicker),
// we track where things were drawn last frame and erase only those specific
// small areas before redrawing in the new position.

struct BotPrevFrame {
  // Previous eye bounds (for clearing when eye mode/size changes)
  int16_t eyeW, eyeH;
  BotEyeMode eyeMode;

  // Previous pupil positions (absolute screen coords)
  int16_t leftPupilX, leftPupilY;
  int16_t rightPupilX, rightPupilY;
  int16_t pupilRadius;

  // Previous brow endpoints (for targeted clear)
  int16_t browLx0, browLy0, browLx1, browLy1;
  int16_t browRx0, browRy0, browRx1, browRy1;
  int16_t browThickness;
  bool browWasVisible;

  // Previous mouth bounds
  int16_t mouthTop, mouthBot, mouthLeft, mouthRight;
  BotMouthType mouthType;

  bool valid;  // false on first frame

  void invalidate() { valid = false; }
};

static BotPrevFrame prevFrame = { .valid = false };

// Erase a thick line from previous frame by overwriting with background
void erasePrevThickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness, uint16_t bgColor) {
  // Clear a bounding rect around the line — simpler and faster than redrawing
  int16_t minX = min(x0, x1) - thickness / 2 - 1;
  int16_t maxX = max(x0, x1) + thickness / 2 + 1;
  int16_t minY = min(y0, y1) - thickness / 2 - 1;
  int16_t maxY = max(y0, y1) + thickness / 2 + 1;
  gfx->fillRect(minX, minY, maxX - minX + 1, maxY - minY + 1, bgColor);
}

// ============================================================================
// Main Eye Render Function — flicker-free
// ============================================================================

// Render the complete face based on current BotFaceState
void renderBotFace(BotFaceState &face, uint16_t bgColor) {
  if (gfx == nullptr) return;

  int16_t cx = BOT_FACE_CX;
  int16_t cy = BOT_FACE_CY;

  // Calculate final pupil positions (expression + dynamic + look)
  int16_t finalPupilX = face.pupilOffsetX + face.dynamicPupilX;
  int16_t finalPupilY = face.pupilOffsetY + face.dynamicPupilY;

  // Eye centers
  int16_t leftEyeCX = cx - face.eyeSpacing;
  int16_t rightEyeCX = cx + face.eyeSpacing;
  int16_t eyeCY = cy;

  // Apply blink: reduce eye height
  int16_t effectiveEyeH = face.eyeWhiteH;
  if (face.blinkAmount > 0.01f) {
    effectiveEyeH = (int16_t)(face.eyeWhiteH * (1.0f - face.blinkAmount));
    if (effectiveEyeH < 2) effectiveEyeH = 2;
  }

  // ---- Erase previous frame's elements that moved/changed ----
  if (prevFrame.valid) {
    // If eye mode changed or eyes shrank, clear old eye areas
    if (prevFrame.eyeMode != face.eyeMode ||
        prevFrame.eyeW > face.eyeWhiteW + 2 ||
        prevFrame.eyeH > effectiveEyeH + 2) {
      // Clear old eye bounding boxes
      gfx->fillRect(leftEyeCX - prevFrame.eyeW - 2, eyeCY - prevFrame.eyeH - 2,
                     prevFrame.eyeW * 2 + 4, prevFrame.eyeH * 2 + 4, bgColor);
      gfx->fillRect(rightEyeCX - prevFrame.eyeW - 2, eyeCY - prevFrame.eyeH - 2,
                     prevFrame.eyeW * 2 + 4, prevFrame.eyeH * 2 + 4, bgColor);
    }

    // Erase old brows if they were visible
    if (prevFrame.browWasVisible) {
      erasePrevThickLine(prevFrame.browLx0, prevFrame.browLy0,
                         prevFrame.browLx1, prevFrame.browLy1,
                         prevFrame.browThickness, bgColor);
      erasePrevThickLine(prevFrame.browRx0, prevFrame.browRy0,
                         prevFrame.browRx1, prevFrame.browRy1,
                         prevFrame.browThickness, bgColor);
    }

    // Erase old mouth area
    if (prevFrame.mouthType != MOUTH_NONE) {
      gfx->fillRect(prevFrame.mouthLeft - 1, prevFrame.mouthTop - 1,
                     prevFrame.mouthRight - prevFrame.mouthLeft + 2,
                     prevFrame.mouthBot - prevFrame.mouthTop + 2, bgColor);
    }
  }

  // ---- Draw eyes ----
  // Eye whites are drawn every frame — they naturally cover old pupils
  switch (face.eyeMode) {
    case EYE_NORMAL: {
      // Large white ellipses
      gfx->fillEllipse(leftEyeCX, eyeCY, face.eyeWhiteW, effectiveEyeH, botFaceColor);
      gfx->fillEllipse(rightEyeCX, eyeCY, face.eyeWhiteW, effectiveEyeH, botFaceColor);

      // Pupils (only if eyes are reasonably open)
      if (effectiveEyeH > 8) {
        int16_t maxPupilX = face.eyeWhiteW - face.pupilRadius - 4;
        int16_t maxPupilY = effectiveEyeH - face.pupilRadius - 4;
        int16_t pX = constrain(finalPupilX, -maxPupilX, maxPupilX);
        int16_t pY = constrain(finalPupilY, -maxPupilY, maxPupilY);

        gfx->fillCircle(leftEyeCX + pX, eyeCY + pY, face.pupilRadius, BOT_COLOR_PUPIL);
        gfx->fillCircle(rightEyeCX + pX, eyeCY + pY, face.pupilRadius, BOT_COLOR_PUPIL);

        // Track pupil positions
        prevFrame.leftPupilX = leftEyeCX + pX;
        prevFrame.leftPupilY = eyeCY + pY;
        prevFrame.rightPupilX = rightEyeCX + pX;
        prevFrame.rightPupilY = eyeCY + pY;
        prevFrame.pupilRadius = face.pupilRadius;
      }
      break;
    }

    case EYE_CARET: {
      drawCaretEye(leftEyeCX, eyeCY, face.eyeWhiteW * 2 / 3, face.eyeWhiteH, botFaceColor);
      drawCaretEye(rightEyeCX, eyeCY, face.eyeWhiteW * 2 / 3, face.eyeWhiteH, botFaceColor);
      break;
    }

    case EYE_HEART: {
      int16_t heartSize = min(face.eyeWhiteW, face.eyeWhiteH) * 3 / 4;
      drawHeart(leftEyeCX, eyeCY, heartSize, botFaceColor);
      drawHeart(rightEyeCX, eyeCY, heartSize, botFaceColor);
      break;
    }

    case EYE_X: {
      int16_t xSize = min(face.eyeWhiteW, face.eyeWhiteH) * 2 / 3;
      drawXEye(leftEyeCX, eyeCY, xSize, botFaceColor);
      drawXEye(rightEyeCX, eyeCY, xSize, botFaceColor);
      break;
    }

    case EYE_SPIRAL: {
      gfx->fillEllipse(leftEyeCX, eyeCY, face.eyeWhiteW, effectiveEyeH, botFaceColor);
      gfx->fillEllipse(rightEyeCX, eyeCY, face.eyeWhiteW, effectiveEyeH, botFaceColor);
      float phase = (float)(millis() % 2000) / 2000.0f * TWO_PI;
      int16_t spiralR = min(face.eyeWhiteW, effectiveEyeH) - 6;
      drawSpiral(leftEyeCX, eyeCY, spiralR, BOT_COLOR_PUPIL, phase);
      drawSpiral(rightEyeCX, eyeCY, spiralR, BOT_COLOR_PUPIL, -phase);
      break;
    }

    case EYE_STAR: {
      int16_t starOuter = min(face.eyeWhiteW, face.eyeWhiteH) * 3 / 4;
      int16_t starInner = starOuter * 2 / 5;
      drawStar(leftEyeCX, eyeCY, starOuter, starInner, botFaceColor);
      drawStar(rightEyeCX, eyeCY, starOuter, starInner, botFaceColor);
      break;
    }
  }

  // Track eye state for next frame
  prevFrame.eyeW = face.eyeWhiteW;
  prevFrame.eyeH = effectiveEyeH;
  prevFrame.eyeMode = face.eyeMode;

  // ---- Eyebrows ----
  if (face.browVisible && face.blinkAmount < 0.8f) {
    int16_t browY = eyeCY - effectiveEyeH + face.browOffsetY;

    float radL = face.browAngleL * PI / 180.0f;
    int16_t lbx0 = leftEyeCX - face.browLength;
    int16_t lbx1 = leftEyeCX + face.browLength;
    int16_t lby0 = browY + (int16_t)(sinf(-radL) * face.browLength);
    int16_t lby1 = browY + (int16_t)(sinf(radL) * face.browLength);
    drawThickLine(lbx0, lby0, lbx1, lby1, face.browThickness, botFaceColor);

    float radR = face.browAngleR * PI / 180.0f;
    int16_t rbx0 = rightEyeCX - face.browLength;
    int16_t rbx1 = rightEyeCX + face.browLength;
    int16_t rby0 = browY + (int16_t)(sinf(radR) * face.browLength);
    int16_t rby1 = browY + (int16_t)(sinf(-radR) * face.browLength);
    drawThickLine(rbx0, rby0, rbx1, rby1, face.browThickness, botFaceColor);

    // Track brow positions
    prevFrame.browLx0 = lbx0; prevFrame.browLy0 = lby0;
    prevFrame.browLx1 = lbx1; prevFrame.browLy1 = lby1;
    prevFrame.browRx0 = rbx0; prevFrame.browRy0 = rby0;
    prevFrame.browRx1 = rbx1; prevFrame.browRy1 = rby1;
    prevFrame.browThickness = face.browThickness;
    prevFrame.browWasVisible = true;
  } else {
    prevFrame.browWasVisible = false;
  }

  // ---- Mouth ----
  int16_t mouthCX = cx;
  int16_t mouthCY = cy + face.mouthOffsetY;

  // Track mouth bounds as we draw
  int16_t mTop = mouthCY, mBot = mouthCY, mLeft = mouthCX, mRight = mouthCX;

  switch (face.mouthType) {
    case MOUTH_NONE:
      break;

    case MOUTH_LINE: {
      drawThickLine(mouthCX - face.mouthWidth, mouthCY,
                     mouthCX + face.mouthWidth, mouthCY, 3, botFaceColor);
      mLeft = mouthCX - face.mouthWidth; mRight = mouthCX + face.mouthWidth;
      mTop = mouthCY - 2; mBot = mouthCY + 2;
      break;
    }

    case MOUTH_SMILE: {
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = (int16_t)(face.mouthCurve * t * t);
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      mLeft = mouthCX - face.mouthWidth - 2; mRight = mouthCX + face.mouthWidth + 2;
      mTop = mouthCY - 2; mBot = mouthCY + face.mouthCurve + 2;
      break;
    }

    case MOUTH_FROWN: {
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = -(int16_t)(face.mouthCurve * t * t);
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      mLeft = mouthCX - face.mouthWidth - 2; mRight = mouthCX + face.mouthWidth + 2;
      mTop = mouthCY - face.mouthCurve - 2; mBot = mouthCY + 2;
      break;
    }

    case MOUTH_OPEN_O: {
      gfx->fillCircle(mouthCX, mouthCY, face.mouthCurve, botFaceColor);
      gfx->fillCircle(mouthCX, mouthCY, face.mouthCurve - 3, BOT_COLOR_BG);
      mLeft = mouthCX - face.mouthCurve - 1; mRight = mouthCX + face.mouthCurve + 1;
      mTop = mouthCY - face.mouthCurve - 1; mBot = mouthCY + face.mouthCurve + 1;
      break;
    }

    case MOUTH_GRIN: {
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = (int16_t)(face.mouthCurve * t * t);
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      drawThickLine(mouthCX - face.mouthWidth + 4, mouthCY + 2,
                     mouthCX + face.mouthWidth - 4, mouthCY + 2, 2, BOT_COLOR_BG);
      mLeft = mouthCX - face.mouthWidth - 2; mRight = mouthCX + face.mouthWidth + 2;
      mTop = mouthCY - 2; mBot = mouthCY + face.mouthCurve + 2;
      break;
    }

    case MOUTH_WAVY: {
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        int16_t y = (int16_t)(sinf(t * PI * 3.0f) * face.mouthCurve);
        gfx->fillCircle(mouthCX + x, mouthCY + y, 2, botFaceColor);
      }
      mLeft = mouthCX - face.mouthWidth - 2; mRight = mouthCX + face.mouthWidth + 2;
      mTop = mouthCY - face.mouthCurve - 2; mBot = mouthCY + face.mouthCurve + 2;
      break;
    }

    case MOUTH_SMIRK: {
      for (int16_t x = -face.mouthWidth; x <= face.mouthWidth; x++) {
        float t = (float)x / face.mouthWidth;
        float normalized = (t + 1.0f) / 2.0f;
        int16_t y = (int16_t)(face.mouthCurve * normalized * normalized);
        gfx->fillCircle(mouthCX + x, mouthCY + y - face.mouthCurve / 3, 2, botFaceColor);
      }
      mLeft = mouthCX - face.mouthWidth - 2; mRight = mouthCX + face.mouthWidth + 2;
      mTop = mouthCY - face.mouthCurve / 3 - 2; mBot = mouthCY + face.mouthCurve + 2;
      break;
    }
  }

  // Store mouth bounds for next-frame erase
  prevFrame.mouthLeft = mLeft;
  prevFrame.mouthRight = mRight;
  prevFrame.mouthTop = mTop;
  prevFrame.mouthBot = mBot;
  prevFrame.mouthType = face.mouthType;

  prevFrame.valid = true;
}

#endif // BOT_EYES_H
