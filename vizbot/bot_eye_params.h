#ifndef BOT_EYE_PARAMS_H
#define BOT_EYE_PARAMS_H

// ============================================================================
// Bot Eye Parameters (generated: do not edit by hand)
// ============================================================================
// Source: eyelab/faces (the vizBot eye lab face files)
// Regenerate: python3 tools/gen_eye_params.py eyelab/faces > vizbot/bot_eye_params.h
//
// One parametric eye per side, drawn as one vertical span per pixel column.
// Sizes are x R (eye half-width). [0] = screen-left eye, [1] = screen-right.
// Row order matches the EXPR_* indices in bot_faces.h; vizbrain and /expr rely on it.
// ============================================================================

#include <Arduino.h>

enum BotEyeOverlay : uint8_t { EYE_OVERLAY_NONE = 0, EYE_OVERLAY_X, EYE_OVERLAY_SPIRAL, EYE_OVERLAY_GLITCH };
enum BotGazePattern : uint8_t {
  GAZE_IDLE = 0, GAZE_THINKING, GAZE_NERVOUS, GAZE_WORRIED, GAZE_SHY, GAZE_FOCUSED, GAZE_ANNOYED, GAZE_SHOCKED
};
enum BotEyeEntry : uint8_t { EYE_ENTRY_NONE = 0, EYE_ENTRY_SMALL, EYE_ENTRY_EYEROLL };

struct BotEyeParams {
  float aspect[2];   // half-height / half-width
  float scale[2];    // per-eye size
  float lidTop[2];   // top lid coverage, x eye height
  float slant[2];    // + inner corner down, x half-height
  float sag[2];      // top lid middle droop, x eye height
  float lidBot[2];   // bottom lid coverage, x eye height
  float bow[2];      // bottom lid arches up in the middle
  float shut[2];     // morph to the closed arc
  float cup[2];      // 0 = asleep, 1 = beaming ^
  float inset[2];    // pupil toward the nose, x R
  float pdy[2];      // pupil vertical bias, + down, x R
  float pw[2];       // pupil half-width, x R
  float ph[2];       // pupil half-height, x R
  float heart[2];    // pupil heart-ness
  float star[2];     // pupil star-ness (heart + star <= 1)
  float gazeX;       // authored look, + right, x R
  float gazeY;       // authored look, + down, x R
  float pop;         // mood pop on entry, 0..1
  uint16_t transitionMs;  // shape spring settles in about this long
  BotEyeOverlay overlay;
  BotGazePattern gaze;
  BotEyeEntry entry;
  bool pulse;        // heart pulse
  bool exitBlink;    // leaving this face starts a blink
  uint16_t noBlinkMs;  // no auto blink for this long after entry
  float blinkRate;   // auto blink rate multiplier
};

// Globals
#define BOT_EYE_R        48.0f  // eye half-width in px at 240 wide
#define BOT_EYE_GAP      0.08f  // min gap between the eyes, x R
#define BOT_EYE_ARC_W    0.18f  // closed-arc thickness, x R
#define BOT_EYE_STAR_SPIN 0.6f // star pupil rotation, rad/s

