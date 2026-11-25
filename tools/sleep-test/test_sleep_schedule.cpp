// Host tests for sleep_schedule.h. Run: tools/sleep-test/run.sh
#include "../../vizbot/sleep_schedule.h"

#include <cstdio>
#include <cstdlib>

using namespace sleepsched;

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (cond) { ++g_pass; }                                                \
    else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static constexpr uint32_t kMin = 60000UL;
static int16_t hm(int h, int m) { return minuteOfDay(h, m); }
static uint16_t uhm(int h, int m) { return static_cast<uint16_t>(minuteOfDay(h, m)); }

// A clock that walks minute by minute, so tests read like a night.
struct Sim {
  Machine m;
  uint32_t ms = 1000;
  int16_t mod = 0;
  explicit Sim(Config c = kDefaults, int16_t start = hm(12, 0)) : mod(start) {
    m.begin(c, ms);
  }
  Change tick() { return m.update(mod, ms); }
  // Advance n minutes, ticking every minute; returns the last non-None change.
  Change advance(int n) {
    Change last = Change::None;
    for (int i = 0; i < n; ++i) {
      ms += kMin;
      mod = static_cast<int16_t>((mod + 1) % kMinutesPerDay);
      Change c = tick();
      if (c != Change::None) last = c;
    }
    return last;
  }
  void to(int h, int m2) {   // walk forward to h:m2
    const int16_t target = hm(h, m2);
    int n = (target - mod + kMinutesPerDay) % kMinutesPerDay;
    advance(n);
  }
};

static void test_minute_of_day() {
  CHECK(minuteOfDay(0, 0) == 0);
  CHECK(minuteOfDay(23, 59) == 1439);
  CHECK(minuteOfDay(24, 0) == kTimeUnknown);
  CHECK(minuteOfDay(-1, 0) == kTimeUnknown);
  CHECK(minuteOfDay(12, 60) == kTimeUnknown);
}

static void test_step_minute() {
  CHECK(stepMinute(uhm(23, 0), +1, 30) == uhm(23, 30));
  CHECK(stepMinute(uhm(23, 0), -1, 30) == uhm(22, 30));
  CHECK(stepMinute(uhm(23, 30), +1, 30) == uhm(0, 0));     // wraps forward
  CHECK(stepMinute(uhm(0, 0), -1, 30) == uhm(23, 30));     // wraps back
  CHECK(stepMinute(uhm(23, 15), -1, 30) == uhm(23, 0));    // off grid: down to the line
  CHECK(stepMinute(uhm(23, 15), +1, 30) == uhm(23, 30));   // off grid: up to the line
  CHECK(stepMinute(uhm(23, 45), +1, 30) == uhm(0, 0));
  CHECK(stepMinute(uhm(0, 10), -1, 30) == uhm(0, 0));
  CHECK(stepMinute(uhm(7, 0), +1, 0) == uhm(7, 1));        // step 0 treated as 1
  CHECK(stepMinute(5000, +1, 30) < kMinutesPerDay);      // garbage in, a time out
}

static void test_window_maths() {
  const Config night = kDefaults;                 // 23:00 -> 08:00
  CHECK(inWindow(night, hm(23, 0)));              // opens on the minute
  CHECK(inWindow(night, hm(0, 0)));
  CHECK(inWindow(night, hm(7, 59)));
  CHECK(!inWindow(night, hm(8, 0)));              // closes on the minute
  CHECK(!inWindow(night, hm(22, 59)));
  CHECK(!inWindow(night, hm(12, 0)));

  const Config day = { true, 13 * 60, 14 * 60, 10 };   // same-day window
  CHECK(inWindow(day, hm(13, 30)));
  CHECK(!inWindow(day, hm(14, 0)));
  CHECK(!inWindow(day, hm(12, 59)));

  const Config empty = { true, 600, 600, 10 };
  CHECK(!inWindow(empty, hm(10, 0)));

  Config off = kDefaults; off.enabled = false;
  CHECK(!inWindow(off, hm(2, 0)));

  CHECK(!inWindow(night, kTimeUnknown));
  CHECK(!inWindow(night, 1440));
}

