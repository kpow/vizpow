#ifndef STACKCHAN_BASE_H
#define STACKCHAN_BASE_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include <M5Unified.h>
#include <memory>
#include "config.h"
#include "system_status.h"
#include "drivers/PY32IOExpander/PY32IOExpander.hpp"
#include "drivers/FTServo/src/SCSCL.h"
#include "drivers/Si12T/Si12T.h"
#include "utility/power/INA226_Class.hpp"

extern SystemStatus sysStatus;

// ============================================================================
// Global driver instances (accessible for runtime use after boot)
// ============================================================================
static std::unique_ptr<m5::PY32IOExpander_Class> scIoExpander;
static SCSCL scServoBus;
static std::unique_ptr<Si12T> scTouch;
static std::unique_ptr<m5::INA226_Class> scBattMon;

// Servo zero positions (default from BSP, will use NVS later)
static int scServoXZeroPos = 460;
static int scServoYZeroPos = 620;

// ============================================================================
// Init Functions — called by boot_sequence.h, replace Phase 1 stubs
// ============================================================================

inline bool scInitIoExpander() {
  scIoExpander = std::make_unique<m5::PY32IOExpander_Class>();

  // PY32 boots slowly — poll up to 1200ms (matches BSP)
  uint32_t start = millis();
  while (true) {
    delay(200);
    if (millis() - start > 1200) {
      DBGLN("  IO Exp: timeout");
      scIoExpander.reset();
      break;
    }
    if (scIoExpander->begin()) {
      break;
    }
  }

  bool ok = (scIoExpander != nullptr);
  if (ok) {
    uint8_t ver = scIoExpander->readVersion();
    DBG("  IO Exp: v");
    DBGLN(ver);
  }
  sysStatus.scIoExpanderReady = ok;
  return ok;
}

inline bool scInitVmEn() {
  if (!scIoExpander) {
    DBGLN("  VM_EN: no IO expander");
    sysStatus.scVmEnReady = false;
    return false;
  }

  // Pin 0 = VM_EN: output, pull-up, drive HIGH to enable servo power
  scIoExpander->setDirection(0, true);
  scIoExpander->setPullMode(0, true);
  scIoExpander->digitalWrite(0, true);
  delay(500);  // Servos need time to boot after power-on

  DBGLN("  VM_EN: enabled");
  sysStatus.scVmEnReady = true;
  return true;
}

// Cold-boot ping helper. On a warm reset VM_EN stays HIGH and the servos are
// already up, so they answer the first ping instantly. On a *cold* first boot
// VM_EN has only just risen and the SCS0009 needs time to start — and any
// power-on noise on the half-duplex line can corrupt the first frame. Rather
// than guess a fixed settle delay, retry until the servo actually responds or
// we hit the deadline. (Ping() flushes the RX line internally each call.)
inline bool scPingServo(uint8_t id, uint32_t timeoutMs = 1500) {
  uint32_t start = millis();
  do {
    if (scServoBus.Ping(id) >= 0) return true;
    delay(50);
  } while (millis() - start < timeoutMs);
  return false;
}

inline bool scInitServoX() {
  if (!sysStatus.scVmEnReady) {
    DBGLN("  Servo X: no power");
    sysStatus.scServoXReady = false;
    return false;
  }

  // Init UART bus once (shared by both servos)
  static bool busReady = false;
  if (!busReady) {
    // UART1, 1MHz baud, TX=6 RX=7 (matches BSP exactly)
    busReady = scServoBus.begin(UART_NUM_1, 1000000, 6, 7);
    if (!busReady) {
      DBGLN("  Servo bus: UART1 init failed");
      sysStatus.scServoXReady = false;
      return false;
    }
    DBGLN("  Servo bus: UART1 @ 1MHz");
  }

  // Ping yaw servo (ID 1) — retries through cold-boot servo start-up — then read position
  if (!scPingServo(SC_SERVO_X_ID)) {
    DBG("  Servo X: ping failed (err=");
    DBG(scServoBus.getLastError());
    DBGLN(")");
    sysStatus.scServoXReady = false;
    return false;
  }
  int pos = scServoBus.ReadPos(SC_SERVO_X_ID);
  bool ok = (pos >= 0);
  if (ok) {
    DBG("  Servo X: ID=");
    DBG(SC_SERVO_X_ID);
    DBG(" pos=");
    DBGLN(pos);
  } else {
    DBGLN("  Servo X: read pos failed");
  }
  sysStatus.scServoXReady = ok;
  return ok;
}

