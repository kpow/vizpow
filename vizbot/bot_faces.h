#ifndef BOT_FACES_H
#define BOT_FACES_H

#include <Arduino.h>

// ============================================================================
// Bot Face Expression System
// ============================================================================
// Each expression is a struct of numeric parameters that define the face.
// Transitions between expressions are done by lerping all parameters.
// Rendering uses TFT drawing primitives only — no sprites.
//
// Eye style: large white ellipses (overlapping at center), dark pupils,
// thick arc eyebrows. Matches cartoon eye reference sheet aesthetic.
// ============================================================================

// Special eye modes (override normal ellipse rendering)
enum BotEyeMode : uint8_t {
  EYE_NORMAL = 0,    // Standard ellipse whites + circle pupils
  EYE_CARET,         // ^ ^ happy squint (upward arcs, no pupils)
  EYE_HEART,         // Heart-shaped eyes
  EYE_X,             // X X dead/angry eyes
  EYE_SPIRAL,        // Spiral dizzy eyes (animated)
  EYE_STAR           // Star/sparkle eyes
};

// Mouth shape types
enum BotMouthType : uint8_t {
  MOUTH_NONE = 0,    // No mouth drawn
  MOUTH_LINE,        // Flat horizontal line
  MOUTH_SMILE,       // Upward arc
  MOUTH_FROWN,       // Downward arc
  MOUTH_OPEN_O,      // Circle/O shape (surprise)
  MOUTH_GRIN,        // Wide smile with teeth line
  MOUTH_WAVY,        // Wavy line (dizzy/confused)
  MOUTH_SMIRK        // Asymmetric half-smile
};

// Expression parameter struct — defines a complete face pose
struct BotExpression {
  // Eye shape (ellipse radii for normal mode)
  int16_t eyeWhiteW;       // Eye white ellipse horizontal radius (half-width)
  int16_t eyeWhiteH;       // Eye white ellipse vertical radius (half-height)
  int16_t eyeSpacing;      // Horizontal distance between eye centers (from screen center)

  // Pupil
  int16_t pupilRadius;     // Pupil circle radius
  int16_t pupilOffsetX;    // Base pupil X offset from eye center (for look direction)
  int16_t pupilOffsetY;    // Base pupil Y offset from eye center

  // Eyebrows
  int16_t browOffsetY;     // Brow Y position relative to eye top (-= higher)
  int16_t browLength;      // Brow arc/bar length (half-width)
  int16_t browThickness;   // Brow thickness in pixels
  int8_t  browAngleL;      // Left brow angle (-= inner down, += inner up) in degrees
  int8_t  browAngleR;      // Right brow angle (mirrored)
  bool    browVisible;     // Whether to draw eyebrows

  // Mouth
  BotMouthType mouthType;
  int16_t mouthWidth;      // Mouth width (half-width from center)
  int16_t mouthOffsetY;    // Mouth Y position below eye center
  int16_t mouthCurve;      // Curve amount for smile/frown (arc height)

  // Special modes
  BotEyeMode eyeMode;

  // Animation hints
  uint16_t transitionMs;   // Default transition time to this expression
};

// ============================================================================
// Expression Definitions
// ============================================================================
// Screen: 240x280. Face centered around (120, 125) — slightly above center
// to leave room for mouth and speech bubbles below.
//
// Eye whites: ~48-55px radius horizontally, overlapping at center.
// Pupils: ~12-14px radius. Brows: thick arcs above eyes.
// ============================================================================

// Face center coordinates
#define BOT_FACE_CX 120
#define BOT_FACE_CY 118

// Number of defined expressions
#define BOT_NUM_EXPRESSIONS 20

// Expression indices
#define EXPR_NEUTRAL    0
#define EXPR_HAPPY      1
#define EXPR_SAD        2
#define EXPR_SURPRISED  3
#define EXPR_SLEEPY     4
#define EXPR_ANGRY      5
#define EXPR_LOVE       6
#define EXPR_DIZZY      7
#define EXPR_THINKING   8
#define EXPR_EXCITED    9
#define EXPR_MISCHIEF   10
#define EXPR_DEAD       11
#define EXPR_SKEPTICAL  12
#define EXPR_WORRIED    13
#define EXPR_CONFUSED   14
#define EXPR_PROUD      15
#define EXPR_SHY        16
#define EXPR_ANNOYED    17
#define EXPR_BLISS      18
#define EXPR_FOCUSED    19

