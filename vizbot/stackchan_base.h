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

// power_log.h (included after this header): one row in the power log.
void plogRow(const char* event, const char* note = "");

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

// Si12T (head touch chip, 0x68) registers 0x02..0x12, read raw. The boot copy
// is taken before TouchSensor.begin() resets the chip, so it shows the state
// the chip was left in by the previous run. Shown by /debug/touchraw.
#define SC_SI12T_REG_FIRST 0x02
#define SC_SI12T_REG_COUNT 17
static uint8_t scSi12tBootRegs[SC_SI12T_REG_COUNT];
static bool    scSi12tBootOk[SC_SI12T_REG_COUNT];
inline void scSi12tReadRegs(uint8_t* out, bool* ok) {
  for (int i = 0; i < SC_SI12T_REG_COUNT; i++) {
    out[i] = 0;
    ok[i] = M5.In_I2C.readRegister(0x68, SC_SI12T_REG_FIRST + i, &out[i], 1, 100000);
  }
}

class VizStackChan : public m5::M5StackChan_Class {
public:
  void beginHardware() {
    scSi12tReadRegs(scSi12tBootRegs, scSi12tBootOk);
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
// Top speed on BSP's 0-1000 scale. It was 900; the quickest gestures (nod,
// shake) are the hardest thing the gears and the battery see, so cap them.
#define SC_SERVO_MAX_SPEED    700

inline int scSpeedFromTimeMs(uint16_t timeMs) {
  if (timeMs < SC_SERVO_MIN_MOVE_MS) timeMs = SC_SERVO_MIN_MOVE_MS;
  if (timeMs > 2000) timeMs = 2000;
  long sp = map((long)timeMs, (long)SC_SERVO_MIN_MOVE_MS, 2000L, (long)SC_SERVO_MAX_SPEED, 120L);
  return (int)constrain(sp, 120L, (long)SC_SERVO_MAX_SPEED);
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

// Camera: stackchan_camera.h (GC0308 photos + motion). Photo FS remains a stub.
#include "stackchan_camera.h"

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
// When the head will be still again (millis). The camera's motion check skips
// frames until then, so the head's own movement never reads as someone arriving.
volatile uint32_t scHeadBusyUntilMs = 0;

// True while the battery guard (below) has parked the head and cut the servo
// rail. Moves are dropped and the freeze watchdog stands down until it clears.
volatile bool scServosResting = false;
inline void scMarkHeadBusy(uint16_t timeMs) {
  uint32_t until = millis() + timeMs + 1200;
  if ((int32_t)(until - scHeadBusyUntilMs) > 0) scHeadBusyUntilMs = until;
}

inline void scMoveYaw(int angleTenths, uint16_t timeMs = 500) {
  if (!sysStatus.scServoXReady || scServosResting) return;
  scMarkHeadBusy(timeMs);
  scChan.Motion.moveYaw(angleTenths, scSpeedFromTimeMs(timeMs));
}

inline void scMovePitch(int angleTenths, uint16_t timeMs = 500) {
  if (!sysStatus.scServoYReady || scServosResting) return;
  scMarkHeadBusy(timeMs);
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

// Power-cycle the servo rail and re-home. Called by the freeze watchdog below
// and by POST /bot/servo/reinit.
inline bool scServoAnswers(uint8_t id);   // servo freeze watchdog, below

// After the rail comes up: wait until both servos answer. Do NOT command a move
// before this returns true. Moves sent while they are still booting are lost,
// and BSP starts a move from the servo's read position: a failed read there
// clamps to the range floor and turns a glide into a full-torque slam.
// Ping rather than read angles: BSP's getCurrentAngle() falls back to the
// animation value when a read fails, so angles look fine on a dead bus.
inline bool scWaitServosAnswer(uint32_t maxMs) {
  uint32_t started = millis();
  while (millis() - started < maxMs) {
    delay(100);
    if (scServoAnswers(SC_SERVO_X_ID) && scServoAnswers(SC_SERVO_Y_ID)) return true;
  }
  return false;
}

inline bool scRecoverServos() {
  if (scServosResting) return false;   // the rail is off on purpose: battery guard
  // Capture the pose BEFORE cutting power so the head can glide back to where
  // it was instead of snapping to home. During a freeze the servos don't answer,
  // so this is BSP's animation value (its fallback for a failed read): where the
  // head was last told to be, which is close enough to glide back from.
  int yaw   = scChan.Motion.getCurrentYawAngle();
  int pitch = scChan.Motion.getCurrentPitchAngle();

  // Dip to the pitch floor BEFORE cutting power. When the rail drops, pitch
  // torque vanishes and the head free-falls to its sag point. Parked at the
  // floor there is nowhere left to fall. (A frozen servo ignores this, so a
  // recovery from a real freeze still drops the head once.)
  scMovePitch(SC_SERVO_Y_MIN_DEG * 10, 1200);
  delay(1300);

  plogRow("RAIL_CYCLE");   // written before the cut: if the unit dies here, this is its last row
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

  // A still head beats a half-done glide: see scWaitServosAnswer().
  if (!scWaitServosAnswer(2500)) {
    DBGLN("  reinit: bus not answering after power-up — skipping repositioning");
    plogRow("RAIL_FAIL");
    return false;
  }
  plogRow("RAIL_OK");

  // Glide back to the pre-cut pose. Floor values mean that read failed too —
  // fall back to a slow re-home.
  bool haveYaw   = (yaw   > -1270);
  bool havePitch = (pitch > 5);
  scMoveYaw(haveYaw ? yaw : 0, 1800);
  scMovePitch(havePitch ? pitch : SC_SERVO_Y_HOME_DEG * 10, 1800);
  return true;
}

// ============================================================================
// Servo freeze watchdog
// ============================================================================
// What a head stall really is: both servos stop answering on the bus at once,
// and stop obeying moves too, and stay that way until the servo rail is cycled.
// Bench tests on 2026-11-05 pinned it to the hardware: it happens only while the
// servo motors are driven, with M5's own BSP and stock idle motion as much as
// with ours, at full or half torque, on battery or USB, with or without a
// battery sag. A head that only holds still, or is turned by hand with torque
// off, never locks up. Firmware can't stop it, so detect it fast and recover.
//
// Ping both servos every second; after 3 checks in a row where a ready servo
// never answers, cycle the rail. The pings go through a second handle on the
// same UART (BSP keeps its bus object private) but hold BSP's own Motion mutex
// while they do, so they never collide with the motion task's traffic.
#include <drivers/FTServo_Arduino/src/SCSCL.h>
#include <new>
#include "mem.h"

#define SC_SERVO_CHECK_MS     1000
#define SC_SERVO_DEAD_CHECKS  3
#define SC_SERVO_BACKOFF_MS   15000   // after a recovery, before checking again
#define SC_SERVO_BACKOFF_MAX  300000  // doubles while recoveries don't help (servo missing?)

static uint16_t scServoReboots     = 0;   // exposed in /state
static uint32_t scNextServoCheckMs = 0;
static uint8_t  scServoDeadChecks  = 0;
static uint32_t scServoBackoffMs   = SC_SERVO_BACKOFF_MS;

// Motion::_mutex is private, and every Motion call (including the 50 Hz update
// task) holds it for its bus I/O. An explicit template instantiation may name a
// private member, so this hands back a pointer-to-member for it without
// patching BSP. Hold scBusLock around any direct bus access.
template <typename Tag, typename Tag::type M>
struct ScPrivateMember { friend typename Tag::type scGet(Tag) { return M; } };
struct ScMotionMutexTag {
  typedef std::mutex stackchan::motion::Motion::*type;
  friend type scGet(ScMotionMutexTag);
};
template struct ScPrivateMember<ScMotionMutexTag, &stackchan::motion::Motion::_mutex>;

struct ScBusLock {
  std::lock_guard<std::mutex> g;
  ScBusLock() : g(scChan.Motion.*scGet(ScMotionMutexTag())) {}
};

// The second bus handle. SCSCL carries 2 KB of angle-limit tables, and as a
// static it sat in internal RAM, where the camera needs one contiguous 30 KB
// DMA block at boot: those 2 KB left the largest free block 12 bytes short and
// the camera failed to start. Build it in PSRAM on first use instead.
inline SCSCL& scProbe() {
  static SCSCL* bus = nullptr;
  if (!bus) {
    void* mem = memAlloc(MEM_SERVO, sizeof(SCSCL), MEM_BIG_ONLY);
    bus = mem ? new (mem) SCSCL() : new SCSCL();
    bus->uart_num  = UART_NUM_1;   // BSP installed the driver; share it
    bus->IOTimeOut = 10;
  }
  return *bus;
}

inline bool scServoAnswers(uint8_t id) {
  ScBusLock lock;
  for (int i = 0; i < 3; i++) {
    if (scProbe().Ping(id) == id) return true;
    delay(2);
  }
  return false;
}

// Call once per loop.
inline void scServoWatchdogTick() {
  if (!sysStatus.scServoXReady && !sysStatus.scServoYReady) return;
  if (scServosResting) return;   // rail off on purpose: silence is expected
  uint32_t now = millis();
  if ((int32_t)(now - scNextServoCheckMs) < 0) return;
  scNextServoCheckMs = now + SC_SERVO_CHECK_MS;

  bool deaf = (sysStatus.scServoXReady && !scServoAnswers(SC_SERVO_X_ID)) ||
              (sysStatus.scServoYReady && !scServoAnswers(SC_SERVO_Y_ID));
  if (!deaf) { scServoDeadChecks = 0; return; }
  if (++scServoDeadChecks < SC_SERVO_DEAD_CHECKS) return;

  DBGLN("Servos stopped answering — cycling the rail");
  scServoDeadChecks = 0;
  scServoReboots++;
  scMarkHeadBusy(5000);   // the rail cycle bows and lifts the head
  // A servo that's missing or broken never comes back: don't stall the loop and
  // cycle the rail every 18 s forever, back off up to 5 min instead.
  if (scRecoverServos()) scServoBackoffMs = SC_SERVO_BACKOFF_MS;
  else scServoBackoffMs = min(scServoBackoffMs * 2, (uint32_t)SC_SERVO_BACKOFF_MAX);
  scNextServoCheckMs = millis() + scServoBackoffMs;
}

// ============================================================================
// Battery care
// ============================================================================
// The base cell is a small 550 mAh pack that sits on the charger for most of
// its life and feeds the servo boost directly, so it is the part that wears.
//
// Charging: stop at 4.1 V instead of 4.2 V and charge at 300 mA. A cell held
// full at 4.2 V around the clock ages fastest; 4.1 V costs about a tenth of
// the capacity. M5Unified's defaults charge it at well over 1C.
//
// Low battery (2 s checks, 5 in a row to act):
//   below 3.55 V  park the head at its floor, cut the servo rail and the LEDs
//   below 3.30 V  and still draining: switch the unit off, before the cell is
//                 run flat. Draining means no USB, or USB that is plugged in
//                 but not keeping up (current still leaving the cell).
//   above 3.75 V  after at least a minute of rest: servo rail and LEDs back
//                 on, head back to home. The minute keeps the rebound in
//                 voltage when the load comes off from waking it straight away.
// The two rest thresholds can be set from the build flags, for bench tests.
#define SC_CHARGE_MV        4100
#define SC_CHARGE_MA        300
#ifndef SC_BATT_REST_V
#define SC_BATT_REST_V      3.55f
#endif
#ifndef SC_BATT_RESUME_V
#define SC_BATT_RESUME_V    3.75f
#endif
#define SC_BATT_OFF_V       3.30f
#define SC_BATT_DRAIN_A     0.05f    // INA226: positive is current leaving the cell
#define SC_BATT_CHECK_MS    2000
#define SC_BATT_CHECKS      5
#define SC_BATT_MIN_REST_MS 60000
#define SC_POWER_LOCK_MS    500      // how long a power switch waits for the I2C bus

// The CoreS3's 5 V output. The base's LED ring is powered from it, so it has to
// be on for the LEDs to light. On one unit, switching it on stops USB power
// reaching the charger, and that unit must keep it off. So the choice is saved
// per unit (NVS "power" / "ext5v", default on) and found out by test, at boot
// when USB is plugged in and again the first time the battery guard rests a
// unit that started on battery (sc5vBlocksUsb, sc5vLearnAtRest).
static bool sc5vWanted  = true;
static bool sc5vAutoOff = false;   // this boot's check switched it off: logged once

inline void sc5vSave(bool on) {
  sc5vWanted = on;
  nvs_handle_t h;
  if (nvs_open("power", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u8(h, "ext5v", on ? 1 : 0);
  nvs_commit(h);
  nvs_close(h);
}

// One bus transaction each. False: the bus was busy and nothing was switched
// or read. (Before the task manager starts there is no lock to wait for.)
inline bool sc5vSwitch(bool on) {
  ScI2cLock lock(SC_POWER_LOCK_MS);
  if (!lock.held) return false;
  M5.Power.setExtOutput(on);
  return true;
}

inline bool scUsbPresent(bool* present) {
  ScI2cLock lock(SC_POWER_LOCK_MS);
  if (!lock.held) return false;
  *present = M5.Power.Axp2101.getVBUSVoltage() > 4.0f;
  return true;
}

// Does switching the 5 V output on take USB away from the charger? Call with
// the output off and USB present. True only when USB went away with the output
// on AND came back with it off, and then the output is left off. A cable pulled
// during the test does only the first half, so it is not mistaken for the
// fault. In every other case the output is left on.
inline bool sc5vBlocksUsb() {
  bool usb = false;
  if (!sc5vSwitch(true)) return false;
  delay(300);
  if (!scUsbPresent(&usb) || usb) return false;   // still there: the output is harmless
  if (!sc5vSwitch(false)) return false;
  delay(400);
  if (scUsbPresent(&usb) && usb) return true;     // gone with it on, back with it off
  sc5vSwitch(true);                               // USB really went away: nothing learned
  return false;
}

// Call right after M5.begin(): M5Unified has just written its own charger setup,
// and vizbot.ino asked it to leave the 5 V output off.
inline void scBatteryCareBegin() {
  M5.Power.Axp2101.setChargeVoltage(SC_CHARGE_MV);
  M5.Power.Axp2101.setChargeCurrent(SC_CHARGE_MA);

  nvs_handle_t h;
  uint8_t saved = 1;
  if (nvs_open("power", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, "ext5v", &saved);
    nvs_close(h);
  }
  sc5vWanted = saved != 0;
  if (!sc5vWanted) return;

  bool usb = false;
  scUsbPresent(&usb);
  if (!usb) {
    sc5vSwitch(true);   // on battery: nothing to test against yet (see sc5vLearnAtRest)
  } else if (sc5vBlocksUsb()) {
    sc5vSave(false);
    sc5vAutoOff = true;
  }
}

static uint32_t scRestSinceMs = 0;

// Park the head, then cut the servo rail and the LED ring's supply. False: the
// bus was busy and nothing was switched, so the caller tries again next check.
inline bool scRestServos() {
  if (scServosResting) return true;
  // Park at the pitch floor first: with the rail off there is no torque, and
  // from the floor the head has nowhere to fall.
  scMovePitch(SC_SERVO_Y_MIN_DEG * 10, 1200);
  delay(1300);
  {
    ScI2cLock lock(SC_POWER_LOCK_MS);
    if (!lock.held) return false;
    scChan.setServoPowerEnabled(false);
    M5.Power.setExtOutput(false);   // the LED ring's supply
  }
  scServosResting = true;
  scRestSinceMs = millis();
  return true;
}

// Servo rail and LEDs back on, head back to home. False: bus busy, still resting.
inline bool scWakeServos() {
  if (!scServosResting) return true;
  {
    ScI2cLock lock(SC_POWER_LOCK_MS);
    if (!lock.held) return false;
    scChan.setServoPowerEnabled(true);
    if (sc5vWanted) M5.Power.setExtOutput(true);
  }
  delay(500);   // servos need time to boot after power-on
  const bool live = scWaitServosAnswer(2500);
  scServosResting = false;
  scServoDeadChecks = 0;
  scNextServoCheckMs = millis() + 3000;
  // Not answering yet: leave the head where it is. The freeze watchdog is back
  // on duty and recovers it the usual way.
  if (live) scGoHome(1800);
  return true;
}

// A unit that started on battery never ran the boot test. The rest has just
// cut the 5 V output: if USB shows up now where there was none a moment ago,
// the output may have been keeping it from the charger. Test it properly and
// save the answer. Leaves the output off either way (the unit is resting).
inline void sc5vLearnAtRest() {
  bool usb = false;
  delay(400);
  if (!scUsbPresent(&usb) || !usb) return;   // still no USB: it really is on battery
  if (sc5vBlocksUsb()) {
    sc5vSave(false);
    plogRow("EXT5V_OFF", "5 V output was blocking USB: switched off and saved (base LEDs stay dark)");
  }
  sc5vSwitch(false);
}

// Call once per loop.
inline void scBatteryCareTick() {
  static uint32_t nextMs = 0;
  static uint8_t low = 0, flat = 0, good = 0;
  static bool offLogged = false;
  uint32_t now = millis();
  if ((int32_t)(now - nextMs) < 0) return;
  nextMs = now + SC_BATT_CHECK_MS;

  float volts, amps;
  bool onUsb;
  {
    ScI2cLock lock(20);
    if (!lock.held) return;
    volts = scChan.getBatteryVoltage();
    amps  = scChan.getBatteryCurrent();
    onUsb = M5.Power.Axp2101.getVBUSVoltage() > 4.0f;
  }
  if (volts < 2.0f) return;   // no reading (I2C not up yet, or a failed read)

  if (sc5vAutoOff) {
    sc5vAutoOff = false;
    plogRow("EXT5V_OFF", "5 V output blocked USB at boot: switched off and saved (base LEDs stay dark)");
  }

  // Resting and waking go by battery voltage alone, plugged in or not: a unit
  // on a weak USB supply can still run its battery down. The switch-off also
  // needs the cell to be draining: no USB, or USB that is not keeping up.
  const bool draining = !onUsb || amps > SC_BATT_DRAIN_A;
  flat = (draining && volts < SC_BATT_OFF_V) ? (flat < 250 ? flat + 1 : flat) : 0;
  low  = volts < SC_BATT_REST_V ? (low < 250 ? low + 1 : low) : 0;
  good = volts >= SC_BATT_RESUME_V ? (good < 250 ? good + 1 : good) : 0;

  if (flat >= SC_BATT_CHECKS) {
    if (!offLogged) {
      offLogged = true;
      plogRow("BATT_OFF", "battery under 3.30 V and draining: switching off");
      DBGLN("Battery flat: switching off");
    }
    if (!scRestServos()) return;   // bus busy: next check
    delay(300);
    ScI2cLock lock(SC_POWER_LOCK_MS);
    if (lock.held) M5.Power.powerOff();
    return;                        // still here: bus busy or the PMU refused; next check
  }
  if (!scServosResting && low >= SC_BATT_CHECKS) {
    if (!scRestServos()) return;
    low = good = 0;
    plogRow("BATT_REST", "battery low: head parked, servo rail and LEDs off");
    DBGLN("Battery low: resting the servos and LEDs");
    if (sc5vWanted && !onUsb) sc5vLearnAtRest();
  } else if (scServosResting && good >= SC_BATT_CHECKS &&
             millis() - scRestSinceMs >= SC_BATT_MIN_REST_MS) {
    if (!scWakeServos()) return;
    low = good = 0;   // a fresh run of checks before it may rest again
    plogRow("BATT_WAKE", "battery recovered: servo rail and LEDs back on");
  }
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
