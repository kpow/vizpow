#ifndef STACKCHAN_IDLE_H
#define STACKCHAN_IDLE_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include "config.h"
#include "stackchan_base.h"

// ============================================================================
// Servo Idle Behavior — personality-driven random head movement
// ============================================================================
// Two behavior modes that alternate:
//   DRIFT:  Small random movements around center (original behavior, bigger range)
//   SWEEP:  Full slow scan across the environment — surveys the room
//
// Personality controls frequency, amplitude, and sweep chance:
//   Hyper:  frequent moves, big range, sweeps often
//   Chill:  moderate moves, medium range, occasional sweeps
//   Grumpy: rare moves, small range, rare sweeps

// Forward declarations
struct BotModeState;
extern BotModeState botMode;

enum ScIdlePhase {
  IDLE_DRIFT,    // Random point-to-point movements
  IDLE_SWEEP,    // Full environment scan (left → right → center or reverse)
};

struct ScIdleServo {
  unsigned long nextMoveMs = 0;
  bool enabled = true;
  ScIdlePhase phase = IDLE_DRIFT;

  // Sweep state
  uint8_t sweepStep = 0;      // which step of the sweep we're on
  uint8_t sweepStepCount = 0;  // total steps in current sweep
  int sweepPoints[6];          // yaw waypoints for current sweep
  int sweepPitch[6];           // pitch at each waypoint
  unsigned long sweepNextMs = 0;

  // Current idle target (tenths of degrees)
  int targetYaw = 0;
  int targetPitch = SC_SERVO_Y_HOME_DEG * 10;

  // Drift counter — trigger sweep after N drifts
  uint8_t driftCount = 0;
  uint8_t driftsBeforeSweep = 4;  // adjusted per personality

  void init() {
    nextMoveMs = millis() + random(2000, 4000);
    targetYaw = 0;
    targetPitch = SC_SERVO_Y_HOME_DEG * 10;
    enabled = true;
    phase = IDLE_DRIFT;
    driftCount = 0;
    driftsBeforeSweep = 4;
  }

  void update(uint8_t personalityIndex, bool isReacting) {
    if (!enabled) return;
    if (!sysStatus.scServoXReady && !sysStatus.scServoYReady) return;
    if (isReacting) return;

    unsigned long now = millis();

    if (phase == IDLE_SWEEP) {
      updateSweep(now, personalityIndex);
      return;
    }

    // DRIFT phase
    if (now < nextMoveMs) return;

    // Personality-driven parameters
    int yawRange, pitchRange;
    uint32_t intervalMin, intervalMax;
    uint16_t moveTimeMs;
    uint8_t sweepChance;  // % chance to start sweep after driftsBeforeSweep

    switch (personalityIndex) {
      case PERSONALITY_HYPER:
        yawRange = 600;       // ±60° — big head turns
        pitchRange = 150;     // ±15°
        intervalMin = 2000;
        intervalMax = 4000;
        moveTimeMs = 400;
        sweepChance = 50;     // sweeps often
        driftsBeforeSweep = 3;
        break;

      case PERSONALITY_GRUMPY:
        yawRange = 200;       // ±20°
        pitchRange = 50;      // ±5°
        intervalMin = 8000;
        intervalMax = 15000;
        moveTimeMs = 900;
        sweepChance = 15;     // rarely sweeps
        driftsBeforeSweep = 8;
        break;

      default:  // CHILL
        yawRange = 450;       // ±45° — relaxed scanning
        pitchRange = 100;     // ±10°
        intervalMin = 3000;
        intervalMax = 7000;
        moveTimeMs = 600;
        sweepChance = 35;     // occasional sweeps
        driftsBeforeSweep = 5;
        break;
    }

    driftCount++;

    // Check if it's time for a sweep
    if (driftCount >= driftsBeforeSweep && random(100) < sweepChance) {
      startSweep(personalityIndex);
      driftCount = 0;
      return;
    }

    // 15% chance to return to center
    if (random(100) < 15) {
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

  // --- Sweep: full environment scan ---

  void startSweep(uint8_t personalityIndex) {
    phase = IDLE_SWEEP;
    sweepStep = 0;

    int pitchHome = SC_SERVO_Y_HOME_DEG * 10;
    uint16_t sweepSpeed;

    switch (personalityIndex) {
      case PERSONALITY_HYPER:
        // Fast zigzag: far left → far right → center, head bobs
        sweepStepCount = 5;
        sweepPoints[0] = -800;  sweepPitch[0] = pitchHome + 80;
        sweepPoints[1] =  800;  sweepPitch[1] = pitchHome - 60;
        sweepPoints[2] = -400;  sweepPitch[2] = pitchHome + 40;
        sweepPoints[3] =  400;  sweepPitch[3] = pitchHome - 40;
        sweepPoints[4] =    0;  sweepPitch[4] = pitchHome;
        sweepSpeed = 500;
        break;

      case PERSONALITY_GRUMPY:
        // Minimal scan: slight left → slight right → back, reluctant
        sweepStepCount = 3;
        sweepPoints[0] = -300;  sweepPitch[0] = pitchHome;
        sweepPoints[1] =  300;  sweepPitch[1] = pitchHome + 30;
        sweepPoints[2] =    0;  sweepPitch[2] = pitchHome;
        sweepSpeed = 1000;
        break;

      default:  // CHILL
        // Smooth panoramic scan: left → right → center, gentle pitch wave
        sweepStepCount = 5;
        sweepPoints[0] = -700;  sweepPitch[0] = pitchHome + 50;
        sweepPoints[1] = -300;  sweepPitch[1] = pitchHome - 30;
        sweepPoints[2] =  300;  sweepPitch[2] = pitchHome - 30;
        sweepPoints[3] =  700;  sweepPitch[3] = pitchHome + 50;
        sweepPoints[4] =    0;  sweepPitch[4] = pitchHome;
        sweepSpeed = 800;
        break;
    }

    // Start first waypoint
    scMoveYaw(sweepPoints[0], sweepSpeed);
    scMovePitch(sweepPitch[0], sweepSpeed);
    sweepNextMs = millis() + sweepSpeed + random(200, 600);  // pause at each point
  }

  void updateSweep(unsigned long now, uint8_t personalityIndex) {
    if (now < sweepNextMs) return;

    sweepStep++;
    if (sweepStep >= sweepStepCount) {
      // Sweep complete — return to drift
      phase = IDLE_DRIFT;
      nextMoveMs = now + random(3000, 6000);
      return;
    }

    uint16_t sweepSpeed;
    switch (personalityIndex) {
      case PERSONALITY_HYPER:  sweepSpeed = 500;  break;
      case PERSONALITY_GRUMPY: sweepSpeed = 1000; break;
      default:                 sweepSpeed = 800;  break;
    }

    scMoveYaw(sweepPoints[sweepStep], sweepSpeed);
    scMovePitch(sweepPitch[sweepStep], sweepSpeed);
    sweepNextMs = now + sweepSpeed + random(200, 800);
  }

  // Immediately return to home (called after reactions end)
  void returnHome(uint16_t timeMs = 500) {
    phase = IDLE_DRIFT;
    targetYaw = 0;
    targetPitch = SC_SERVO_Y_HOME_DEG * 10;
    scGoHome(timeMs);
  }
};

static ScIdleServo scIdleServo;

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_IDLE_H
