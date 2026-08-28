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
// Bus guards — both of these buses have more than one task on them
// ============================================================================
// I2C: the PY32 expander, Si12T head touch and INA226 share the bus with the
// LCD touch controller. The render loop drives the base LEDs from Core 1 at
// ~30fps while the web and cloud tasks poll battery/touch from Core 0. With no
// mutex those transactions interleave, and because PY32 digitalWrite() is a
// read-modify-write of REG_GPIO_O_L, a corrupted one silently clears bit 0 —
// which is VM_EN, the servo power rail. That is the "servos die after 5-10 min"
// bug: the rail really did go low while sysStatus.scVmEnReady still said true.
// task_manager.h already had this mutex; nothing on the stackchan path used it.
//
// (Declared here rather than included — task_manager.h is pulled in after the
// stackchan headers in vizbot.ino.)
bool i2cAcquire(uint32_t timeoutMs);
void i2cRelease();

struct ScI2cLock {
  bool held;
  explicit ScI2cLock(uint32_t timeoutMs = 50) : held(i2cAcquire(timeoutMs)) {}
  ~ScI2cLock() { if (held) i2cRelease(); }
  ScI2cLock(const ScI2cLock&) = delete;
  ScI2cLock& operator=(const ScI2cLock&) = delete;
};

// Servo bus: half-duplex UART1, written by the render loop (idle drift, touch
// reactions, health polling) and read by the web task via /state. Interleaving
// there corrupts replies — that is why servoXPos/servoYPos read -1.
static SemaphoreHandle_t scServoMutex = nullptr;

inline void scInitServoMutex() {
  if (scServoMutex == nullptr) scServoMutex = xSemaphoreCreateMutex();
}