// Expression parameter table (stored in PROGMEM)
const BotExpression botExpressions[BOT_NUM_EXPRESSIONS] PROGMEM = {
  // 0: NEUTRAL — relaxed, default idle state
  // Large round eyes, centered pupils, relaxed brows, slight smile
  {
    .eyeWhiteW = 50, .eyeWhiteH = 45, .eyeSpacing = 44,
    .pupilRadius = 13, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -12, .browLength = 30, .browThickness = 6,
    .browAngleL = 0, .browAngleR = 0, .browVisible = true,
    .mouthType = MOUTH_SMILE, .mouthWidth = 18, .mouthOffsetY = 62, .mouthCurve = 6,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 300
  },

  // 1: HAPPY — upward arc eyes (^ ^), big smile
  {
    .eyeWhiteW = 50, .eyeWhiteH = 45, .eyeSpacing = 44,
    .pupilRadius = 13, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -16, .browLength = 28, .browThickness = 5,
    .browAngleL = 10, .browAngleR = 10, .browVisible = false,
    .mouthType = MOUTH_GRIN, .mouthWidth = 24, .mouthOffsetY = 58, .mouthCurve = 12,
    .eyeMode = EYE_CARET,
    .transitionMs = 250
  },

  // 2: SAD — droopy eyes, down-looking pupils, frown
  {
    .eyeWhiteW = 48, .eyeWhiteH = 40, .eyeSpacing = 44,
    .pupilRadius = 14, .pupilOffsetX = 0, .pupilOffsetY = 8,
    .browOffsetY = -10, .browLength = 28, .browThickness = 6,
    .browAngleL = -18, .browAngleR = -18, .browVisible = true,
    .mouthType = MOUTH_FROWN, .mouthWidth = 16, .mouthOffsetY = 65, .mouthCurve = 8,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 400
  },

  // 3: SURPRISED — wide eyes, small pupils, O mouth
  {
    .eyeWhiteW = 55, .eyeWhiteH = 55, .eyeSpacing = 46,
    .pupilRadius = 9, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -20, .browLength = 26, .browThickness = 5,
    .browAngleL = 12, .browAngleR = 12, .browVisible = true,
    .mouthType = MOUTH_OPEN_O, .mouthWidth = 14, .mouthOffsetY = 62, .mouthCurve = 14,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 150
  },

  // 4: SLEEPY — half-lidded eyes (vertically squished), slight open mouth
  {
    .eyeWhiteW = 50, .eyeWhiteH = 22, .eyeSpacing = 44,
    .pupilRadius = 12, .pupilOffsetX = 0, .pupilOffsetY = 4,
    .browOffsetY = -6, .browLength = 30, .browThickness = 6,
    .browAngleL = -5, .browAngleR = -5, .browVisible = true,
    .mouthType = MOUTH_OPEN_O, .mouthWidth = 8, .mouthOffsetY = 58, .mouthCurve = 6,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 500
  },

  // 5: ANGRY — narrowed sharp eyes, V-brows, gritting mouth
  {
    .eyeWhiteW = 52, .eyeWhiteH = 32, .eyeSpacing = 44,
    .pupilRadius = 12, .pupilOffsetX = 0, .pupilOffsetY = 2,
    .browOffsetY = -6, .browLength = 32, .browThickness = 7,
    .browAngleL = 22, .browAngleR = 22, .browVisible = true,
    .mouthType = MOUTH_LINE, .mouthWidth = 20, .mouthOffsetY = 62, .mouthCurve = 0,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 200
  },

  // 6: LOVE — heart-shaped eyes, smile
  {
    .eyeWhiteW = 50, .eyeWhiteH = 45, .eyeSpacing = 46,
    .pupilRadius = 13, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -14, .browLength = 28, .browThickness = 5,
    .browAngleL = 8, .browAngleR = 8, .browVisible = false,
    .mouthType = MOUTH_SMILE, .mouthWidth = 20, .mouthOffsetY = 60, .mouthCurve = 10,
    .eyeMode = EYE_HEART,
    .transitionMs = 300
  },

  // 7: DIZZY — spiral eyes, wavy mouth (post-shake)
  {
    .eyeWhiteW = 50, .eyeWhiteH = 45, .eyeSpacing = 44,
    .pupilRadius = 13, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -10, .browLength = 26, .browThickness = 5,
    .browAngleL = -8, .browAngleR = 12, .browVisible = true,
    .mouthType = MOUTH_WAVY, .mouthWidth = 18, .mouthOffsetY = 62, .mouthCurve = 6,
    .eyeMode = EYE_SPIRAL,
    .transitionMs = 200
  },

  // 8: THINKING — looking up-right, one raised brow, slight pucker
  {
    .eyeWhiteW = 50, .eyeWhiteH = 45, .eyeSpacing = 44,
    .pupilRadius = 12, .pupilOffsetX = 10, .pupilOffsetY = -10,
    .browOffsetY = -14, .browLength = 28, .browThickness = 6,
    .browAngleL = 0, .browAngleR = 15, .browVisible = true,
    .mouthType = MOUTH_LINE, .mouthWidth = 10, .mouthOffsetY = 62, .mouthCurve = 0,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 350
  },

  // 9: EXCITED — star/sparkle eyes, big grin
  {
    .eyeWhiteW = 52, .eyeWhiteH = 48, .eyeSpacing = 46,
    .pupilRadius = 14, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -18, .browLength = 26, .browThickness = 5,
    .browAngleL = 12, .browAngleR = 12, .browVisible = false,
    .mouthType = MOUTH_GRIN, .mouthWidth = 26, .mouthOffsetY = 56, .mouthCurve = 14,
    .eyeMode = EYE_STAR,
    .transitionMs = 200
  },

  // 10: MISCHIEVOUS — side-glance, one eye narrowed, smirk
  {
    .eyeWhiteW = 50, .eyeWhiteH = 45, .eyeSpacing = 44,
    .pupilRadius = 13, .pupilOffsetX = 12, .pupilOffsetY = 2,
    .browOffsetY = -12, .browLength = 28, .browThickness = 6,
    .browAngleL = -6, .browAngleR = 14, .browVisible = true,
    .mouthType = MOUTH_SMIRK, .mouthWidth = 18, .mouthOffsetY = 62, .mouthCurve = 8,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 300
  },

  // 11: DEAD — X eyes, flat line mouth
  {
    .eyeWhiteW = 48, .eyeWhiteH = 42, .eyeSpacing = 44,
    .pupilRadius = 13, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -10, .browLength = 28, .browThickness = 5,
    .browAngleL = 0, .browAngleR = 0, .browVisible = false,
    .mouthType = MOUTH_LINE, .mouthWidth = 18, .mouthOffsetY = 62, .mouthCurve = 0,
    .eyeMode = EYE_X,
    .transitionMs = 150
  },

  // 12: SKEPTICAL — one eye narrowed, one normal, flat mouth
  {
    .eyeWhiteW = 50, .eyeWhiteH = 45, .eyeSpacing = 44,
    .pupilRadius = 13, .pupilOffsetX = 6, .pupilOffsetY = 2,
    .browOffsetY = -12, .browLength = 28, .browThickness = 6,
    .browAngleL = -10, .browAngleR = 18, .browVisible = true,
    .mouthType = MOUTH_LINE, .mouthWidth = 14, .mouthOffsetY = 62, .mouthCurve = 0,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 300
  },

  // 13: WORRIED — wide eyes, brows angled inward-up, frown
  {
    .eyeWhiteW = 52, .eyeWhiteH = 50, .eyeSpacing = 44,
    .pupilRadius = 11, .pupilOffsetX = 0, .pupilOffsetY = 4,
    .browOffsetY = -16, .browLength = 26, .browThickness = 5,
    .browAngleL = -14, .browAngleR = -14, .browVisible = true,
    .mouthType = MOUTH_FROWN, .mouthWidth = 12, .mouthOffsetY = 64, .mouthCurve = 6,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 350
  },

  // 14: CONFUSED — looking sideways, asymmetric brows, wavy mouth
  {
    .eyeWhiteW = 50, .eyeWhiteH = 45, .eyeSpacing = 44,
    .pupilRadius = 12, .pupilOffsetX = -8, .pupilOffsetY = -4,
    .browOffsetY = -12, .browLength = 26, .browThickness = 5,
    .browAngleL = 10, .browAngleR = -8, .browVisible = true,
    .mouthType = MOUTH_WAVY, .mouthWidth = 14, .mouthOffsetY = 62, .mouthCurve = 4,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 350
  },

  // 15: PROUD — eyes slightly closed, chin-up look, big smile
  {
    .eyeWhiteW = 50, .eyeWhiteH = 35, .eyeSpacing = 44,
    .pupilRadius = 12, .pupilOffsetX = 0, .pupilOffsetY = -4,
    .browOffsetY = -10, .browLength = 30, .browThickness = 6,
    .browAngleL = 6, .browAngleR = 6, .browVisible = true,
    .mouthType = MOUTH_SMILE, .mouthWidth = 22, .mouthOffsetY = 60, .mouthCurve = 10,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 350
  },

  // 16: SHY — looking down-left, small pupils, slight smile
  {
    .eyeWhiteW = 48, .eyeWhiteH = 42, .eyeSpacing = 44,
    .pupilRadius = 10, .pupilOffsetX = -10, .pupilOffsetY = 8,
    .browOffsetY = -10, .browLength = 24, .browThickness = 5,
    .browAngleL = -4, .browAngleR = 4, .browVisible = true,
    .mouthType = MOUTH_SMILE, .mouthWidth = 12, .mouthOffsetY = 62, .mouthCurve = 4,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 400
  },

  // 17: ANNOYED — half-lidded, side-looking, flat mouth
  {
    .eyeWhiteW = 52, .eyeWhiteH = 28, .eyeSpacing = 44,
    .pupilRadius = 12, .pupilOffsetX = 8, .pupilOffsetY = 2,
    .browOffsetY = -4, .browLength = 30, .browThickness = 7,
    .browAngleL = 10, .browAngleR = 10, .browVisible = true,
    .mouthType = MOUTH_LINE, .mouthWidth = 16, .mouthOffsetY = 60, .mouthCurve = 0,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 250
  },

  // 18: BLISS — caret eyes but relaxed, big content smile
  {
    .eyeWhiteW = 48, .eyeWhiteH = 40, .eyeSpacing = 44,
    .pupilRadius = 13, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -14, .browLength = 26, .browThickness = 5,
    .browAngleL = 8, .browAngleR = 8, .browVisible = false,
    .mouthType = MOUTH_SMILE, .mouthWidth = 20, .mouthOffsetY = 58, .mouthCurve = 10,
    .eyeMode = EYE_CARET,
    .transitionMs = 400
  },

  // 19: FOCUSED — normal eyes looking straight, slight squint, no mouth
  {
    .eyeWhiteW = 48, .eyeWhiteH = 38, .eyeSpacing = 44,
    .pupilRadius = 14, .pupilOffsetX = 0, .pupilOffsetY = 0,
    .browOffsetY = -8, .browLength = 28, .browThickness = 6,
    .browAngleL = 4, .browAngleR = 4, .browVisible = true,
    .mouthType = MOUTH_NONE, .mouthWidth = 0, .mouthOffsetY = 62, .mouthCurve = 0,
    .eyeMode = EYE_NORMAL,
    .transitionMs = 300
  }
};

