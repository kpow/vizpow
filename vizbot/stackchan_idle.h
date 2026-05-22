#ifndef STACKCHAN_IDLE_H
#define STACKCHAN_IDLE_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include "config.h"
#include "stackchan_base.h"

// ============================================================================
// Servo Idle Behavior — personality-driven random head movement
// ============================================================================
// Mirrors BotLookAround (eye drift) but for physical servos.
// Personality controls frequency and amplitude:
//   Hyper:  frequent small jittery moves (2-5s interval, ±20° yaw, ±10° pitch)
//   Chill:  rare slow drifts (6-12s interval, ±15° yaw, ±8° pitch)
//   Grumpy: almost never moves (12-25s interval, ±5° yaw, ±3° pitch)
//
// Call scIdleServo.update() each frame. Automatically pauses during reactions.

// Forward declarations
struct BotModeState;
extern BotModeState botMode;

struct ScIdleServo {
  unsigned long nextMoveMs = 0;
  bool enabled = true;

  // Current idle target (tenths of degrees)
  int targetYaw = 0;
  int targetPitch = SC_SERVO_Y_HOME_DEG * 10;

  void init() {
    nextMoveMs = millis() + random(3000, 6000);
    targetYaw = 0;
    targetPitch = SC_SERVO_Y_HOME_DEG * 10;
    enabled = true;
  }

  void update(uint8_t personalityIndex, bool isReacting) {
    if (!enabled) return;
    if (!sysStatus.scServoXReady && !sysStatus.scServoYReady) return;
    if (isReacting) return;  // don't fight reaction movements

    unsigned long now = millis();
    if (now < nextMoveMs) return;

    // Personality-driven parameters
    int yawRange, pitchRange;
    uint32_t intervalMin, intervalMax;
    uint16_t moveTimeMs;

    switch (personalityIndex) {
      case PERSONALITY_HYPER:
        yawRange = 200;       // ±20°
        pitchRange = 100;     // ±10°
        intervalMin = 2000;
        intervalMax = 5000;
        moveTimeMs = 300;     // quick snappy moves
        break;

      case PERSONALITY_GRUMPY:
        yawRange = 50;        // ±5°
        pitchRange = 30;      // ±3°
        intervalMin = 12000;
        intervalMax = 25000;
        moveTimeMs = 800;     // slow reluctant
        break;

      default:  // CHILL
        yawRange = 150;       // ±15°
        pitchRange = 80;      // ±8°
        intervalMin = 6000;
        intervalMax = 12000;
        moveTimeMs = 600;     // smooth drift
        break;
    }

    // 20% chance to return to center (home position)
    if (random(100) < 20) {
      targetYaw = 0;
      targetPitch = SC_SERVO_Y_HOME_DEG * 10;
    } else {
      targetYaw = random(-yawRange, yawRange + 1);
      int pitchCenter = SC_SERVO_Y_HOME_DEG * 10;
      targetPitch = pitchCenter + random(-pitchRange, pitchRange + 1);
    }

    scMoveYaw(targetYaw, moveTimeMs);
    scMovePitch(targetPitch, moveTimeMs);

    nextMoveMs = now + random(intervalMin, intervalMax);
  }

  // Immediately return to home (called after reactions end)
  void returnHome(uint16_t timeMs = 500) {
    targetYaw = 0;
    targetPitch = SC_SERVO_Y_HOME_DEG * 10;
    scGoHome(timeMs);
  }
};

static ScIdleServo scIdleServo;

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_IDLE_H