static void test_sanitize() {
  Config bad = { true, 5000, 9999, 250 };
  Config s = sanitize(bad);
  CHECK(s.sleepMin == kDefaults.sleepMin);
  CHECK(s.wakeMin == kDefaults.wakeMin);
  CHECK(s.napMin == kDefaults.napMin);
  Config ok = { false, 0, 1439, 0 };
  Config s2 = sanitize(ok);
  CHECK(!s2.enabled && s2.sleepMin == 0 && s2.wakeMin == 1439 && s2.napMin == 0);
}

static void test_plain_night() {
  Sim s;
  CHECK(s.tick() == Change::None && !s.m.asleep());
  s.to(22, 59);
  CHECK(!s.m.asleep());
  CHECK(s.advance(1) == Change::FellAsleep);       // 23:00
  CHECK(s.m.asleep() && s.m.cause() == Cause::Schedule);
  s.to(7, 59);
  CHECK(s.m.asleep());
  CHECK(s.advance(1) == Change::WokeUp);           // 08:00
  CHECK(!s.m.asleep() && s.m.cause() == Cause::None);
}

static void test_boot_inside_window_sleeps() {
  Sim s(kDefaults, hm(2, 0));
  CHECK(s.tick() == Change::FellAsleep);
}

static void test_unknown_time_never_sleeps() {
  Sim s(kDefaults, kTimeUnknown);
  for (int i = 0; i < 100; ++i) { s.ms += kMin; CHECK(s.tick() == Change::None); }
  CHECK(!s.m.asleep());
  s.mod = hm(1, 0);                                // NTP lands mid-window
  CHECK(s.tick() == Change::FellAsleep);
}

static void test_hand_wake_then_nod_off() {
  Sim s(kDefaults, hm(1, 0));
  s.tick();
  CHECK(s.m.asleep());
  s.m.wakeNow();
  CHECK(s.tick() == Change::WokeUp);
  CHECK(s.advance(9) == Change::None && !s.m.asleep());   // 9 quiet minutes
  CHECK(s.advance(1) == Change::FellAsleep);              // 10th
}

static void test_hand_wake_after_long_uptime() {
  // Review finding: 25 days up with no touch, then a hand wake at 02:00. The
  // nap must still run its full 10 minutes (no stale stamp compared).
  Machine m;
  uint32_t ms = 1000;
  m.begin(kDefaults, ms);
  m.update(hm(12, 0), ms);
  ms += 2200000000UL;                              // ~25.5 days later
  CHECK(m.update(hm(2, 0), ms) == Change::FellAsleep);
  m.wakeNow();
  CHECK(m.update(hm(2, 0), ms) == Change::WokeUp);
  for (int i = 1; i < 10; ++i) CHECK(m.update(hm(2, i), ms + static_cast<uint32_t>(i) * kMin) == Change::None);
  CHECK(m.update(hm(2, 10), ms + 10 * kMin) == Change::FellAsleep);
}

static void test_old_activity_never_defers_bedtime() {
  // Activity 49.7 days ago lands "inside the nap span" by unsigned wrap; the
  // latch must have closed it long before.
  Machine m;
  uint32_t ms = 1000;
  m.begin(kDefaults, ms);
  m.update(hm(12, 0), ms);
  m.update(hm(12, 30), ms + 30 * kMin);             // latch closes here
  ms += 0xFFFFFFFFUL - 2 * kMin;                     // wraps to just before the stamp
  m.update(hm(22, 59), ms);
  CHECK(m.update(hm(23, 0), ms + kMin) == Change::FellAsleep);
}

static void test_wrap_window_peek_math() {
  // Bedtime deferral measured across the 2^31 boundary (a signed compare flips there).
  Machine m;
  uint32_t ms = 0x7FFFFFFFUL - 3 * kMin;
  m.begin(kDefaults, ms);                           // boot counts as activity
  CHECK(m.update(hm(22, 55), ms) == Change::None);
  CHECK(m.update(hm(23, 0), ms + 5 * kMin) == Change::None);       // deferred
  CHECK(m.update(hm(23, 4), ms + 9 * kMin) == Change::None);
  CHECK(m.update(hm(23, 5), ms + 10 * kMin) == Change::FellAsleep);
}

static void test_activity_extends_nap() {
  Sim s(kDefaults, hm(1, 0));
  s.tick();
  s.m.wakeNow();
  s.tick();
  for (int i = 0; i < 30; ++i) {                   // played with every 5 min
    s.advance(5);
    CHECK(!s.m.asleep());
    s.m.noteActivity(s.ms);
  }
  CHECK(s.advance(10) == Change::FellAsleep);
}