// ============================================================================
// Expression interpolation helpers
// ============================================================================

// Lerp a single int16_t value
inline int16_t lerpI16(int16_t a, int16_t b, uint8_t t) {
  // t: 0-255 (0 = a, 255 = b)
  return a + (((int32_t)(b - a) * t) >> 8);
}

// Lerp a single int8_t value
inline int8_t lerpI8(int8_t a, int8_t b, uint8_t t) {
  return a + (((int16_t)(b - a) * t) >> 8);
}

// Runtime expression state (interpolated values — not in PROGMEM)
struct BotFaceState {
  // Interpolated parameters
  int16_t eyeWhiteW, eyeWhiteH, eyeSpacing;
  int16_t pupilRadius, pupilOffsetX, pupilOffsetY;
  int16_t browOffsetY, browLength, browThickness;
  int8_t  browAngleL, browAngleR;
  bool    browVisible;
  BotMouthType mouthType;
  int16_t mouthWidth, mouthOffsetY, mouthCurve;
  BotEyeMode eyeMode;

  // Dynamic offsets (from IMU, look-around, etc.)
  int16_t dynamicPupilX;   // Added to pupilOffsetX for final pupil position
  int16_t dynamicPupilY;   // Added to pupilOffsetY

  // Blink state
  float blinkAmount;       // 0.0 = open, 1.0 = fully closed