struct ScServoLock {
  bool held;
  explicit ScServoLock(uint32_t timeoutMs = 100)
    : held(scServoMutex == nullptr
             ? true
             : xSemaphoreTake(scServoMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {}
  ~ScServoLock() { if (held && scServoMutex) xSemaphoreGive(scServoMutex); }
  ScServoLock(const ScServoLock&) = delete;
  ScServoLock& operator=(const ScServoLock&) = delete;
};

// ============================================================================
// Global driver instances (accessible for runtime use after boot)
// ============================================================================
static std::unique_ptr<m5::PY32IOExpander_Class> scIoExpander;
static SCSCL scServoBus;
static std::unique_ptr<Si12T> scTouch;
static std::unique_ptr<m5::INA226_Class> scBattMon;

// Servo zero positions (will use NVS later).
// Pitch zero measured on hardware, not inherited from the BSP: the BSP default
// of 620 put the whole reachable band (zero+80..zero+272, from the 25-85 deg
// clamp in scMovePitch) at raw 700-892, so the head could only ever look up.
// 500 centres that band on 580-772, and home (48 deg) rests at raw 653.
// Set when the servos stop answering reads. Movement is unaffected (WritePos
// needs no reply); this exists so /state can show a degraded servo link instead
// of the head silently going dead. Measured on this unit: reads succeed ~57% of
// the time with the head still tracking every command, which is a marginal RX
// path on the bus, not a firmware fault.
static bool scServoLinkDegraded = false;

static int scServoXZeroPos = 460;
static int scServoYZeroPos = 500;

// ============================================================================
// Init Functions — called by boot_sequence.h, replace Phase 1 stubs
// ============================================================================

inline bool scInitIoExpander() {
  // Created before the web/cloud tasks start, so the guards are live by the
  // time anything but the boot path touches these buses.
  scInitServoMutex();
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
// Put a servo into the state WritePos actually requires. The M5 BSP does both
// of these and vizbot never did: in PWM mode WritePos is silently IGNORED, and
// with torque disabled the servo holds position but won't track commands. Either
// state looks identical from outside — head stiff, powered, ignoring the web UI,
// cleared only by a power cycle (which resets the servo to position mode). That
// is the "totally stuck" failure.
inline void scArmServo(uint8_t id) {
  ScServoLock lock;
  scServoBus.SwitchMode(id, 0);   // 0 = position mode
  delay(5);
  scServoBus.EnableTorque(id, 1);
  delay(5);
}

inline bool scPingServo(uint8_t id, uint32_t timeoutMs = 1500) {
  uint32_t start = millis();
  do {
    {
      ScServoLock lock;
      if (scServoBus.Ping(id) >= 0) return true;
    }
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

  // Ping yaw servo (ID 1) — retries through cold-boot servo start-up.
  // A failed ping does NOT disable the head. WritePos is one-way and needs no
  // reply, so a servo whose RX path back to us is marginal still moves fine.
  // Treating "no reply" as "no servo" is what turned one flaky wire into a
  // completely dead head that only a reboot could clear. Reads are advisory.
  if (!scPingServo(SC_SERVO_X_ID)) {
    DBG("  Servo X: ping failed (err=");
    DBG(scServoBus.getLastError());
    DBGLN(") — enabling anyway, link degraded");
    sysStatus.scServoXReady = true;
    scServoLinkDegraded = true;
    scArmServo(SC_SERVO_X_ID);
    return true;
  }
  scArmServo(SC_SERVO_X_ID);
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

  // Ping pitch servo (ID 2) — same rationale as Servo X above.
  if (!scPingServo(SC_SERVO_Y_ID)) {
    DBG("  Servo Y: ping failed (err=");
    DBG(scServoBus.getLastError());
    DBGLN(") — enabling anyway, link degraded");
    sysStatus.scServoYReady = true;
    scServoLinkDegraded = true;
    scArmServo(SC_SERVO_Y_ID);
    return true;
  }
  scArmServo(SC_SERVO_Y_ID);
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
    ScI2cLock lock;
    scIoExpander->setLedColor(index, r, g, b);
  }
}

inline void scSetAllBaseLeds(uint8_t r, uint8_t g, uint8_t b) {
  if (scIoExpander && sysStatus.scBaseLedsReady) {
    ScI2cLock lock;
    for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
      scIoExpander->setLedColor(i, r, g, b);
    }
    scIoExpander->refreshLeds();
  }
}

inline void scRefreshBaseLeds() {
  if (scIoExpander && sysStatus.scBaseLedsReady) {
    ScI2cLock lock;
    scIoExpander->refreshLeds();
  }
}

inline void scSetServoPower(bool enabled) {
  if (scIoExpander) {
    ScI2cLock lock;
    scIoExpander->digitalWrite(0, enabled);
  }
}

// Move servo to raw position (0-1000 range, as BSP uses)
inline void scServoWritePos(uint8_t id, uint16_t position, uint16_t timeMs) {
  if (sysStatus.scServoXReady || sysStatus.scServoYReady) {
    ScServoLock lock;
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
  ScServoLock lock;
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
  ScServoLock lock;
  scServoBus.WritePos(SC_SERVO_Y_ID, mapped, timeMs, 0);
}

inline void scGoHome(uint16_t timeMs = 500) {
  scMoveYaw(0, timeMs);
  scMovePitch(SC_SERVO_Y_HOME_DEG * 10, timeMs);
}

inline int scReadServoPos(uint8_t id) {
  ScServoLock lock;
  return scServoBus.ReadPos(id);
}

// ============================================================================
// Runtime servo recovery — the servo rail can latch off under sustained load
// ============================================================================
// Observed 2026-08-27: after 5-10 min of use both SCS0009s go silent (Ping and
// ReadPos fail, err=1 ERR_NO_REPLY) while drawing ~0mA, with VM_EN still driven
// HIGH and the pack at a nominal 4.19V on a 3A supply. Steady voltage + zero
// current means the base's servo rail has cut out, not that the supply sagged
// or the servos stalled. A warm reset never clears it because VM_EN never drops
// — only actually toggling VM_EN (or a full power-off) brings them back.
//
// scServoXReady/scServoYReady used to be written ONLY by boot init, so once the
// rail dropped the head stayed dead until a manual reboot. The health monitor
// below now owns those flags at runtime.

inline bool scRecoverServos() {
  if (!scIoExpander) return false;

  // Drop the rail long enough for the servos to fully reset, then bring it back
  // and give them time to boot before pinging. Each write is I2C-guarded, and
  // the lock is NOT held across the delays or the pings.
  {
    ScI2cLock lock;
    scIoExpander->digitalWrite(0, false);
  }
  delay(300);
  {
    ScI2cLock lock;
    scIoExpander->digitalWrite(0, true);
  }
  delay(500);
  sysStatus.scVmEnReady = true;

  bool x = scPingServo(SC_SERVO_X_ID, 1000);
  bool y = scPingServo(SC_SERVO_Y_ID, 1000);
  scArmServo(SC_SERVO_X_ID);
  scArmServo(SC_SERVO_Y_ID);
  sysStatus.scServoXReady = x;
  sysStatus.scServoYReady = y;
  return x || y;
}

// Polls the bus and cycles VM_EN when the servos stop answering. ReadPos is the
// probe: a live servo always replies, so -1 from BOTH servos means the bus (and
// therefore the rail) is gone. Two consecutive failures are required so a single
// dropped frame mid-move can't trigger a needless power cycle.
struct ScServoHealth {
  // Read failures mean very little on this bus: measured ~43% of reads return -1
  // while the head still tracks every command, because WritePos is one-way and
  // only the servos' replies are unreliable. Tuning history, so this doesn't get
  // "fixed" back to something aggressive:
  //   2 strikes / 60s cooldown -> 16 VM_EN cycles in 25 min, disrupting a head
  //                               that was working fine.
  //   no recovery at all       -> the head did get genuinely stuck (ignoring
  //                               writes too) and needed a manual endpoint hit.
  // So: recover, but rarely. Only 3 solid minutes of silence justifies touching
  // the rail, then at most once per 10 min. A cycle costs ~800ms of head freeze
  // and is the only thing that reliably clears the stuck state.
  static constexpr uint32_t CHECK_INTERVAL_MS    = 5000;
  static constexpr uint32_t RECOVERY_COOLDOWN_MS = 600000;  // 10 min
  static constexpr uint16_t FAILS_BEFORE_RECOVER = 36;      // 36 * 5s = 3 min

  uint32_t nextCheckMs    = 0;
  uint32_t lastRecoveryMs = 0;
  uint16_t failCount      = 0;
  uint16_t recoveries     = 0;
  bool     lastRecoveryOk = false;

  void update() {
    uint32_t now = millis();
    if (now < nextCheckMs) return;
    nextCheckMs = now + CHECK_INTERVAL_MS;

    bool alive = (scReadServoPos(SC_SERVO_X_ID) >= 0) ||
                 (scReadServoPos(SC_SERVO_Y_ID) >= 0);

    if (alive) {
      failCount = 0;
      if (scServoLinkDegraded) {
        scServoLinkDegraded = false;
        DBGLN("Servo link healthy again");
      }
      return;
    }

    // Reads are failing. On its own that is NOT a reason to cut servo power —
    // the head usually keeps moving correctly while replies go missing.
    if (failCount < 65535) failCount++;

    if (failCount == FAILS_BEFORE_RECOVER && !scServoLinkDegraded) {
      scServoLinkDegraded = true;
      DBGLN("Servo reads failing — link degraded (movement usually unaffected)");
    }
    if (failCount < FAILS_BEFORE_RECOVER) return;
    if (lastRecoveryMs != 0 && (now - lastRecoveryMs) < RECOVERY_COOLDOWN_MS) return;

    // Three minutes of total silence. Without reads we cannot tell "reads broken
    // but head fine" from "head genuinely stuck", so accept the occasional
    // needless cycle: it is brief, and it is what clears the stuck state.
    DBGLN("Servo bus silent 3min — cycling VM_EN");
    lastRecoveryMs = now;
    recoveries++;
    lastRecoveryOk = scRecoverServos();
    failCount = 0;
    DBGLN(lastRecoveryOk ? "  servo recovery OK" : "  servo recovery: still no reply");
  }
};

static ScServoHealth scServoHealth;

inline void scReadTouch() {
  if (scTouch && sysStatus.scHeadTouchReady) {
    ScI2cLock lock;
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
  ScI2cLock lock;
  return scBattMon->getBusVoltage();
}

inline float scGetBatteryCurrent() {
  if (!scBattMon) return 0.0f;
  ScI2cLock lock;
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
