#ifndef STACKCHAN_BASE_H
#define STACKCHAN_BASE_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include "config.h"
#include "system_status.h"

// ============================================================================
// StackChan Base — Phase 1 Stubs
// ============================================================================
// All subsystem inits are no-ops that touch NO hardware. Each returns false
// (not ready) and logs a deferred message. The boot sequence calls these and
// draws DEFER status. Phase 2 replaces each stub with real driver code.
// ============================================================================

extern SystemStatus sysStatus;

inline bool scInitIoExpander() {
  DBGLN("  IO expander: deferred (Phase 2)");
  sysStatus.scIoExpanderReady = false;
  return false;
}

inline bool scInitVmEn() {
  DBGLN("  VM_EN: deferred (Phase 2)");
  sysStatus.scVmEnReady = false;
  return false;
}

inline bool scInitServoX() {
  DBGLN("  Servo X (yaw): deferred (Phase 2)");
  sysStatus.scServoXReady = false;
  return false;
}

inline bool scInitServoY() {
  DBGLN("  Servo Y (pitch): deferred (Phase 2)");
  sysStatus.scServoYReady = false;
  return false;
}

inline bool scInitBaseLeds() {
  DBGLN("  Base LEDs: deferred (Phase 2)");
  sysStatus.scBaseLedsReady = false;
  return false;
}

inline bool scInitHeadTouch() {
  DBGLN("  Head touch: deferred (Phase 2)");
  sysStatus.scHeadTouchReady = false;
  return false;
}

inline bool scInitBatteryMon() {
  DBGLN("  Battery monitor: deferred (Phase 2)");
  sysStatus.scBatteryMonReady = false;
  return false;
}

inline bool scInitCamera() {
  DBGLN("  Camera: deferred (Phase 4)");
  sysStatus.scCameraReady = false;
  return false;
}

inline bool scInitPhotoFs() {
  DBGLN("  Photo storage: deferred (Phase 4)");
  sysStatus.scPhotoFsReady = false;
  return false;
}

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_BASE_H