static void test_nap_zero_stays_up_till_morning() {
  Config c = kDefaults; c.napMin = 0;
  Sim s(c, hm(1, 0));
  s.tick();
  s.m.wakeNow();
  CHECK(s.tick() == Change::WokeUp);
  s.to(7, 59);
  CHECK(!s.m.asleep());
  s.advance(1);
  CHECK(!s.m.asleep());
  s.to(23, 0);                                     // next night: sleeps again
  CHECK(s.m.asleep());
}

static void test_bedtime_waits_for_play() {
  Sim s;
  s.to(22, 55);
  s.m.noteActivity(s.ms);                          // played with at 22:55
  CHECK(s.advance(5) == Change::None);             // 23:00 — deferred
  CHECK(!s.m.asleep());
  CHECK(s.advance(5) == Change::FellAsleep);       // 23:05 — 10 min after play
}

static void test_bedtime_on_time_without_play() {
  Sim s;
  s.to(22, 40);
  s.m.noteActivity(s.ms);                          // 20 min before bedtime
  CHECK(s.advance(20) == Change::FellAsleep);
}

static void test_sleep_now_outside_window() {
  Sim s(kDefaults, hm(15, 0));
  s.tick();
  s.m.sleepNow();
  CHECK(s.tick() == Change::FellAsleep);
  CHECK(s.m.cause() == Cause::Manual);
  s.to(23, 30);
  CHECK(s.m.asleep());                             // through bedtime
  s.to(7, 59);
  CHECK(s.m.asleep());
  CHECK(s.advance(1) == Change::WokeUp);           // morning clears it
}

static void test_sleep_now_then_hold_wake() {
  Sim s(kDefaults, hm(15, 0));
  s.tick();
  s.m.sleepNow();
  s.tick();
  s.m.wakeNow();
  CHECK(s.tick() == Change::WokeUp);
  CHECK(s.advance(60) == Change::None);            // outside window: no nap timer
}

static void test_sleep_now_without_clock() {
  Sim s(kDefaults, kTimeUnknown);
  s.tick();
  s.m.sleepNow();
  CHECK(s.tick() == Change::FellAsleep);
  s.ms += 24UL * 60 * kMin;
  CHECK(s.tick() == Change::None && s.m.asleep()); // pendant: until held awake
  s.m.wakeNow();
  CHECK(s.tick() == Change::WokeUp);
}

static void test_sleep_now_inside_window_after_wake() {
  Config c = kDefaults; c.napMin = 0;
  Sim s(c, hm(1, 0));
  s.tick();
  s.m.wakeNow();
  s.tick();
  s.m.sleepNow();
  CHECK(s.tick() == Change::FellAsleep);
  s.to(8, 0);
  CHECK(!s.m.asleep());
}

static void test_wake_now_while_awake_is_harmless() {
  Sim s;
  s.tick();
  s.m.wakeNow();
  CHECK(s.tick() == Change::None);
  s.to(23, 0);                                     // afternoon hold does not leak
  CHECK(s.m.asleep());
}

static void test_last_command_wins() {
  Sim s(kDefaults, hm(15, 0));
  s.tick();
  s.m.sleepNow();
  s.m.wakeNow();
  CHECK(s.tick() == Change::None && !s.m.asleep());
  s.m.wakeNow();
  s.m.sleepNow();
  CHECK(s.tick() == Change::FellAsleep);
}

static void test_disable_mid_window_wakes() {
  Sim s(kDefaults, hm(1, 0));
  s.tick();
  s.advance(15);                                   // past boot's nap span
  Config off = kDefaults; off.enabled = false;
  s.m.setConfig(off);
  CHECK(s.tick() == Change::WokeUp);
  s.m.setConfig(kDefaults);                        // re-enable: back to sleep
  CHECK(s.tick() == Change::FellAsleep);
}

static void test_config_change_defers_while_played_with() {
  // Moving bedtime earlier past "now" on the bot itself (someone is using it):
  // like a bedtime, he waits for 10 quiet minutes instead of sleeping mid-edit.
  Sim s(kDefaults, hm(22, 0));
  s.tick();
  s.advance(30);
  s.m.noteActivity(s.ms);
  Config early = kDefaults; early.sleepMin = 22 * 60;
  s.m.setConfig(early);
  CHECK(s.tick() == Change::None);
  CHECK(s.advance(10) == Change::FellAsleep);
}

