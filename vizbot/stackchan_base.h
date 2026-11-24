#ifndef STACKCHAN_BASE_H
#define STACKCHAN_BASE_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include <M5Unified.h>
#include <M5StackChan.h>
#include "config.h"
#include "system_status.h"
#include "ao_core.h"   // Active Object core: events, timeline (stage 1)
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
bool i2cTry(uint32_t timeoutMs);
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

// ---- Head touch chip start --------------------------------------------------
// BSP's TouchSensor.begin() lets the chip out of its reset first and writes the
// settings afterwards. A chip that has just been powered still holds its
// power-on settings at that reset, and it then reports touches nobody made, on
// all pads at once, for the whole run (seen in /debug/touchraw: 71 contacts in
// five minutes, hands off). A chip that kept its power across an ESP restart
// already holds the settings when the reset comes, and it stays quiet. So the
// settings go in first, then the reset, which is what the quiet case does.
// It is done again after a supply rail is switched: BSP's own notes say that
// skews the chip's idle reference.
#define SC_TOUCH_QUIET_MS 1500                       // no taps this long after a chip start
static uint32_t scTouchStarts = 0;                   // chip starts this run
static uint32_t scTouchStartFails = 0;               // ...of which a write was refused
static volatile uint32_t scTouchQuietUntilMs = 0;    // contacts before this are not taps

inline bool scSi12tWrite(uint8_t reg, uint8_t value) {
  return M5.In_I2C.writeRegister8(0x68, reg, value, 100000);
}

// Caller holds the I2C lock. The values are the ones BSP sets.
inline bool scTouchChipStartLocked() {
  bool ok = true;
  for (uint8_t r = 0x02; r <= 0x06; r++) ok &= scSi12tWrite(r, 0x33);   // sensitivity
  ok &= scSi12tWrite(0x08, 0x22);                                        // response time
  for (uint8_t r = 0x0A; r <= 0x0F; r++) ok &= scSi12tWrite(r, 0x00);   // every channel on and calibrating
  ok &= scSi12tWrite(0x09, 0x0F);                                        // reset...
  ok &= scSi12tWrite(0x09, 0x07);                                        // ...and run: the reference is taken now
  scTouchStarts++;
  if (!ok) scTouchStartFails++;
  scTouchQuietUntilMs = millis() + SC_TOUCH_QUIET_MS;
  return ok;
}

// From any task: a supply rail was switched. Published as an event; TouchAO
// takes a fresh reference once the rail has settled AND the bot is still.
inline void scTouchAskRestart(uint16_t inMs) {
  aoPublish(SIG_RAIL_SWITCH, SRC_POWER, inMs);
}

// Debug only (/debug/touchraw?cold=1): put the power-on settings back, then run
// BSP's order, to bring the fault on without waiting for a cold start.
inline void scTouchChipColdBspLocked() {
  for (uint8_t r = 0x02; r <= 0x07; r++) scSi12tWrite(r, 0xBB);
  scSi12tWrite(0x08, 0x22);
  scSi12tWrite(0x09, 0x05);
  scSi12tWrite(0x0A, 0xFE); scSi12tWrite(0x0B, 0x3F);
  scSi12tWrite(0x0C, 0xFE); scSi12tWrite(0x0D, 0x3F);
  scSi12tWrite(0x0E, 0x00); scSi12tWrite(0x0F, 0x00);
  delay(50);
  for (uint8_t r = 0x0A; r <= 0x0F; r++) scSi12tWrite(r, 0x00);
  scSi12tWrite(0x09, 0x0F);
  scSi12tWrite(0x09, 0x07);
  scSi12tWrite(0x08, 0x22);
  for (uint8_t r = 0x02; r <= 0x06; r++) scSi12tWrite(r, 0x33);
}

