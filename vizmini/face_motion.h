#ifndef FACE_MOTION_H
#define FACE_MOTION_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "gfx_mono.h"
#include "bot_eye_model.h"   // BotSpring, eyeRnd()

// ============================================================================
// Face motion — whole-face rotation for the vizMini
// ============================================================================
// The face is drawn upright into the U8g2 buffer as usual; apply() then turns
// the whole frame about the screen centre with a nearest-neighbour inverse map
// (8192 pixels, fixed point). While turned, the face shrinks just enough
// for an ellipse of FIT_A x FIT_B to stay on the 128x64 screen, so a spin
// passes through a small upright-ish face instead of clipping.
//
// The angle is a damped spring chasing a short list of keyframes per move:
// tilt, wobble, pendulum, drift, spin, double spin, flip. Moves are rare by
// design (Kevin, 2026-11-03): at level "rare" one lands every few minutes, a
// mood change sometimes adds a lean. Level "often" is for showing him off.
// ============================================================================

enum FaceMove : uint8_t {
  MOVE_NONE = 0, MOVE_TILT, MOVE_WOBBLE, MOVE_PENDULUM, MOVE_DRIFT,
  MOVE_SPIN, MOVE_SPIN2, MOVE_FLIP, MOVE_NUM
};
static const char *const faceMoveNames[MOVE_NUM] = {
  "none", "tilt", "wobble", "pendulum", "drift", "spin", "spin2", "flip"
};

enum MotionLevel : uint8_t { MOTION_OFF = 0, MOTION_RARE, MOTION_OFTEN, MOTION_NUM_LEVELS };
static const char *const motionLevelNames[MOTION_NUM_LEVELS] = { "off", "rare", "often" };

#define MOTION_MAX_KEYS 10

struct MotionKey {
  float angle;     // degrees, may pass 360 (wrapped when the move ends)
  uint16_t holdMs; // time on this key before the next
  float f, z;      // spring frequency (Hz) and damping while heading here
};

class FaceMotion {
public:
  void (*onDizzy)() = nullptr;   // a double spin leaves him dizzy (set by vizmini.ino)

  void begin() {
    ang.init(0, 1.2f, 0.6f);
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    level = p.getUChar("motion", MOTION_RARE);
    p.end();
    if (level >= MOTION_NUM_LEVELS) level = MOTION_RARE;
    scheduleNext(millis());
  }

  void setLevel(uint8_t l) {
    if (l >= MOTION_NUM_LEVELS) return;
    level = l;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar("motion", level);
    p.end();
    scheduleNext(millis());
  }
  uint8_t getLevel() const { return level; }
  const char *levelName() const { return motionLevelNames[level]; }
  const char *moveName() const { return faceMoveNames[move]; }
  float angle() const { return ang.x; }
  bool turned() const { return fabsf(ang.x) > 0.3f || fabsf(ang.v) > 1.0f; }

