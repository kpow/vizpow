#ifndef STACKCHAN_BASE_H
#define STACKCHAN_BASE_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include <M5Unified.h>
#include <M5StackChan.h>
#include "config.h"
#include "system_status.h"
#include <nvs.h>

extern SystemStatus sysStatus;

// ============================================================================
// StackChan hardware layer — delegates to M5's StackChan-BSP
// ============================================================================
// This file used to hand-roll the servo bus, IO expander, head touch and
// battery monitor on top of vendored copies of M5's drivers. That divergence
// cost two days: the head would go stiff, powered and deaf to commands, and
// only a VM_EN power cycle brought it back. M5's own firmware never does this
// on identical hardware.
//
// The root problem was structural. Our servo bus was driven from BOTH the
// render loop (Core 1) and the WiFi server task (Core 0), through a lock whose
// callers ignored whether they had actually acquired it — so under load two
// tasks transmitted on a half-duplex UART at once, the servo received a
// corrupted frame, and it stayed deaf until power-cycled.
//
// BSP cannot have that failure mode: every SCS bus call is confined to
// ScsServo, reachable only through Motion, which wraps every public method in
// a BLOCKING std::lock_guard and drives the bus from its own 20ms task. One
// owner. Calling Motion from any task is safe by construction, and
// getCurrentAngle() returns a cached animation value, so telemetry never
// touches the bus at all.
//
// Everything above this layer — LED effects, idle drift, touch reactions, the
// web UI — is unchanged and still ours. Only the hardware access moved.

// The I2C bus is still shared by the PY32 expander, Si12T, INA226 and the LCD
// touch controller, and we still poll them from more than one task. BSP does
// not address that, so this mutex stays. (Declared here rather than included:
// task_manager.h is pulled in after the stackchan headers in vizbot.ino.)
bool i2cAcquire(uint32_t timeoutMs);
void i2cRelease();

struct ScI2cLock {
  bool held;
  explicit ScI2cLock(uint32_t timeoutMs = 50) : held(i2cAcquire(timeoutMs)) {}
  ~ScI2cLock() { if (held) i2cRelease(); }
  ScI2cLock(const ScI2cLock&) = delete;
  ScI2cLock& operator=(const ScI2cLock&) = delete;
};

// BSP's M5StackChan_Class::begin() calls M5.begin() itself, but vizbot already
// does that early in setup() before the display comes up. Its hardware inits
// are `protected`, so a derived class can run them without a second M5.begin().
// BSP reads each servo's zero position from NVS (namespace "servo", keys
// zero_pos_1 / zero_pos_2, int32) and falls back to its own defaults. Its pitch
// default of 620 sits this unit's head ~120 raw steps too high — the usable
// band measured on this hardware is centred on 500. Seed the key BSP reads
// rather than fudging angles afterwards, so BSP's whole 0-90 degree pitch range
// maps onto the range the head can actually reach.
#define SC_SERVO_X_ZERO_POS 460   // matches BSP default
#define SC_SERVO_Y_ZERO_POS 500   // measured on this unit (BSP default 620)

// Seed ONLY when the key is absent (first boot on a given unit). Every
// stackchan needs its own zero — 500 is right for this head, BSP's 620 may well
// be right for another — so once a unit has a stored value, whether from this
// default or from BSP's setCurrentPostionAsHome(), leave it alone. Overwriting
// on every boot would clobber per-unit calibration across the fleet.
inline void scSeedServoZero(const char* key, int32_t value) {
  nvs_handle_t h;
  if (nvs_open("servo", NVS_READWRITE, &h) != ESP_OK) return;
  int32_t cur = 0;
  if (nvs_get_i32(h, key, &cur) != ESP_OK) {   // absent only
    nvs_set_i32(h, key, value);
    nvs_commit(h);
  }
  nvs_close(h);
}