  // Transition state
  uint8_t currentExpr;
  uint8_t targetExpr;
  uint8_t blendT;          // 0-255 blend factor
  unsigned long transitionStart;
  uint16_t transitionDuration;
  bool transitioning;

  // Load expression from PROGMEM
  void loadExpression(uint8_t index) {
    if (index >= BOT_NUM_EXPRESSIONS) index = EXPR_NEUTRAL;
    BotExpression e;
    memcpy_P(&e, &botExpressions[index], sizeof(BotExpression));

    eyeWhiteW = e.eyeWhiteW;
    eyeWhiteH = e.eyeWhiteH;
    eyeSpacing = e.eyeSpacing;
    pupilRadius = e.pupilRadius;
    pupilOffsetX = e.pupilOffsetX;
    pupilOffsetY = e.pupilOffsetY;
    browOffsetY = e.browOffsetY;
    browLength = e.browLength;
    browThickness = e.browThickness;
    browAngleL = e.browAngleL;
    browAngleR = e.browAngleR;
    browVisible = e.browVisible;
    mouthType = e.mouthType;
    mouthWidth = e.mouthWidth;
    mouthOffsetY = e.mouthOffsetY;
    mouthCurve = e.mouthCurve;
    eyeMode = e.eyeMode;

    currentExpr = index;
    targetExpr = index;
    blendT = 255;
    transitioning = false;
  }