  // Start a move now (web buttons, mood reactions). Ignored while one runs,
  // except from the web (force), which replaces it.
  void start(uint8_t m, bool force = false) {
    if (m == MOVE_NONE || m >= MOVE_NUM) return;
    if (move != MOVE_NONE && !force) return;
    unsigned long now = millis();
    nKeys = 0;
    float dir = random(2) ? 1.0f : -1.0f;
    switch (m) {
      case MOVE_TILT: {
        float a = dir * eyeRnd(14, 26);
        key(a, (uint16_t)eyeRnd(1800, 3500), 1.4f, 0.45f);
        key(0, 900, 1.4f, 0.45f);
        break;
      }
      case MOVE_WOBBLE:   // a quick "nope" shake
        key(dir * 11, 130, 3.2f, 0.35f);
        key(-dir * 11, 130, 3.2f, 0.35f);
        key(dir * 8, 130, 3.2f, 0.35f);
        key(-dir * 5, 130, 3.2f, 0.35f);
        key(0, 700, 3.2f, 0.4f);
        break;
      case MOVE_PENDULUM: // pulled to one side, let go, swings itself out
        key(dir * 34, 700, 1.0f, 0.7f);
        key(0, 5000, 0.9f, 0.08f);
        break;
      case MOVE_DRIFT:    // a slow sway for a while
        for (uint8_t i = 0; i < 6; i++) key((i & 1 ? -dir : dir) * eyeRnd(5, 9), 2600, 0.35f, 0.8f);
        key(0, 2500, 0.35f, 0.8f);
        break;
      case MOVE_SPIN:
        key(dir * 360, 2200, 0.7f, 0.55f);
        break;
      case MOVE_SPIN2:
        key(dir * 720, 3200, 0.45f, 0.6f);
        break;
      case MOVE_FLIP:     // upside down for a beat, then rights himself the same way round
        key(dir * 180, (uint16_t)eyeRnd(2200, 3800), 0.8f, 0.5f);
        key(dir * 360, 2000, 0.8f, 0.5f);
        break;
    }
    move = m;
    keyIdx = 0;
    keyAt = now;
  }

  // A new expression may add a lean (leaning moods only, chance by level)
  void onExpression(uint8_t e) {
    if (level == MOTION_OFF || move != MOVE_NONE) return;
    float chance = level == MOTION_OFTEN ? 0.7f : 0.25f;
    float lean = 0;
    switch (e) {
      case EXPR_THINKING:  lean = 16; break;
      case EXPR_CONFUSED:  lean = random(2) ? 22 : -22; break;
      case EXPR_SASSY:     lean = -24; break;
      case EXPR_SHY:       lean = -14; break;
      case EXPR_SKEPTICAL: lean = 12; break;
      case EXPR_MISCHIEF:  lean = 18; break;
      case EXPR_DIZZY:     if (eyeRnd01() < chance) start(MOVE_SPIN); return;
      case EXPR_ANGRY:
      case EXPR_ANNOYED:   if (eyeRnd01() < chance) start(MOVE_WOBBLE); return;
      default: return;
    }
    if (eyeRnd01() >= chance) return;
    nKeys = 0;
    key(lean, (uint16_t)eyeRnd(2500, 4500), 1.3f, 0.45f);
    key(0, 900, 1.3f, 0.45f);
    move = MOVE_TILT;
    keyIdx = 0;
    keyAt = millis();
  }

  void step(unsigned long now) {
    float dt = (now - lastStep) * 0.001f;
    lastStep = now;
    if (dt > 0.1f) dt = 0.1f;

    if (move == MOVE_NONE) {
      if (level != MOTION_OFF && eyeReached(now, nextAt)) {
        start(pickMove());
        scheduleNext(now);
      }
    } else if (now - keyAt >= keys[keyIdx].holdMs) {
      if (++keyIdx >= nKeys) finish();
      else keyAt = now;
    }

    float target = 0, f = 1.2f, z = 0.6f;
    if (move != MOVE_NONE) { target = keys[keyIdx].angle; f = keys[keyIdx].f; z = keys[keyIdx].z; }
    ang.f = f;
    ang.z = z;
    ang.step(target, dt);
  }