class VizStackChan : public m5::M5StackChan_Class {
public:
  void beginHardware() {
    TouchSensor.begin();
    io_expander_init();
    // Must precede servo_init(): that is where BSP reads these.
    scSeedServoZero("zero_pos_1", SC_SERVO_X_ZERO_POS);
    scSeedServoZero("zero_pos_2", SC_SERVO_Y_ZERO_POS);
    // Servo power MUST come up before servo_init(): BSP reads each servo's zero
    // position off the bus while constructing ScsServo, so with VM_EN still low
    // it initialises Motion from a dead bus and the head never moves — the
    // animation still runs and reports perfect angles, which makes it look fine
    // from /state.
    setServoPowerEnabled(true);
    delay(500);   // servos need time to boot after power-on
    servo_init();
    ina226_init();
  }
};

static VizStackChan scChan;

// BSP speaks angles in tenths of a degree (same as us) but takes a 0-1000
// speed rather than a duration. Faster moves are louder, so keep the existing
// floor on how quick a move may be.
#define SC_SERVO_MIN_MOVE_MS  300

inline int scSpeedFromTimeMs(uint16_t timeMs) {
  if (timeMs < SC_SERVO_MIN_MOVE_MS) timeMs = SC_SERVO_MIN_MOVE_MS;
  if (timeMs > 2000) timeMs = 2000;
  long sp = map((long)timeMs, (long)SC_SERVO_MIN_MOVE_MS, 2000L, 900L, 120L);
  return (int)constrain(sp, 120L, 900L);
}

// ============================================================================
// Init — called by boot_sequence.h (signatures unchanged)
// ============================================================================

inline bool scInitIoExpander() {
  // Brings up the expander, head touch, servos and INA226 in one shot. The
  // later scInit* calls just report what BSP already set up.
  static bool started = false;
  if (!started) {
    scChan.beginHardware();
    started = true;
  }
  DBGLN("  StackChan BSP: hardware init");
  sysStatus.scIoExpanderReady = true;
  return true;
}

inline bool scInitVmEn() {
  // Already enabled inside beginHardware() — it has to precede servo_init().
  // Kept so boot_sequence.h and its phase reporting stay unchanged.
  DBGLN("  VM_EN: enabled (before servo init)");
  sysStatus.scVmEnReady = true;
  return true;
}

inline bool scInitServoX() {
  // BSP's servo_init() already constructed and homed both servos, and Motion
  // owns the bus from here on. There is deliberately no boot ping: a missed
  // reply used to latch the head off for the whole session even though moving
  // it only needs one-way writes.
  DBG("  Servo X: ID=");
  DBGLN(SC_SERVO_X_ID);
  sysStatus.scServoXReady = true;
  return true;
}

inline bool scInitServoY() {
  DBG("  Servo Y: ID=");
  DBGLN(SC_SERVO_Y_ID);
  sysStatus.scServoYReady = true;
  return true;
}

inline bool scInitBaseLeds() {
  // BSP's io_expander_init() already set pin 13 up and called setLedCount(12).
  {
    ScI2cLock lock;
    scChan.showRgbColor(0, 0, 0);
  }
  DBGLN("  Base LEDs: 12x WS2812C (BSP)");
  sysStatus.scBaseLedsReady = true;
  return true;
}

inline bool scInitHeadTouch() {
  DBGLN("  Head touch: Si12T via BSP TouchSensor");
  sysStatus.scHeadTouchReady = true;
  return true;
}

inline bool scInitBatteryMon() {
  float volts;
  {
    ScI2cLock lock;
    volts = scChan.getBatteryVoltage();
  }
  DBG("  Battery: ");
  DBG(volts, 2);
  DBGLN("V");
  sysStatus.scBatteryMonReady = true;
  return true;
}

// Camera + Photo FS remain stubs (Phase 4)
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

// ============================================================================
// Runtime API — same surface as before, now backed by BSP
// ============================================================================

inline void scSetBaseLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (!sysStatus.scBaseLedsReady) return;
  ScI2cLock lock;
  scChan.setRgbColor(index, r, g, b);
}

inline void scSetAllBaseLeds(uint8_t r, uint8_t g, uint8_t b) {
  if (!sysStatus.scBaseLedsReady) return;
  ScI2cLock lock;
  scChan.showRgbColor(r, g, b);   // sets all 12 and refreshes
}