inline bool scInitServoY() {
  if (!sysStatus.scVmEnReady) {
    DBGLN("  Servo Y: no power");
    sysStatus.scServoYReady = false;
    return false;
  }

  // Ping pitch servo (ID 2) — retries through cold-boot servo start-up — then read position
  if (!scPingServo(SC_SERVO_Y_ID)) {
    DBG("  Servo Y: ping failed (err=");
    DBG(scServoBus.getLastError());
    DBGLN(")");
    sysStatus.scServoYReady = false;
    return false;
  }
  int pos = scServoBus.ReadPos(SC_SERVO_Y_ID);
  bool ok = (pos >= 0);
  if (ok) {
    DBG("  Servo Y: ID=");
    DBG(SC_SERVO_Y_ID);
    DBG(" pos=");
    DBGLN(pos);
  } else {
    DBGLN("  Servo Y: read pos failed");
  }
  sysStatus.scServoYReady = ok;
  return ok;
}

inline bool scInitBaseLeds() {
  if (!scIoExpander) {
    DBGLN("  Base LEDs: no IO expander");
    sysStatus.scBaseLedsReady = false;
    return false;
  }

  // Pin 13 = RGB LED data: output, pull-up, push-pull (matches BSP)
  scIoExpander->setDirection(13, true);
  scIoExpander->setPullMode(13, true);
  scIoExpander->setDriveMode(13, false);
  scIoExpander->setLedCount(SC_BASE_LED_COUNT);
  delay(200);

  // Clear all LEDs (black)
  for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
    scIoExpander->setLedColor(i, (uint8_t)0, (uint8_t)0, (uint8_t)0);
  }
  scIoExpander->refreshLeds();
  delay(50);
  // Double-write to clear (matches BSP pattern)
  for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
    scIoExpander->setLedColor(i, (uint8_t)0, (uint8_t)0, (uint8_t)0);
  }
  scIoExpander->refreshLeds();

  DBGLN("  Base LEDs: 12x WS2812C");
  sysStatus.scBaseLedsReady = true;
  return true;
}

inline bool scInitHeadTouch() {
  // Match M5Stack StackChan-BSP: higher sensitivity gives clean, reliable reads.
  // (The previous Low/Level_0 ran the pads at the noise floor → jittery/phantom.)
  scTouch = std::make_unique<Si12T>(SI12T_Type_High, SI12T_Sensitivity_Level_4, &m5::In_I2C);
  scTouch->begin();

  // Verify by reading touch result
  scTouch->read_touch_result();
  scTouch->parse_touch_result();

  DBGLN("  Head touch: Si12T @ 0x68");
  sysStatus.scHeadTouchReady = true;
  return true;
}

inline bool scInitBatteryMon() {
  scBattMon = std::make_unique<m5::INA226_Class>(SC_BATTMON_I2C_ADDR);

  m5::INA226_Class::config_t cfg;
  cfg.shunt_res            = 0.01;
  cfg.max_expected_current = 8.19;
  scBattMon->config(cfg);

  if (!scBattMon->begin()) {
    DBGLN("  Battery: INA226 init failed");
    scBattMon.reset();
    sysStatus.scBatteryMonReady = false;
    return false;
  }

  float volts = scBattMon->getBusVoltage();
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
// Runtime API — called after boot for ongoing operation
// ============================================================================

inline void scSetBaseLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (scIoExpander && sysStatus.scBaseLedsReady) {
    scIoExpander->setLedColor(index, r, g, b);
  }
}

inline void scSetAllBaseLeds(uint8_t r, uint8_t g, uint8_t b) {
  if (scIoExpander && sysStatus.scBaseLedsReady) {
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      scIoExpander->setLedColor(i, r, g, b);
    }
    scIoExpander->refreshLeds();
  }
}

inline void scRefreshBaseLeds() {
  if (scIoExpander && sysStatus.scBaseLedsReady) {
    scIoExpander->refreshLeds();
  }
}