const BotEyeParams botEyeParams[25] PROGMEM = {
  // 0: NEUTRAL
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.04f, 0.04f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 300,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 1: HAPPY
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.03f, 1.03f},
    .lidTop = {0.04f, 0.04f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.35f, 0.35f}, .bow = {0.5f, 0.5f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {-0.12f, -0.12f},
    .pw = {0.3f, 0.3f}, .ph = {0.3f, 0.3f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 1.0f, .transitionMs = 250,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 2: SAD (Kevin-tuned)
  {
    .aspect = {0.92f, 0.92f}, .scale = {0.96f, 0.96f},
    .lidTop = {0.22f, 0.22f}, .slant = {-0.45f, -0.45f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.3f, 0.3f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.1f, 0.1f},
    .pw = {0.32f, 0.32f}, .ph = {0.34f, 0.34f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.2f, .pop = 0.0f, .transitionMs = 400,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 3: SURPRISED
  {
    .aspect = {1.0f, 1.0f}, .scale = {1.1f, 1.1f},
    .lidTop = {0.0f, 0.0f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.08f, 0.08f}, .ph = {0.08f, 0.08f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 1.0f, .transitionMs = 170,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_SMALL,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 4: CHILL (Kevin-tuned)
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.38f, 0.38f}, .slant = {-0.05f, -0.05f}, .sag = {0.08f, 0.08f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.05f, 0.05f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 400,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 5: ANGRY
  {
    .aspect = {0.85f, 0.85f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.3f, 0.3f}, .slant = {0.45f, 0.45f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.12f, 0.12f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.18f, 0.18f}, .ph = {0.18f, 0.18f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.5f, .transitionMs = 230,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 6: LOVE (Kevin-tuned)
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.0f, 0.0f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.25f, 0.25f}, .bow = {0.4f, 0.4f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.42f, 0.42f}, .ph = {0.42f, 0.42f},
    .heart = {1.0f, 1.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 1.0f, .transitionMs = 300,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = true, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 7: DIZZY
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.04f, 0.94f},
    .lidTop = {0.04f, 0.04f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 200,
    .overlay = EYE_OVERLAY_SPIRAL, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 8: THINKING
  {
    .aspect = {0.92f, 0.92f}, .scale = {0.96f, 1.06f},
    .lidTop = {0.24f, 0.0f}, .slant = {-0.1f, 0.1f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.1f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.18f, 0.18f}, .ph = {0.18f, 0.18f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 350,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_THINKING, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 9: EXCITED (Kevin-tuned)
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.08f, 1.08f},
    .lidTop = {0.14f, 0.14f}, .slant = {-0.35f, -0.35f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.42f, 0.42f}, .ph = {0.42f, 0.42f},
    .heart = {0.0f, 0.0f}, .star = {1.0f, 1.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 1.0f, .transitionMs = 200,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 10: MISCHIEF
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.32f, 0.08f}, .slant = {0.2f, 0.05f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.2f, 0.12f}, .bow = {0.35f, 0.35f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.32f, .gazeY = 0.0f, .pop = 0.5f, .transitionMs = 300,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 11: SKEPTICAL (Kevin-tuned)
  {
    .aspect = {0.92f, 0.92f}, .scale = {0.95f, 1.05f},
    .lidTop = {0.38f, 0.0f}, .slant = {0.15f, -0.05f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.16f, 0.26f}, .ph = {0.16f, 0.26f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.12f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 300,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 12: WORRIED
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.12f, 0.12f}, .slant = {-0.35f, -0.35f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.06f, 0.06f},
    .pw = {0.18f, 0.18f}, .ph = {0.18f, 0.18f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 350,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_WORRIED, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 13: CONFUSED
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.06f, 0.88f},
    .lidTop = {0.0f, 0.3f}, .slant = {0.0f, -0.2f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.16f, 0.26f}, .ph = {0.16f, 0.26f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = -0.1f, .pop = 0.5f, .transitionMs = 350,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 14: PROUD
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.04f, 1.04f},
    .lidTop = {0.04f, 0.04f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {1.0f, 1.0f}, .cup = {1.0f, 1.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.5f, .transitionMs = 350,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 15: SHY
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 0.97f},
    .lidTop = {0.16f, 0.16f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.3f, 0.3f}, .bow = {0.4f, 0.4f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 400,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_SHY, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 16: ANNOYED
  {
    .aspect = {0.88f, 0.88f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.5f, 0.5f}, .slant = {0.03f, 0.03f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.02f, 0.02f},
    .pw = {0.2f, 0.2f}, .ph = {0.2f, 0.2f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 350,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_ANNOYED, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = true, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 17: FOCUSED
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.2f, 0.2f}, .slant = {0.08f, 0.08f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.1f, 0.1f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.18f, 0.18f}, .ph = {0.18f, 0.18f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 300,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_FOCUSED, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 18: WINKING (Kevin-tuned)
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.04f, 0.04f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.2f, 0.0f}, .bow = {0.3f, 0.0f},
    .shut = {0.0f, 1.0f}, .cup = {0.0f, 1.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.5f, .transitionMs = 320,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 19: DEVIOUS
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.36f, 0.36f}, .slant = {0.3f, 0.3f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.22f, 0.22f}, .bow = {0.45f, 0.45f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.18f, 0.18f}, .ph = {0.18f, 0.18f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.15f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 300,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 20: SHOCKED
  {
    .aspect = {1.12f, 1.12f}, .scale = {1.12f, 1.12f},
    .lidTop = {0.0f, 0.0f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.08f, 0.08f}, .ph = {0.08f, 0.08f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 1.0f, .transitionMs = 200,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_SHOCKED, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = true, .noBlinkMs = 1500, .blinkRate = 1.0f
  },
  // 21: KISSING
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.04f, 0.04f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {1.0f, 1.0f}, .cup = {0.2f, 0.2f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.5f, .transitionMs = 250,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 22: NERVOUS (Kevin-tuned)
  {
    .aspect = {1.05f, 1.05f}, .scale = {0.94f, 0.94f},
    .lidTop = {0.08f, 0.08f}, .slant = {-0.3f, -0.3f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.16f, 0.16f}, .ph = {0.16f, 0.16f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 200,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_NERVOUS, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 2.0f
  },
  // 23: GLITCHING
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.04f, 0.04f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.0f, .gazeY = 0.0f, .pop = 0.0f, .transitionMs = 100,
    .overlay = EYE_OVERLAY_GLITCH, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_NONE,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
  // 24: SASSY
  {
    .aspect = {0.92f, 0.92f}, .scale = {1.0f, 1.0f},
    .lidTop = {0.25f, 0.15f}, .slant = {0.0f, 0.0f}, .sag = {0.0f, 0.0f},
    .lidBot = {0.0f, 0.0f}, .bow = {0.0f, 0.0f},
    .shut = {0.0f, 0.0f}, .cup = {0.0f, 0.0f},
    .inset = {0.08f, 0.08f}, .pdy = {0.0f, 0.0f},
    .pw = {0.27f, 0.27f}, .ph = {0.27f, 0.27f},
    .heart = {0.0f, 0.0f}, .star = {0.0f, 0.0f},
    .gazeX = 0.2f, .gazeY = -0.05f, .pop = 0.5f, .transitionMs = 250,
    .overlay = EYE_OVERLAY_NONE, .gaze = GAZE_IDLE, .entry = EYE_ENTRY_EYEROLL,
    .pulse = false, .exitBlink = false, .noBlinkMs = 0, .blinkRate = 1.0f
  },
};

#endif // BOT_EYE_PARAMS_H