inline void scRefreshBaseLeds() {
  if (!sysStatus.scBaseLedsReady) return;
  ScI2cLock lock;
  scChan.refreshRgb();
}

inline void scSetServoPower(bool enabled) {
  ScI2cLock lock;
  scChan.setServoPowerEnabled(enabled);
}

// Move yaw by angle (tenths of degrees). Motion is internally mutex-guarded,
// so this is safe to call from the render loop, the web task or anywhere else.
inline void scMoveYaw(int angleTenths, uint16_t timeMs = 500) {
  if (!sysStatus.scServoXReady) return;
  scChan.Motion.moveYaw(angleTenths, scSpeedFromTimeMs(timeMs));
}

inline void scMovePitch(int angleTenths, uint16_t timeMs = 500) {
  if (!sysStatus.scServoYReady) return;
  constexpr int minTenths = SC_SERVO_Y_MIN_DEG * 10;
  constexpr int maxTenths = SC_SERVO_Y_MAX_DEG * 10;
  if (angleTenths < minTenths) angleTenths = minTenths;
  if (angleTenths > maxTenths) angleTenths = maxTenths;
  scChan.Motion.movePitch(angleTenths, scSpeedFromTimeMs(timeMs));
}

inline void scGoHome(uint16_t timeMs = 500) {
  scMoveYaw(0, timeMs);
  scMovePitch(SC_SERVO_Y_HOME_DEG * 10, timeMs);
}

// Current angle in tenths of a degree. This is BSP's cached animation value,
// NOT a bus read — polling it from the web task costs nothing and cannot
// collide with a move in progress.
inline int scReadServoPos(uint8_t id) {
  if (id == SC_SERVO_X_ID) return scChan.Motion.getCurrentYawAngle();
  if (id == SC_SERVO_Y_ID) return scChan.Motion.getCurrentPitchAngle();
  return -1;
}

// Power-cycle the servo rail and re-home. Kept as a manual escape hatch behind
// POST /bot/servo/reinit; with BSP owning the bus it should never be needed.
inline bool scRecoverServos() {
  // Capture the pose BEFORE cutting power (reads still answer during a stall,
  // per a day of observation) so the head can glide back to where it was
  // instead of sagging and then snapping to home — the old goHome(500) here
  // made every scheduled reinit a visible jump.
  int yaw   = scChan.Motion.getCurrentYawAngle();
  int pitch = scChan.Motion.getCurrentPitchAngle();

  {
    ScI2cLock lock;
    scChan.setServoPowerEnabled(false);
  }
  delay(300);
  {
    ScI2cLock lock;
    scChan.setServoPowerEnabled(true);
  }
  delay(500);

  // Do NOT command a move until the bus actually answers reads again. BSP
  // teleports its spring animation to getCurrentAngle() (a real ReadPos) at
  // the start of every move; while the servo is still booting that read fails
  // and clamps to the angle floor (-1280 yaw / 0 pitch), so a "gentle glide"
  // springs across the entire range at full torque — hard enough to walk the
  // whole robot across a desk. Poll until reads are sane, and if they never
  // are, command nothing: a still head beats a slamming one, and the next
  // idle-drift move will re-sync via the same teleport once reads recover.
  bool live = false;
  uint32_t started = millis();
  while (millis() - started < 2500) {
    delay(100);
    int y = scChan.Motion.getCurrentYawAngle();
    int p = scChan.Motion.getCurrentPitchAngle();
    if (y > -1270 && p > 5) { live = true; break; }
  }
  if (!live) {
    DBGLN("  reinit: bus not answering after power-up — skipping repositioning");
    return false;
  }

  // Glide back to the pre-cut pose. Floor values mean that read failed too —
  // fall back to a slow re-home.
  bool haveYaw   = (yaw   > -1270);
  bool havePitch = (pitch > 5);
  scMoveYaw(haveYaw ? yaw : 0, 1800);
  scMovePitch(havePitch ? pitch : SC_SERVO_Y_HOME_DEG * 10, 1800);
  return true;
}

