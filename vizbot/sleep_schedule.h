#pragma once
// ============================================================================
// Sleep schedule — when a bot sleeps and when it wakes (docs/plan-sleep.md)
// ============================================================================
// Pure C++: no Arduino, no clock reads, no hardware. The firmware feeds it the
// local minute-of-day (or kTimeUnknown) and millis(), applies the effects of
// the Change it returns, and owns persistence. That keeps every rule here
// testable on the Mac: tools/sleep-test/run.sh.
//
// ONE FILE, TWO COPIES: vizbot/sleep_schedule.h and vizmini/sleep_schedule.h
// must stay byte-identical (run.sh checks). Edit one, copy it to the other.
//
// Rules
//   - Asleep from sleepMin up to (not including) wakeMin, crossing midnight if
//     sleepMin > wakeMin. sleepMin == wakeMin is no window at all.
//   - Unknown time never starts a scheduled sleep and fires no window edges.
//   - wakeNow() inside the window holds the bot awake until napMin minutes
//     pass without noteActivity(); napMin 0 holds it until the wake time.
//   - When the window opens, recent activity (inside the nap span) defers
//     bedtime the same way, so a bot being played with is not put down mid-game.
//   - sleepNow() lasts until wakeNow() or the next wake time (window close).
//   - The window closing clears every override: morning is a clean slate.
//   - setConfig() is not a morning: no clean slate. If it puts "now" inside the
//     window it is treated like a bedtime (deferred while played with), so a
//     setting changed on the bot itself does not put him down mid-edit; from
//     afar (no recent activity) he sleeps at once. Disabling wakes him.
//
// millis() wraps every 49.7 days and a signed difference flips after 24.8, so
// no stamp is ever compared with "now" after its nap span has passed: the
// span is latched off (napLive_) on the first update() past it.
//
// Commands (sleepNow / wakeNow / setConfig) take effect at the next update(),
// which is the only place a transition is reported. Call update() every tick.
// ============================================================================

#include <stdint.h>

namespace sleepsched {

constexpr uint16_t kMinutesPerDay = 1440;
constexpr int16_t  kTimeUnknown   = -1;
constexpr uint8_t  kNapMaxMin     = 120;

struct Config {
  bool     enabled;    // the schedule; sleepNow() works either way
  uint16_t sleepMin;   // minute-of-day the window opens, 0..1439
  uint16_t wakeMin;    // minute-of-day the window closes, 0..1439
  uint8_t  napMin;     // quiet minutes before a hand-woken bot nods off; 0 = never
};

// Kevin's defaults (2026-11-24): on, 23:00 -> 08:00, nod off after 10 minutes.
constexpr Config kDefaults = { true, 23 * 60, 8 * 60, 10 };

// Clamp anything out of range (corrupt NVS, bad web input) to the default.
inline Config sanitize(Config c) {
  if (c.sleepMin >= kMinutesPerDay) c.sleepMin = kDefaults.sleepMin;
  if (c.wakeMin  >= kMinutesPerDay) c.wakeMin  = kDefaults.wakeMin;
  if (c.napMin   >  kNapMaxMin)     c.napMin   = kDefaults.napMin;
  return c;
}

// Minute-of-day from hours and minutes; kTimeUnknown for anything out of range.
inline int16_t minuteOfDay(int hour, int minute) {
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return kTimeUnknown;
  return static_cast<int16_t>(hour * 60 + minute);
}

// One step of a +/- control on a step-minute grid, wrapping round midnight.
// Off the grid (say 23:15 on a 30-minute grid), -1 lands on the grid line below
// (23:00) and +1 on the one above (23:30). step must divide a day; 0 = 1.
inline uint16_t stepMinute(uint16_t mod, int dir, uint16_t step) {
  if (step == 0) step = 1;
  const int slots = kMinutesPerDay / step;
  int g = (mod % kMinutesPerDay) / step;
  if (dir < 0 && (mod % step) != 0) g += 1;
  g = ((g + (dir < 0 ? -1 : 1)) % slots + slots) % slots;
  return static_cast<uint16_t>(g * step);
}

// Is minuteOfDay inside the sleep window? False when disabled, empty or unknown.
inline bool inWindow(const Config& c, int16_t mod) {
  if (!c.enabled || mod < 0 || mod >= static_cast<int16_t>(kMinutesPerDay)) return false;
  if (c.sleepMin == c.wakeMin) return false;
  const uint16_t m = static_cast<uint16_t>(mod);
  if (c.sleepMin < c.wakeMin) return m >= c.sleepMin && m < c.wakeMin;
  return m >= c.sleepMin || m < c.wakeMin;   // crosses midnight
}

enum class Cause  : uint8_t { None, Schedule, Manual };
enum class Change : uint8_t { None, FellAsleep, WokeUp };

class Machine {
 public:
  void begin(const Config& c, uint32_t nowMs) {
    cfg_ = sanitize(c);
    primed_ = prevWin_ = cfgChanged_ = false;
    manual_ = heldAwake_ = asleep_ = false;
    sleepReq_ = wakeReq_ = false;
    noteActivity(nowMs);           // booting counts: no bedtime in the first nap span
  }