  // Turn the frame in the U8g2 buffer. Call after the face is drawn, before
  // anything that should stay upright (speech bubble).
  void apply() {
    if (!turned()) return;
    float rad = ang.x * DEG_TO_RAD;
    float c = cosf(rad), s = sinf(rad);
    // Shrink so the face ellipse stays on screen at this angle
    float halfW = sqrtf(FIT_A * FIT_A * c * c + FIT_B * FIT_B * s * s);
    float halfH = sqrtf(FIT_A * FIT_A * s * s + FIT_B * FIT_B * c * c);
    float k = min(1.0f, min((OLED_WIDTH / 2.0f) / halfW, (OLED_HEIGHT / 2.0f + 1) / halfH));

    uint8_t *buf = u8g2.getBufferPtr();
    memcpy(src, buf, sizeof(src));
    memset(buf, 0, sizeof(src));

    // Inverse map: source = centre + R(-a) (dest - centre) / k, stepped in
    // 16.16 fixed point: the C3 has no FPU, and a float inner loop cost ~28 ms
    const float cx = OLED_WIDTH / 2.0f - 0.5f, cy = OLED_HEIGHT / 2.0f - 0.5f;
    float ic = c / k, is = s / k;
    const int32_t one = 1 << 16;
    int32_t dxX = (int32_t)lroundf(ic * one), dxY = (int32_t)lroundf(-is * one);   // per dest x
    int32_t dyX = (int32_t)lroundf(is * one), dyY = (int32_t)lroundf(ic * one);    // per dest y
    // Source of dest (0, 0), plus a half pixel so >> 16 rounds
    int32_t rowX = (int32_t)lroundf((cx - cx * ic - cy * is + 0.5f) * one);
    int32_t rowY = (int32_t)lroundf((cy + cx * is - cy * ic + 0.5f) * one);
    for (int y = 0; y < OLED_HEIGHT; y++, rowX += dyX, rowY += dyY) {
      uint8_t *row = buf + (y >> 3) * OLED_WIDTH;
      uint8_t bit = 1 << (y & 7);
      int32_t sx = rowX, sy = rowY;
      for (int x = 0; x < OLED_WIDTH; x++, sx += dxX, sy += dxY) {
        int ix = sx >> 16, iy = sy >> 16;
        if ((unsigned)ix >= OLED_WIDTH || (unsigned)iy >= OLED_HEIGHT) continue;
        if (src[(iy >> 3) * OLED_WIDTH + ix] & (1 << (iy & 7))) row[x] |= bit;
      }
    }
  }

private:
  static constexpr float FIT_A = MOTION_FIT_A;   // face half-width the fit keeps on screen
  static constexpr float FIT_B = MOTION_FIT_B;   // face half-height

  BotSpring ang;
  uint8_t level = MOTION_RARE;
  uint8_t move = MOVE_NONE;
  MotionKey keys[MOTION_MAX_KEYS];
  uint8_t nKeys = 0, keyIdx = 0;
  unsigned long keyAt = 0, nextAt = 0, lastStep = 0;
  uint8_t src[OLED_WIDTH * OLED_HEIGHT / 8];

  void key(float a, uint16_t hold, float f, float z) {
    if (nKeys < MOTION_MAX_KEYS) keys[nKeys++] = { a, hold, f, z };
  }

  void finish() {
    uint8_t was = move;
    move = MOVE_NONE;
    // Spins and flips end a whole turn round: same picture, so drop the turns
    long turns = lroundf(ang.x / 360.0f);
    ang.x -= turns * 360.0f;
    ang.t = 0;
    if (was == MOVE_SPIN2 && onDizzy) onDizzy();
  }

  void scheduleNext(unsigned long now) {
    if (level == MOTION_OFTEN) nextAt = now + (unsigned long)eyeRnd(MOTION_OFTEN_MIN_S, MOTION_OFTEN_MAX_S) * 1000UL;
    else                       nextAt = now + (unsigned long)eyeRnd(MOTION_RARE_MIN_S, MOTION_RARE_MAX_S) * 1000UL;
  }

  // Gentle moves most of the time, the big ones now and then
  uint8_t pickMove() {
    static const uint8_t weights[MOVE_NUM] = { 0, 30, 15, 15, 10, 15, 5, 10 };
    int total = 0;
    for (uint8_t i = 0; i < MOVE_NUM; i++) total += weights[i];
    int r = random(total);
    for (uint8_t i = 0; i < MOVE_NUM; i++) { if (r < weights[i]) return i; r -= weights[i]; }
    return MOVE_TILT;
  }
};

FaceMotion motion;

#endif // FACE_MOTION_H