// ============================================================================
// Scheduled servo reinit — blind rail cycle every 5 minutes
// ============================================================================
// After a day of observation: the head stalls after hours of running, the bus
// still ANSWERS ReadPos during a stall (the angle-floor watchdog below in git
// history never fired), and a manual /bot/servo/reinit recovers it 100% of the
// time at a cost of ~1s. So the failure is not the bus going deaf — the motion
// layer stops producing movement — and detection by reads is a dead end.
//
// Until the trigger is found, just cycle the rail on a schedule. Worst-case
// dead time drops from "until someone notices" (30+ min) to 5 minutes, and the
// ~1s hiccup + re-home is invisible next to idle drift's constant motion.
//
// Best lead for the real cause if anyone picks this up: stalls did not occur
// during chill mode, which is exactly when idle drift's constant retargeting
// of BSP's spring animation is suspended — and that retargeting is the one
// thing vizbot does that M5's never-stalling demo doesn't. Soak with idle
// drift disabled to confirm.
#ifndef SC_SERVO_CYCLE_MS
#define SC_SERVO_CYCLE_MS 300000UL   // 5 minutes (0 disables)
#endif

static uint32_t scNextServoCycleMs = SC_SERVO_CYCLE_MS;
static uint16_t scServoReboots     = 0;   // exposed in /state

// Call once per loop.
inline void scServoWatchdogTick() {
  if (SC_SERVO_CYCLE_MS == 0) return;
  uint32_t now = millis();
  if ((int32_t)(now - scNextServoCycleMs) < 0) return;
  scNextServoCycleMs = now + SC_SERVO_CYCLE_MS;

  DBGLN("Scheduled servo reinit (5min)");
  scServoReboots++;
  scRecoverServos();
}

// ============================================================================
// Head touch
// ============================================================================
// BSP's getIntensities() returns 0-3 per zone, indexed 0=Front, 1=Middle,
// 2=Back — identical scale to the Si12T output_e enum this used to return.
// Our callers index the other way round (0=Back), so flip here and leave
// stackchan_touch.h untouched.

inline void scReadTouch() {
  if (!sysStatus.scHeadTouchReady) return;
  ScI2cLock lock;
  scChan.TouchSensor.update();
}

inline uint8_t scGetTouchZone(uint8_t zone) {
  if (zone > 2) return 0;
  return scChan.TouchSensor.getIntensities()[2 - zone];
}

inline bool scIsTouched() {
  const auto& v = scChan.TouchSensor.getIntensities();
  return (v[0] != 0 || v[1] != 0 || v[2] != 0);
}

inline float scGetBatteryVoltage() {
  ScI2cLock lock;
  return scChan.getBatteryVoltage();
}

inline float scGetBatteryCurrent() {
  ScI2cLock lock;
  return scChan.getBatteryCurrent();
}

// ============================================================================
// Nod / Shake gestures (blocking — shared by web presets and head touch)
// ============================================================================

inline void scGestureDelay(uint16_t ms, void (*pump)()) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    if (pump) pump();
    delay(4);
  }
}

// Nod "yes": dip pitch 15° below home and back, 3 cycles, ending at home.
inline void scNodGesture(void (*pump)() = nullptr) {
  if (!sysStatus.scServoYReady) return;
  constexpr int home = SC_SERVO_Y_HOME_DEG * 10;
  for (int i = 0; i < 3; i++) {
    scMovePitch(home - 150, 250);
    scGestureDelay(250, pump);
    scMovePitch(home, 250);
    scGestureDelay(250, pump);
  }
}

// Shake "no": hold pitch, swing yaw ±25°, 3 cycles, recenter.
inline void scShakeGesture(void (*pump)() = nullptr) {
  if (!sysStatus.scServoXReady) return;
  for (int i = 0; i < 3; i++) {
    scMoveYaw(250, 200);
    scGestureDelay(200, pump);
    scMoveYaw(-250, 200);
    scGestureDelay(200, pump);
  }
  scMoveYaw(0, 250);
}

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_BASE_H