  // Start transitioning to a new expression
  void transitionTo(uint8_t index, uint16_t durationMs = 0) {
    if (index >= BOT_NUM_EXPRESSIONS) index = EXPR_NEUTRAL;
    if (index == targetExpr && !transitioning) return;

    // Read target's default transition time if none specified
    if (durationMs == 0) {
      BotExpression e;
      memcpy_P(&e, &botExpressions[index], sizeof(BotExpression));
      durationMs = e.transitionMs;
    }

    // Current interpolated state becomes the "from" snapshot
    currentExpr = targetExpr;
    targetExpr = index;
    blendT = 0;
    transitionStart = millis();
    transitionDuration = durationMs;
    transitioning = true;
  }

  // Update transition (call each frame)
  void update() {
    if (!transitioning) return;

    unsigned long elapsed = millis() - transitionStart;
    if (elapsed >= transitionDuration) {
      // Transition complete
      loadExpression(targetExpr);
      return;
    }

    // Calculate blend factor (0-255)
    blendT = (uint8_t)((elapsed * 255UL) / transitionDuration);

    // Read both expressions from PROGMEM
    BotExpression from, to;
    memcpy_P(&from, &botExpressions[currentExpr], sizeof(BotExpression));
    memcpy_P(&to, &botExpressions[targetExpr], sizeof(BotExpression));

    // Lerp all numeric parameters
    eyeWhiteW = lerpI16(from.eyeWhiteW, to.eyeWhiteW, blendT);
    eyeWhiteH = lerpI16(from.eyeWhiteH, to.eyeWhiteH, blendT);
    eyeSpacing = lerpI16(from.eyeSpacing, to.eyeSpacing, blendT);
    pupilRadius = lerpI16(from.pupilRadius, to.pupilRadius, blendT);
    pupilOffsetX = lerpI16(from.pupilOffsetX, to.pupilOffsetX, blendT);
    pupilOffsetY = lerpI16(from.pupilOffsetY, to.pupilOffsetY, blendT);
    browOffsetY = lerpI16(from.browOffsetY, to.browOffsetY, blendT);
    browLength = lerpI16(from.browLength, to.browLength, blendT);
    browThickness = lerpI16(from.browThickness, to.browThickness, blendT);
    browAngleL = lerpI8(from.browAngleL, to.browAngleL, blendT);
    browAngleR = lerpI8(from.browAngleR, to.browAngleR, blendT);

    mouthWidth = lerpI16(from.mouthWidth, to.mouthWidth, blendT);
    mouthOffsetY = lerpI16(from.mouthOffsetY, to.mouthOffsetY, blendT);
    mouthCurve = lerpI16(from.mouthCurve, to.mouthCurve, blendT);

    // Discrete parameters snap at halfway point
    bool pastHalf = (blendT >= 128);
    browVisible = pastHalf ? to.browVisible : from.browVisible;
    mouthType = pastHalf ? to.mouthType : from.mouthType;
    eyeMode = pastHalf ? to.eyeMode : from.eyeMode;
  }

  // Initialize to neutral
  void init() {
    dynamicPupilX = 0;
    dynamicPupilY = 0;
    blinkAmount = 0.0f;
    transitioning = false;
    loadExpression(EXPR_NEUTRAL);
  }
};

#endif // BOT_FACES_H