static void test_config_change_from_afar_sleeps_now() {
  Sim s(kDefaults, hm(22, 0));
  s.tick();
  s.advance(30);                                   // nobody near for 30 min
  Config early = kDefaults; early.sleepMin = 22 * 60;
  s.m.setConfig(early);
  CHECK(s.tick() == Change::FellAsleep);
}

static void test_config_change_keeps_morning_edge() {
  // A config change mid-night is no morning: a manual sleep survives it.
  Sim s(kDefaults, hm(15, 0));
  s.tick();
  s.m.sleepNow();
  s.tick();
  Config c = kDefaults; c.napMin = 20;
  s.m.setConfig(c);
  CHECK(s.tick() == Change::None && s.m.asleep());
}

static void test_disable_keeps_manual_sleep() {
  Config off = kDefaults; off.enabled = false;
  Sim s(off, hm(15, 0));
  s.tick();
  s.m.sleepNow();
  CHECK(s.tick() == Change::FellAsleep);
  s.advance(24 * 60);                              // no window to close it
  CHECK(s.m.asleep());
}

static void test_dst_spring_forward() {
  // 02:00 -> 03:00 jump inside a 01:00 -> 02:30 window: the clock leaves it.
  Config c = { true, 60, 150, 10 };
  Sim s(c, hm(1, 59));
  CHECK(s.tick() == Change::FellAsleep);
  s.ms += kMin;
  s.mod = hm(3, 0);
  CHECK(s.tick() == Change::WokeUp);
}

static void test_dst_fall_back() {
  // 08:00 wake, then the clock repeats 01:00-02:00 earlier that night: still asleep.
  Sim s(kDefaults, hm(1, 59));
  s.tick();
  s.ms += kMin;
  s.mod = hm(1, 0);
  CHECK(s.tick() == Change::None && s.m.asleep());
}

static void test_clock_jump_across_window() {
  // NTP correction jumps from 22:00 straight to 09:00: never in the window, no change.
  Sim s(kDefaults, hm(22, 0));
  s.tick();
  s.ms += kMin;
  s.mod = hm(9, 0);
  CHECK(s.tick() == Change::None && !s.m.asleep());
}

static void test_millis_wrap() {
  Machine m;
  uint32_t ms = 0xFFFFFFFFUL - 2 * kMin;
  m.begin(kDefaults, ms);
  CHECK(m.update(hm(1, 0), ms) == Change::FellAsleep);
  m.wakeNow();
  CHECK(m.update(hm(1, 0), ms) == Change::WokeUp);
  ms += 9 * kMin;                                  // wraps past 0
  CHECK(m.update(hm(1, 9), ms) == Change::None);
  ms += kMin;
  CHECK(m.update(hm(1, 10), ms) == Change::FellAsleep);
}

static void test_change_reported_once() {
  Sim s(kDefaults, hm(1, 0));
  CHECK(s.tick() == Change::FellAsleep);
  for (int i = 0; i < 50; ++i) CHECK(s.tick() == Change::None);
}

int main() {
  test_minute_of_day();
  test_step_minute();
  test_window_maths();
  test_sanitize();
  test_plain_night();
  test_boot_inside_window_sleeps();
  test_unknown_time_never_sleeps();
  test_hand_wake_then_nod_off();
  test_hand_wake_after_long_uptime();
  test_old_activity_never_defers_bedtime();
  test_wrap_window_peek_math();
  test_activity_extends_nap();
  test_nap_zero_stays_up_till_morning();
  test_bedtime_waits_for_play();
  test_bedtime_on_time_without_play();
  test_sleep_now_outside_window();
  test_sleep_now_then_hold_wake();
  test_sleep_now_without_clock();
  test_sleep_now_inside_window_after_wake();
  test_wake_now_while_awake_is_harmless();
  test_last_command_wins();
  test_disable_mid_window_wakes();
  test_config_change_defers_while_played_with();
  test_config_change_from_afar_sleeps_now();
  test_config_change_keeps_morning_edge();
  test_disable_keeps_manual_sleep();
  test_dst_spring_forward();
  test_dst_fall_back();
  test_clock_jump_across_window();
  test_millis_wrap();
  test_change_reported_once();
  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