class VizStackChan : public m5::M5StackChan_Class {
public:
  void beginHardware() {
    scSi12tReadRegs(scSi12tBootRegs, scSi12tBootOk);
    TouchSensor.begin();   // makes BSP's driver object; the chip is started properly below
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
    // Last, with the servo rail up: settings first, then the reset (see above).
    // Once more shortly after the loop starts, when WiFi and the LEDs are up too.
    // TouchAO takes the reference that counts later, once the bot is still: a
    // chip powered up from cold takes a bad one under the homing load (2026-11-22).
    scTouchChipStartLocked();   // boot: no other task is on the bus yet
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

// Base LEDs (stage 4 step 3, docs/ao-plans/bus-ui.md). Setting a colour only
// stages it; scRefreshBaseLeds() pushes the frame. It used to be 12 colour
// writes + a refresh read-modify-write every frame (~420 I2C transactions a
// second at 30 fps, most of the bus traffic). Now it is one 24-byte write to the
// PY32's LED RAM + one refresh write, only when a colour changed, plus a re-push
// every 2 s in case the PY32 lost its LED RAM. On a busy bus the frame is
// skipped (the next one carries the same colours), never written unlocked.
#define SC_PY32_ADDR        0x6F
#define SC_PY32_LED_CFG     0x24
#define SC_PY32_LED_RAM     0x30
#define SC_PY32_LED_REFRESH 0x40
#define SC_LED_REPUSH_MS    2000
static uint16_t scLedNext[SC_BASE_LED_COUNT];     // staged frame, RGB565
static uint16_t scLedShown[SC_BASE_LED_COUNT];    // last frame the PY32 took
static uint32_t scLedPushedMs = 0;
static int16_t  scLedCfg = -1;                    // LED_CFG, read once (refresh bit clear)
struct ScLedStats { uint32_t pushes, unchanged, lockMiss, fails; };
static ScLedStats scLedStats = {};

inline void scSetBaseLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (index >= SC_BASE_LED_COUNT) return;
  scLedNext[index] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

inline void scRefreshBaseLeds() {
  if (!sysStatus.scBaseLedsReady) return;
  const uint32_t now = millis();
  if (!i2cTry(0)) { scLedStats.lockMiss++; return; }
  if (memcmp(scLedNext, scLedShown, sizeof(scLedNext)) == 0 && now - scLedPushedMs < SC_LED_REPUSH_MS) {
    i2cRelease();
    scLedStats.unchanged++;
    return;
  }
  uint8_t buf[SC_BASE_LED_COUNT * 2];
  for (int i = 0; i < SC_BASE_LED_COUNT; i++) {
    buf[i * 2] = scLedNext[i] & 0xFF;        // low byte first, as BSP's setLedColor
    buf[i * 2 + 1] = scLedNext[i] >> 8;
  }
  bool ok = true;
  if (scLedCfg < 0) {
    uint8_t cfg = 0;
    ok = M5.In_I2C.readRegister(SC_PY32_ADDR, SC_PY32_LED_CFG, &cfg, 1, 100000);
    if (ok) scLedCfg = cfg & ~SC_PY32_LED_REFRESH;
  }
  ok = ok && M5.In_I2C.writeRegister(SC_PY32_ADDR, SC_PY32_LED_RAM, buf, sizeof(buf), 100000);
  ok = ok && M5.In_I2C.writeRegister8(SC_PY32_ADDR, SC_PY32_LED_CFG, (uint8_t)scLedCfg | SC_PY32_LED_REFRESH, 100000);
  i2cRelease();
  if (ok) {
    memcpy(scLedShown, scLedNext, sizeof(scLedNext));
    scLedPushedMs = now;
    scLedStats.pushes++;
  } else {
    scLedStats.fails++;   // retried next frame: shown != next
  }
}

inline void scSetAllBaseLeds(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < SC_BASE_LED_COUNT; i++) scSetBaseLedColor(i, r, g, b);
  scRefreshBaseLeds();
}

// False if the bus stayed busy (nothing was written).
inline bool scSetServoPower(bool enabled) {
  ScI2cLock lock(500);
  if (!lock.held) return false;
  scChan.setServoPowerEnabled(enabled);
  return true;
}

// When the head will be still again (millis). The camera's motion check skips
// frames until then, so the head's own movement never reads as someone arriving.
volatile uint32_t scHeadBusyUntilMs = 0;

// True while the battery guard (below) has parked the head and cut the servo
// rail. Moves are dropped and the freeze watchdog stands down until it clears.
volatile bool scServosResting = false;

// ---- MotionAO: the public side ------------------------------------------------
// MotionAO (end of this file) is the only code that drives the servos, the
// servo rail and the servo bus. Everyone else posts it a command, from any
// task, and returns at once: nothing here blocks, and nothing here touches
// hardware.
enum ScMoveClass  : uint8_t { SC_MC_APP = 0, SC_MC_IDLE = 1 };
enum ScGesture    : uint8_t { SC_G_NOD = 1, SC_G_SHAKE = 2 };
enum ScRestFlags  : uint8_t { SC_REST_POWEROFF = 1, SC_REST_LEARN_5V = 2 };
enum ScMotionKind : uint8_t { SC_MK_MOVE = 0, SC_MK_IDLE, SC_MK_GESTURE, SC_MK_RECOVER, SC_MK_REST, SC_MK_WAKE };
static AoActive scMotionAO;
// Last commanded angles (tenths), for /state: reading the servos from the web
// task would put it on the servo bus.
static volatile int16_t scYawTarget = 0, scPitchTarget = SC_SERVO_Y_HOME_DEG * 10;

inline bool scPostMove(bool pitch, int angleTenths, uint16_t timeMs, ScMoveClass cls) {
  AoEvt e = aoEvt(SIG_MOTION_MOVE, SRC_APP, angleTenths, timeMs);
  e.flags = (pitch ? 1 : 0) | (cls << 1);
  return aoPost(scMotionAO, e);
}
inline void scMoveYaw(int angleTenths, uint16_t timeMs = 500, ScMoveClass cls = SC_MC_APP) {
  scPostMove(false, angleTenths, timeMs, cls);
}
inline void scMovePitch(int angleTenths, uint16_t timeMs = 500, ScMoveClass cls = SC_MC_APP) {
  scPostMove(true, angleTenths, timeMs, cls);
}
inline void scGoHome(uint16_t timeMs = 500, ScMoveClass cls = SC_MC_APP) {
  scMoveYaw(0, timeMs, cls);
  scMovePitch(SC_SERVO_Y_HOME_DEG * 10, timeMs, cls);
}
// Nod "yes" / shake "no" (step tables in MotionAO). Returns at once.
inline void scNodGesture()   { aoPost(scMotionAO, aoEvt(SIG_MOTION_GESTURE, SRC_APP, SC_G_NOD)); }
inline void scShakeGesture() { aoPost(scMotionAO, aoEvt(SIG_MOTION_GESTURE, SRC_APP, SC_G_SHAKE)); }
// Cycle the servo rail and re-home (POST /bot/servo/reinit). Queued, not done.
inline bool scRecoverServos(bool homeAfter = false) {
  return aoPost(scMotionAO, aoEvt(SIG_MOTION_RECOVER, SRC_WEB, 0, homeAfter ? 1 : 0));
}

// Current angle in tenths of a degree: the last commanded target. Not a bus read.
inline int scReadServoPos(uint8_t id) {
  if (id == SC_SERVO_X_ID) return scYawTarget;
  if (id == SC_SERVO_Y_ID) return scPitchTarget;
  return -1;
}

// ---- MotionAO only ---------------------------------------------------------------
// Every servo move goes through here: it is the motion event TouchAO listens
// for (servo torque comes on with each move).
#define SC_MOTION_SETTLE_MS 300   // torque and gear settle after a move, for touch
inline void scMarkHeadBusy(uint16_t timeMs) {
  uint32_t until = millis() + timeMs + 1200;
  if ((int32_t)(until - scHeadBusyUntilMs) > 0) scHeadBusyUntilMs = until;
  aoPublish(SIG_MOTION_START, SRC_MOTION, timeMs + SC_MOTION_SETTLE_MS);
}

inline void scRawMoveYaw(int angleTenths, uint16_t timeMs) {
  if (!sysStatus.scServoXReady || scServosResting) return;
  scMarkHeadBusy(timeMs);
  scYawTarget = angleTenths;
  scChan.Motion.moveYaw(angleTenths, scSpeedFromTimeMs(timeMs));
}

inline void scRawMovePitch(int angleTenths, uint16_t timeMs) {
  if (!sysStatus.scServoYReady || scServosResting) return;
  scMarkHeadBusy(timeMs);
  constexpr int minTenths = SC_SERVO_Y_MIN_DEG * 10;
  constexpr int maxTenths = SC_SERVO_Y_MAX_DEG * 10;
  if (angleTenths < minTenths) angleTenths = minTenths;
  if (angleTenths > maxTenths) angleTenths = maxTenths;
  scPitchTarget = angleTenths;
  scChan.Motion.movePitch(angleTenths, scSpeedFromTimeMs(timeMs));
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

// One ping each, no retries: MotionAO's wait for servos to boot asks every 100 ms.
inline bool scServoAnswersOnce(uint8_t id) {
  ScBusLock lock;
  return scProbe().Ping(id) == id;
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
  scTouchAskRestart(1500);   // a rail was switched: fresh touch reference
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

// Call right after M5.begin(): M5Unified has just written its own charger setup
// and switched the 5 V output on (its default, kept by vizbot.ino).
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
  if (!sc5vWanted) {
    // M5.begin switched it on: honour this unit's saved choice. Without this the
    // output stayed on, USB never reached the charger, and the unit started on
    // battery (skunky 2026-11-24: battery rest 22 s after power-on).
    sc5vSwitch(false);
    return;
  }

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

// Resting (park the head, cut the servo rail and the LED ring's supply) and
// waking are MotionAO sequences: battery care posts SIG_MOTION_REST / WAKE.

// A unit that started on battery never ran the boot test. The rest has just
// cut the 5 V output: if USB shows up now where there was none a moment ago,
// the output may have been keeping it from the charger. Test it properly and
// save the answer. Leaves the output off either way (the unit is resting).
// MotionAO calls this 400 ms after the cut.
inline void sc5vLearnAtRest() {
  bool usb = false;
  if (!scUsbPresent(&usb) || !usb) return;   // still no USB: it really is on battery
  if (sc5vBlocksUsb()) {
    sc5vSave(false);
    plogRow("EXT5V_OFF", "5 V output was blocking USB: switched off and saved (base LEDs stay dark)");
  }
  sc5vSwitch(false);
}

// ============================================================================
// BusAO (stage 4 step 5, docs/ao-plans/bus-ui.md): the power sensors
// ============================================================================
// Once a second, on the loop thread, one locked pass reads the INA226 (battery
// volts and amps) and the AXP2101 (VBUS, VSYS). Everyone else reads this
// snapshot: /state and /bot/battery/status (the web task used to read the
// INA226 itself on every poll), battery care, the power log, the touch UI.
// A busy bus is retried on the next 100 ms tick, never read unlocked.
struct ScPowerSnap {
  float    volts, amps;
  int      vbusMv, vsysMv;
  uint32_t atMs;              // 0 = no reading yet
  uint32_t reads, misses;     // samples taken; ticks the bus was busy
};
static ScPowerSnap scPower = {};
static AoActive scBusAO;
#define SC_BUS_POWER_MS 1000

static void scBusSamplePower(uint32_t now) {
  if (scPower.atMs && now - scPower.atMs < SC_BUS_POWER_MS) return;
  ScI2cLock lock(0);
  if (!lock.held) { scPower.misses++; return; }
  ScPowerSnap p = scPower;
  p.volts = scChan.getBatteryVoltage();
  p.amps = scChan.getBatteryCurrent();
  p.vbusMv = (int)(M5.Power.Axp2101.getVBUSVoltage() * 1000);
  p.vsysMv = ((M5.Power.Axp2101.readRegister8(0x3A) & 0x3F) << 8) | M5.Power.Axp2101.readRegister8(0x3B);
  p.atMs = now ? now : 1;
  p.reads++;
  scPower = p;   // one owner thread writes; readers on other tasks take a float at a time
}

static void scBusOnEvent(const AoEvt& e) {
  if (e.sig == SIG_TICK && sysStatus.scBatteryMonReady) scBusSamplePower(millis());
}

inline void scBusAOStart() {
  scBusAO.name = "bus";
  scBusAO.onEvent = scBusOnEvent;
  scBusAO.tickMs = 100;
  aoStart(scBusAO);
}

// Call once per loop.
inline void scBatteryCareTick() {
  static uint32_t nextMs = 0;
  static uint8_t low = 0, flat = 0, good = 0;
  static bool offLogged = false;
  uint32_t now = millis();
  if ((int32_t)(now - nextMs) < 0) return;
  nextMs = now + SC_BATT_CHECK_MS;

  if (!scPower.atMs || now - scPower.atMs > 3 * SC_BUS_POWER_MS) return;   // BusAO's snapshot
  const float volts = scPower.volts, amps = scPower.amps;
  const bool onUsb = scPower.vbusMv > 4000;
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
    // MotionAO parks, cuts the rails, then switches off. Asked every check
    // until it happens: a busy AO (mid-recovery) refuses and is asked again.
    aoPost(scMotionAO, aoEvt(SIG_MOTION_REST, SRC_POWER, SC_REST_POWEROFF));
    return;
  }
  // Each ask needs a fresh run of checks (10 s): longer than a rest or wake takes.
  if (!scServosResting && low >= SC_BATT_CHECKS) {
    low = good = 0;
    const uint8_t flags = (sc5vWanted && !onUsb) ? SC_REST_LEARN_5V : 0;
    if (!aoPost(scMotionAO, aoEvt(SIG_MOTION_REST, SRC_POWER, flags))) return;
    plogRow("BATT_REST", "battery low: head parked, servo rail and LEDs off");
    DBGLN("Battery low: resting the servos and LEDs");
  } else if (scServosResting && good >= SC_BATT_CHECKS &&
             millis() - scRestSinceMs >= SC_BATT_MIN_REST_MS) {
    low = good = 0;
    if (!aoPost(scMotionAO, aoEvt(SIG_MOTION_WAKE, SRC_POWER))) return;
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

inline uint8_t scGetTouchZone(uint8_t zone) {
  if (zone > 2) return 0;
  return scChan.TouchSensor.getIntensities()[2 - zone];
}

inline bool scIsTouched() {
  const auto& v = scChan.TouchSensor.getIntensities();
  return (v[0] != 0 || v[1] != 0 || v[2] != 0);
}

// BusAO's snapshot, up to ~1 s old: no bus access from any caller's task.
inline float scGetBatteryVoltage() { return scPower.volts; }
inline float scGetBatteryCurrent() { return scPower.amps; }

// ============================================================================
// MotionAO (stage 2, docs/ao-plans/motion.md)
// ============================================================================
// The one owner of the head servos, the servo rail and the servo bus. It runs
// on the loop thread (aoDispatch) and never blocks: every wait in a gesture,
// a rail cycle, a rest or a wake is a deadline checked on its 10 ms tick.
//
// Arbitration: rail cycle, rest and wake beat gestures, which beat moves.
// A request the current state can't take is dropped and published
// (SIG_MOTION_DROPPED), never queued behind a 4 s recovery.
enum ScMotionState : uint8_t { SC_MS_STILL, SC_MS_GESTURE, SC_MS_RECOVER, SC_MS_REST, SC_MS_RESTED, SC_MS_WAKE };
enum ScMotionStepId : uint8_t {
  SC_SUB_NONE,
  SC_SUB_DIP, SC_SUB_RAIL_OFF, SC_SUB_RAIL_BOOT, SC_SUB_WAIT, SC_SUB_GLIDE,   // recover / wake
  SC_SUB_PARK, SC_SUB_LEARN, SC_SUB_POWEROFF,                               // rest
};
enum ScMotionGate : uint8_t { SC_MG_VOICE = 1 };   // the watchdog stands down in a voice turn

struct ScGestureStep { uint8_t pitch; int16_t angle; uint16_t ms; uint16_t waitMs; };
#define SC_PITCH_HOME (SC_SERVO_Y_HOME_DEG * 10)
// Nod "yes": dip pitch 15 degrees below home and back, 3 cycles, ending at home.
static const ScGestureStep SC_NOD_STEPS[] = {
  {1, SC_PITCH_HOME - 150, 250, 250}, {1, SC_PITCH_HOME, 250, 250},
  {1, SC_PITCH_HOME - 150, 250, 250}, {1, SC_PITCH_HOME, 250, 250},
  {1, SC_PITCH_HOME - 150, 250, 250}, {1, SC_PITCH_HOME, 250, 250},
};
// Shake "no": hold pitch, swing yaw +-25 degrees, 3 cycles, recenter.
static const ScGestureStep SC_SHAKE_STEPS[] = {
  {0, 250, 200, 200}, {0, -250, 200, 200},
  {0, 250, 200, 200}, {0, -250, 200, 200},
  {0, 250, 200, 200}, {0, -250, 200, 200},
  {0, 0, 250, 250},
};

struct ScMotionCtl {
  uint8_t  state = SC_MS_STILL;
  uint8_t  sub = SC_SUB_NONE;
  uint32_t atMs = 0;                 // next step is due
  uint32_t untilMs = 0;              // WAIT gives up here
  const ScGestureStep* seq = nullptr;
  uint8_t  seqLen = 0, step = 0;
  uint8_t  gates = 0;                // ScMotionGate bits, set by the loop
  uint8_t  restFlags = 0;
  bool     fromWatchdog = false, homeAfter = false;
  int      yaw = 0, pitch = 0;       // recovery glides back here
  uint32_t dropped = 0, recoveries = 0;
};
static ScMotionCtl scMo;

inline void scMotionGate(uint8_t bit, bool on) {   // loop thread
  if (on) scMo.gates |= bit; else scMo.gates &= ~bit;
}

inline bool scMotionDue(uint32_t now) { return (int32_t)(now - scMo.atMs) >= 0; }

inline void scMotionDrop(uint8_t kind) {
  scMo.dropped++;
  aoPublish(SIG_MOTION_DROPPED, SRC_MOTION, kind, scMo.state);
}

inline void scMotionEnd(uint8_t kind, bool ok, uint8_t next = SC_MS_STILL) {
  scMo.state = next;
  scMo.sub = SC_SUB_NONE;
  aoPublish(SIG_MOTION_END, SRC_MOTION, kind, ok ? 1 : 0);
}

// Short tries only: a camera frame can hold the I2C bus for seconds, and a
// long wait here would stall the loop. A miss retries on the next tick.
#define SC_MOTION_RAIL_LOCK_MS 20

inline bool scMotionRail(bool on) {
  ScI2cLock lock(SC_MOTION_RAIL_LOCK_MS);
  if (!lock.held) return false;
  scChan.setServoPowerEnabled(on);
  return true;
}

// ---- Gestures ----
inline void scMotionGestureStep(uint32_t now) {
  if (scMo.step >= scMo.seqLen) { scMotionEnd(SC_MK_GESTURE, true); return; }
  const ScGestureStep& g = scMo.seq[scMo.step++];
  if (g.pitch) scRawMovePitch(g.angle, g.ms); else scRawMoveYaw(g.angle, g.ms);
  // From the step's due time, not from now: a loop pass is up to ~50 ms, and
  // counting from now would stretch a 1.5 s nod to 1.8 s.
  scMo.atMs += g.waitMs;
  if ((int32_t)(now - scMo.atMs) > 100) scMo.atMs = now;   // after a long stall, don't bunch up
}

inline void scMotionStartGesture(uint8_t which, uint32_t now) {
  if (which == SC_G_NOD) {
    if (!sysStatus.scServoYReady) return;
    scMo.seq = SC_NOD_STEPS; scMo.seqLen = sizeof(SC_NOD_STEPS) / sizeof(SC_NOD_STEPS[0]);
  } else {
    if (!sysStatus.scServoXReady) return;
    scMo.seq = SC_SHAKE_STEPS; scMo.seqLen = sizeof(SC_SHAKE_STEPS) / sizeof(SC_SHAKE_STEPS[0]);
  }
  scMo.state = SC_MS_GESTURE;
  scMo.step = 0;
  scMo.atMs = now;
  scMotionGestureStep(now);
}

// ---- Rail cycle (freeze watchdog, /bot/servo/reinit) ----
inline void scMotionStartRecover(bool fromWatchdog, bool homeAfter, uint32_t now) {
  scMo.state = SC_MS_RECOVER;
  scMo.fromWatchdog = fromWatchdog;
  scMo.homeAfter = homeAfter;
  scMo.recoveries++;
  // Capture the pose BEFORE cutting power so the head can glide back to where
  // it was instead of snapping to home. During a freeze the servos don't
  // answer, so this is BSP's animation value: where the head was last told to
  // be, which is close enough to glide back from.
  scMo.yaw   = scChan.Motion.getCurrentYawAngle();
  scMo.pitch = scChan.Motion.getCurrentPitchAngle();
  scMarkHeadBusy(5000);   // the rail cycle bows and lifts the head
  // Dip to the pitch floor BEFORE cutting power. When the rail drops, pitch
  // torque vanishes and the head free-falls to its sag point. Parked at the
  // floor there is nowhere left to fall. (A frozen servo ignores this.)
  scRawMovePitch(SC_SERVO_Y_MIN_DEG * 10, 1200);
  scMo.sub = SC_SUB_DIP;
  scMo.atMs = now + 1300;
}

// After the rail comes up: wait until both servos answer. Never command a move
// before that. Moves sent while they are still booting are lost, and BSP starts
// a move from the servo's read position: a failed read there clamps to the
// range floor and turns a glide into a full-torque slam. Ping rather than read
// angles: BSP's getCurrentAngle() falls back to the animation value when a
// read fails, so angles look fine on a dead bus.
inline bool scMotionServosAnswer() {
  return (!sysStatus.scServoXReady || scServoAnswersOnce(SC_SERVO_X_ID)) &&
         (!sysStatus.scServoYReady || scServoAnswersOnce(SC_SERVO_Y_ID));
}

inline void scMotionFinishRecover(bool ok, uint32_t now) {
  if (scMo.fromWatchdog) {
    // A servo that's missing or broken never comes back: don't cycle the rail
    // every 18 s forever, back off up to 5 min instead.
    scServoBackoffMs = ok ? SC_SERVO_BACKOFF_MS
                          : min(scServoBackoffMs * 2, (uint32_t)SC_SERVO_BACKOFF_MAX);
    scNextServoCheckMs = now + scServoBackoffMs;
  }
  scMotionEnd(SC_MK_RECOVER, ok);
}

inline void scMotionRecoverTick(uint32_t now) {
  if (!scMotionDue(now)) return;
  switch (scMo.sub) {
    case SC_SUB_DIP:
      if (!scMotionRail(false)) { scMo.atMs = now + 20; return; }
      plogRow("RAIL_CYCLE");   // if the unit dies here, this is its last row
      scTouchAskRestart(1500);
      scMo.sub = SC_SUB_RAIL_OFF;
      scMo.atMs = now + 300;
      return;
    case SC_SUB_RAIL_OFF:
      if (!scMotionRail(true)) { scMo.atMs = now + 20; return; }
      scTouchAskRestart(1500);   // the rail was switched: fresh touch reference
      scMo.sub = SC_SUB_RAIL_BOOT;
      scMo.atMs = now + 500;     // servos boot
      return;
    case SC_SUB_RAIL_BOOT:
      scMo.sub = SC_SUB_WAIT;
      scMo.untilMs = now + 2500;
      scMo.atMs = now;
      return;
    case SC_SUB_WAIT:
      if (scMotionServosAnswer()) {
        plogRow("RAIL_OK");
        // Glide back to the pre-cut pose. Floor values mean that read failed
        // too: fall back to a slow re-home.
        const bool haveYaw = scMo.yaw > -1270, havePitch = scMo.pitch > 5;
        const int yaw   = (scMo.homeAfter || !haveYaw) ? 0 : scMo.yaw;
        const int pitch = (scMo.homeAfter || !havePitch) ? SC_SERVO_Y_HOME_DEG * 10 : scMo.pitch;
        const uint16_t ms = scMo.homeAfter ? 500 : 1800;
        scRawMoveYaw(yaw, ms);
        scRawMovePitch(pitch, ms);
        scMo.sub = SC_SUB_GLIDE;
        scMo.atMs = now + ms;
      } else if ((int32_t)(now - scMo.untilMs) >= 0) {
        // A still head beats a half-done glide.
        DBGLN("  reinit: bus not answering after power-up — skipping repositioning");
        plogRow("RAIL_FAIL");
        scMotionFinishRecover(false, now);
      } else {
        scMo.atMs = now + 100;
      }
      return;
    case SC_SUB_GLIDE:
      scMotionFinishRecover(true, now);
      return;
  }
}

// ---- Freeze watchdog (see above): 1 s pings while the head is free ----
inline void scMotionWatchdog(uint32_t now) {
  if (!sysStatus.scServoXReady && !sysStatus.scServoYReady) return;
  // Held off in a voice turn: servo noise would keep the end-of-speech
  // detector open, and a rail cycle mid-reply is jarring.
  if (scMo.gates & SC_MG_VOICE) return;
  if ((int32_t)(now - scNextServoCheckMs) < 0) return;
  scNextServoCheckMs = now + SC_SERVO_CHECK_MS;

  bool deaf = (sysStatus.scServoXReady && !scServoAnswers(SC_SERVO_X_ID)) ||
              (sysStatus.scServoYReady && !scServoAnswers(SC_SERVO_Y_ID));
  if (!deaf) { scServoDeadChecks = 0; return; }
  if (++scServoDeadChecks < SC_SERVO_DEAD_CHECKS) return;

  DBGLN("Servos stopped answering — cycling the rail");
  scServoDeadChecks = 0;
  scServoReboots++;
  aoPublish(SIG_SERVO_DEAF, SRC_MOTION);
  scMotionStartRecover(true, false, now);
}

// ---- Rest / wake (battery care) ----
inline void scMotionStartRest(uint8_t flags, uint32_t now) {
  scMo.state = SC_MS_REST;
  scMo.restFlags = flags;
  // Park at the pitch floor first: with the rail off there is no torque, and
  // from the floor the head has nowhere to fall.
  scRawMovePitch(SC_SERVO_Y_MIN_DEG * 10, 1200);
  scMo.sub = SC_SUB_PARK;
  scMo.atMs = now + 1300;
}

inline void scMotionRestTick(uint32_t now) {
  if (!scMotionDue(now)) return;
  switch (scMo.sub) {
    case SC_SUB_PARK: {
      ScI2cLock lock(SC_MOTION_RAIL_LOCK_MS);
      if (!lock.held) { scMo.atMs = now + 20; return; }
      scChan.setServoPowerEnabled(false);
      M5.Power.setExtOutput(false);   // the LED ring's supply
      scServosResting = true;
      scRestSinceMs = now;
      scTouchAskRestart(1500);        // rails were switched: fresh touch reference
      if (scMo.restFlags & SC_REST_POWEROFF) { scMo.sub = SC_SUB_POWEROFF; scMo.atMs = now + 300; }
      else if (scMo.restFlags & SC_REST_LEARN_5V) { scMo.sub = SC_SUB_LEARN; scMo.atMs = now + 400; }
      else scMotionEnd(SC_MK_REST, true, SC_MS_RESTED);
      return;
    }
    case SC_SUB_LEARN:
      sc5vLearnAtRest();
      scMotionEnd(SC_MK_REST, true, SC_MS_RESTED);
      return;
    case SC_SUB_POWEROFF: {
      ScI2cLock lock(SC_MOTION_RAIL_LOCK_MS);
      if (lock.held) M5.Power.powerOff();
      scMo.atMs = now + 20;           // still here: bus busy or the PMU refused
      return;
    }
  }
}

inline void scMotionWakeTick(uint32_t now) {
  if (!scMotionDue(now)) return;
  switch (scMo.sub) {
    case SC_SUB_RAIL_OFF: {           // the rail is off: switch it and the LEDs back on
      ScI2cLock lock(SC_MOTION_RAIL_LOCK_MS);
      if (!lock.held) { scMo.atMs = now + 20; return; }
      scChan.setServoPowerEnabled(true);
      if (sc5vWanted) M5.Power.setExtOutput(true);
      scTouchAskRestart(1500);
      scMo.sub = SC_SUB_RAIL_BOOT;
      scMo.atMs = now + 500;          // servos boot
      return;
    }
    case SC_SUB_RAIL_BOOT:
      scMo.sub = SC_SUB_WAIT;
      scMo.untilMs = now + 2500;
      scMo.atMs = now;
      return;
    case SC_SUB_WAIT: {
      const bool live = scMotionServosAnswer();
      if (!live && (int32_t)(now - scMo.untilMs) < 0) { scMo.atMs = now + 100; return; }
      scServosResting = false;
      scServoDeadChecks = 0;
      scNextServoCheckMs = now + 3000;
      // Not answering yet: leave the head where it is. The freeze watchdog is
      // back on duty and recovers it the usual way.
      if (live) { scRawMoveYaw(0, 1800); scRawMovePitch(SC_SERVO_Y_HOME_DEG * 10, 1800); }
      scMotionEnd(SC_MK_WAKE, live);
      return;
    }
  }
}

// ---- Events ----
inline void scMotionOnEvent(const AoEvt& e) {
  const uint32_t now = millis();
  const uint8_t st = scMo.state;
  switch (e.sig) {
    case SIG_MOTION_MOVE: {
      const bool idle = (e.flags >> 1) == SC_MC_IDLE;
      if (st != SC_MS_STILL) { scMotionDrop(idle ? SC_MK_IDLE : SC_MK_MOVE); return; }
      if (e.flags & 1) scRawMovePitch(e.a, e.b); else scRawMoveYaw(e.a, e.b);
      return;
    }
    case SIG_MOTION_GESTURE:
      if (st != SC_MS_STILL) { scMotionDrop(SC_MK_GESTURE); return; }
      scMotionStartGesture(e.a, now);
      return;
    case SIG_MOTION_RECOVER:
      // Re-entry guard: one rail cycle at a time, and none while resting.
      if (st != SC_MS_STILL && st != SC_MS_GESTURE) { scMotionDrop(SC_MK_RECOVER); return; }
      scMotionStartRecover(e.a == 1, e.b == 1, now);
      return;
    case SIG_MOTION_REST:
      if (st == SC_MS_RESTED && (e.a & SC_REST_POWEROFF)) {   // already parked: just switch off
        scMo.state = SC_MS_REST; scMo.sub = SC_SUB_POWEROFF; scMo.atMs = now;
        return;
      }
      if (st == SC_MS_REST && (e.a & SC_REST_POWEROFF)) { scMo.restFlags |= SC_REST_POWEROFF; return; }  // finish it, then off
      if (st == SC_MS_REST || st == SC_MS_RESTED) return;        // already on it
      // A switch-off beats a rail cycle or a wake: it ends with every rail off
      // anyway. A plain rest is dropped there; battery care asks again.
      if ((st == SC_MS_RECOVER || st == SC_MS_WAKE) && !(e.a & SC_REST_POWEROFF)) { scMotionDrop(SC_MK_REST); return; }
      scMotionStartRest(e.a, now);                                // beats a gesture
      return;
    case SIG_MOTION_WAKE:
      if (st != SC_MS_RESTED) return;
      scMo.state = SC_MS_WAKE;
      scMo.sub = SC_SUB_RAIL_OFF;
      scMo.atMs = now;
      return;
    case SIG_TICK:
      switch (st) {
        case SC_MS_STILL:   scMotionWatchdog(now); break;
        case SC_MS_GESTURE: if (scMotionDue(now)) scMotionGestureStep(now); break;
        case SC_MS_RECOVER: scMotionRecoverTick(now); break;
        case SC_MS_REST:    scMotionRestTick(now); break;
        case SC_MS_WAKE:    scMotionWakeTick(now); break;
        default: break;
      }
      return;
  }
}

inline void scMotionAOStart() {
  scMotionAO.name = "motion";
  scMotionAO.onEvent = scMotionOnEvent;
  scMotionAO.tickMs = 10;
  aoStart(scMotionAO);
}

// True while MotionAO is in a gesture, rail cycle, rest or wake: idle drift waits.
inline bool scMotionBusy() { return scMo.state != SC_MS_STILL; }

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_BASE_H