inline void scSetServoPower(bool enabled) {
  if (scIoExpander) {
    scIoExpander->digitalWrite(0, enabled);
  }
}

// Move servo to raw position (0-1000 range, as BSP uses)
inline void scServoWritePos(uint8_t id, uint16_t position, uint16_t timeMs) {
  if (sysStatus.scServoXReady || sysStatus.scServoYReady) {
    scServoBus.WritePos(id, position, timeMs, 0);
  }
}

// Speed limiter — enforce a minimum move duration to reduce servo noise.
// Fast moves (100-250ms) are the loud ones; slow moves (500ms+) are fine.
// Floor at 300ms caps max speed without slowing already-quiet movements.
#define SC_SERVO_MIN_MOVE_MS  300

// Move yaw servo by angle (tenths of degrees, relative to zero)
// Angle mapping: 1 step = 0.3125 deg → mapped = zero + angle * 16 / 50
inline void scMoveYaw(int angleTenths, uint16_t timeMs = 500) {
  if (!sysStatus.scServoXReady) return;
  if (timeMs < SC_SERVO_MIN_MOVE_MS) timeMs = SC_SERVO_MIN_MOVE_MS;
  int mapped = scServoXZeroPos + angleTenths * 16 / 50;
  if (mapped < 0) mapped = 0;
  if (mapped > 1000) mapped = 1000;
  scServoBus.WritePos(SC_SERVO_X_ID, mapped, timeMs, 0);
}

inline void scMovePitch(int angleTenths, uint16_t timeMs = 500) {
  if (!sysStatus.scServoYReady) return;
  if (timeMs < SC_SERVO_MIN_MOVE_MS) timeMs = SC_SERVO_MIN_MOVE_MS;
  constexpr int minTenths = SC_SERVO_Y_MIN_DEG * 10;
  constexpr int maxTenths = SC_SERVO_Y_MAX_DEG * 10;
  if (angleTenths < minTenths) angleTenths = minTenths;
  if (angleTenths > maxTenths) angleTenths = maxTenths;
  int mapped = scServoYZeroPos + angleTenths * 16 / 50;
  if (mapped < 0) mapped = 0;
  if (mapped > 1000) mapped = 1000;
  scServoBus.WritePos(SC_SERVO_Y_ID, mapped, timeMs, 0);
}

inline void scGoHome(uint16_t timeMs = 500) {
  scMoveYaw(0, timeMs);
  scMovePitch(SC_SERVO_Y_HOME_DEG * 10, timeMs);
}

inline int scReadServoPos(uint8_t id) {
  return scServoBus.ReadPos(id);
}

inline void scReadTouch() {
  if (scTouch && sysStatus.scHeadTouchReady) {
    if (scTouch->read_touch_result()) {
      scTouch->parse_touch_result();
    } else {
      // I2C read failed — don't trust stale data, report no touch this frame.
      scTouch->point_type[0] = OUTPUT_NONE;
      scTouch->point_type[1] = OUTPUT_NONE;
      scTouch->point_type[2] = OUTPUT_NONE;
    }
  }
}

inline uint8_t scGetTouchZone(uint8_t zone) {
  if (!scTouch || zone > 2) return OUTPUT_NONE;
  return scTouch->point_type[zone];
}

inline bool scIsTouched() {
  if (!scTouch) return false;
  return (scTouch->point_type[0] != OUTPUT_NONE ||
          scTouch->point_type[1] != OUTPUT_NONE ||
          scTouch->point_type[2] != OUTPUT_NONE);
}

inline float scGetBatteryVoltage() {
  if (!scBattMon) return 0.0f;
  return scBattMon->getBusVoltage();
}

inline float scGetBatteryCurrent() {
  if (!scBattMon) return 0.0f;
  return scBattMon->getShuntCurrent();
}

// ============================================================================
// Nod / Shake gestures (blocking — shared by web presets and head touch)
// ============================================================================
// These are the exact, proven move sequences the web UI head presets use. They
// block briefly (~1.5s) while the SCS0009 interpolates each move. An optional
// `pump` callback is invoked during the dwell delays so a caller can keep e.g.
// the sound engine ticking (botSounds.update()) without coupling this layer to it.

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