  // Applied by the next update(): no edges from the change itself (see Rules).
  void setConfig(const Config& c) {
    cfg_ = sanitize(c);
    primed_ = false;
    cfgChanged_ = true;
  }
  const Config& config() const { return cfg_; }

  // Commands: queued, applied by the next update().
  void sleepNow() { sleepReq_ = true; wakeReq_ = false; }
  void wakeNow()  { wakeReq_ = true;  sleepReq_ = false; }

  // A person interacted, at atMs (normally now). An event, not a level: call it
  // when an interaction happens, never with an old "last touched" stamp.
  void noteActivity(uint32_t atMs) {
    lastActivityMs_ = atMs;
    napLive_ = true;
  }

  Change update(int16_t mod, uint32_t nowMs) {
    const bool known = mod >= 0 && mod < static_cast<int16_t>(kMinutesPerDay);
    const bool win = inWindow(cfg_, mod);
    latchNap(nowMs);

    if (wakeReq_) {            // hold-to-wake, web "wake"
      wakeReq_ = false;
      manual_ = false;
      heldAwake_ = true;
      noteActivity(nowMs);
    }
    if (sleepReq_) {           // "sleep now"
      sleepReq_ = false;
      manual_ = true;
      heldAwake_ = false;
    }

    if (known) {
      if (primed_ && prevWin_ && !win) {          // wake time: clean slate
        manual_ = false;
        heldAwake_ = false;
      } else if (primed_ && !prevWin_ && win) {   // bedtime: defer while played with
        heldAwake_ = napLive_;
      } else if (!primed_ && cfgChanged_ && win && !asleep_ && !manual_) {
        heldAwake_ = heldAwake_ || napLive_;      // a new config: like a bedtime
      }
      if (!primed_) cfgChanged_ = false;
      prevWin_ = win;
      primed_ = true;
    }

    if (heldAwake_ && win && cfg_.napMin != 0 && !napLive_) heldAwake_ = false;
    if (!win && !manual_) heldAwake_ = false;   // outside the window there is nothing to hold off

    const bool want = manual_ || (win && !heldAwake_);
    if (want == asleep_) return Change::None;
    asleep_ = want;
    return want ? Change::FellAsleep : Change::WokeUp;
  }

  bool asleep() const { return asleep_; }
  Cause cause() const {
    if (!asleep_) return Cause::None;
    return manual_ ? Cause::Manual : Cause::Schedule;
  }

 private:
  // napLive_: the last activity is younger than the nap span. Latched off the
  // first update() past the span, while the unsigned difference is still small,
  // so it stays right however long the bot then runs.
  void latchNap(uint32_t nowMs) {
    if (!napLive_) return;
    if (cfg_.napMin == 0 ||
        (nowMs - lastActivityMs_) >= static_cast<uint32_t>(cfg_.napMin) * 60000UL) napLive_ = false;
  }

  Config   cfg_ = kDefaults;
  bool     primed_ = false;      // seen a known time since begin()
  bool     cfgChanged_ = false;  // setConfig() since the last known time
  bool     napLive_ = false;     // inside the nap span of the last activity
  bool     prevWin_ = false;     // window state at the last known time
  bool     manual_ = false;      // sleepNow() in force
  bool     heldAwake_ = false;   // woken (or bedtime deferred) inside the window
  bool     asleep_ = false;
  bool     sleepReq_ = false;
  bool     wakeReq_ = false;
  uint32_t lastActivityMs_ = 0;
};

}  // namespace sleepsched
