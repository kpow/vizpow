#pragma once
// ============================================================================
// Sleep owner — vizbot's side of the sleep schedule (docs/plan-sleep.md)
// API and notes for callers: bot_sleep.h.
// ============================================================================
// sleep_schedule.h decides WHEN (pure logic, host-tested). This file is the
// only code that puts the bot to sleep or wakes him, and it runs on the loop
// thread only:
//   - botSleepTick() reads the clock (rate-limited), applies the transition
//     the schedule reports and reconciles the effects. The 1.69 calls it every
//     frame; on StackChan it is SleepAO's tick (end of this file).
//   - Web and cloud reach it through the command queue (task_manager.h); the
//     web task reads only the snapshot below.
// Effects while asleep: bot state SLEEPING (eyes shut, Zzz, no sayings or
// chirps), backlight dimmed, LEDs off, sound hushed, any UI sheet closed.
// StackChan also: head rested through MotionAO (servo rail and 5 V off), wake
// stream and Audio FX mic paused, chill ended. None of them touch a saved
// setting, so the morning puts everything back.
// ============================================================================

#include "bot_sleep.h"

#ifdef HAS_SLEEP_SCHEDULE

#include <time.h>

extern uint8_t lcdBrightness;

// ---- Loop-thread state --------------------------------------------------------
static sleepsched::Machine botSleep;
static int16_t  botSleepMod = sleepsched::kTimeUnknown;   // last minute-of-day read
static uint32_t botSleepClockAt = 0;                      // millis() of that read
static bool     botSleepClockRead = false;
static unsigned long botSleepSeenHuman = 0;               // botMode.lastHumanMs last passed on
static bool     botSleepBlPending = false;                // backlight change still to apply
#ifdef BOARD_HAS_STACKCHAN_BASE
static uint32_t botSleepRestAskAt = 0;                    // last rest/wake ask to MotionAO
static bool     botSleepRestAsked = false;
#endif

// ---- Snapshot for the web task (GET /bot/sleep, /state) -----------------------
static BotSleepSnapshot botSleepSnap = { sleepsched::kDefaults, false, sleepsched::Cause::None, false };
static portMUX_TYPE botSleepSnapMux = portMUX_INITIALIZER_UNLOCKED;

static void botSleepPublish() {
  BotSleepSnapshot s = { botSleep.config(), botSleep.asleep(), botSleep.cause(),
                         botSleepMod != sleepsched::kTimeUnknown };
  portENTER_CRITICAL(&botSleepSnapMux);
  botSleepSnap = s;
  portEXIT_CRITICAL(&botSleepSnapMux);
}

BotSleepSnapshot botSleepSnapshot() {
  portENTER_CRITICAL(&botSleepSnapMux);
  BotSleepSnapshot s = botSleepSnap;
  portEXIT_CRITICAL(&botSleepSnapMux);
  return s;
}

// Local minute-of-day, or kTimeUnknown before NTP has set the clock.
// Reads time() directly: getLocalTime(&t, 0) can report false on a set clock
// when millis() ticks over inside it (vizmini/ink_time.h found this).
static int16_t botSleepReadClock() {
  time_t now = time(nullptr);
  struct tm t;
  if (localtime_r(&now, &t) == nullptr || t.tm_year < (2024 - 1900)) return sleepsched::kTimeUnknown;
  return sleepsched::minuteOfDay(t.tm_hour, t.tm_min);
}

// ---- Effects -------------------------------------------------------------------
// The backlight follows lcdSleepDim (display_lcd.h). On CoreS3 it is a PMU
// write on the shared bus: lock or skip (rule 3), and the tick tries again.
static void botSleepApplyBacklight() {
  #ifdef BOARD_HAS_STACKCHAN_BASE
  if (!i2cTry(0)) { botSleepBlPending = true; return; }
  setLCDBacklight(lcdBrightness);
  i2cRelease();
  #else
  setLCDBacklight(lcdBrightness);
  #endif
  botSleepBlPending = false;
}

#ifdef BOARD_HAS_STACKCHAN_BASE
// Asleep, the head rests for SC_RR_SLEEP; awake, that reason is cleared.
// MotionAO owns the rest (rule 1) and may refuse mid-recovery, so this asks
// again every SLEEP_REST_ASK_MS until scRestReasons agrees. A transition asks
// at once.
static void botSleepReconcileRest(uint32_t now, bool changed) {
  const bool want = botSleep.asleep();
  const bool has = (scRestReasons & SC_RR_SLEEP) != 0;
  if (want == has) { botSleepRestAsked = false; return; }
  if (botSleepRestAsked && !changed && now - botSleepRestAskAt < SLEEP_REST_ASK_MS) return;
  aoPost(scMotionAO, aoEvt(want ? SIG_MOTION_REST : SIG_MOTION_WAKE, SRC_APP, 0, SC_RR_SLEEP));
  botSleepRestAsked = true;
  botSleepRestAskAt = now;
}
#endif

static void botSleepApply(sleepsched::Change c) {
  if (c == sleepsched::Change::FellAsleep) {
    if (infoMode.active) infoMode.beginExitTransition();
    #ifdef TOUCH_UI_V2
    uiSleepClose();
    #endif
    botMode.fallAsleep();          // plays the descend, so hush after it starts
    botSounds.hushed = true;
    #ifdef HAS_BASE_LEDS
    scLeds.asleep = true;
    #endif
    #ifdef BOARD_HAS_STACKCHAN_BASE
    scEndChillQuietly();
    aoPublish(SIG_SLEEP_START, SRC_APP, botSleep.cause() == sleepsched::Cause::Manual ? 2 : 1);
    #endif
    #ifdef VOICE_ENABLED
    voiceSleeping = true;          // the voice task stops the wake stream and FX mic
    #endif
    lcdSleepDim = true;
    botSleepApplyBacklight();
    DBGLN("Sleep: asleep");
  } else if (c == sleepsched::Change::WokeUp) {
    #ifdef BOARD_HAS_STACKCHAN_BASE
    aoPublish(SIG_SLEEP_END, SRC_APP);
    #endif
    #ifdef VOICE_ENABLED
    voiceSleeping = false;
    #endif
    lcdSleepDim = false;
    botSleepApplyBacklight();
    #ifdef HAS_BASE_LEDS
    scLeds.asleep = false;
    #endif
    botSounds.hushed = false;      // before wake(): it plays the chime
    botMode.wake();
    DBGLN("Sleep: awake");
  }
}

// ---- API (loop thread) --------------------------------------------------------
void botSleepBegin(const sleepsched::Config& c) {
  botSleep.begin(c, millis());
  botSleepPublish();
}

void botSleepSetConfig(const sleepsched::Config& c) {
  botSleep.setConfig(c);
  markSettingsDirty();
  botSleepPublish();
}

void botSleepApplyPatch(const BotSleepPatch& p) {
  sleepsched::Config c = botSleep.config();
  if (p.mask & SLEEP_F_ON)   c.enabled  = p.cfg.enabled;
  if (p.mask & SLEEP_F_AT)   c.sleepMin = p.cfg.sleepMin;
  if (p.mask & SLEEP_F_WAKE) c.wakeMin  = p.cfg.wakeMin;
  if (p.mask & SLEEP_F_NAP)  c.napMin   = p.cfg.napMin;
  botSleepSetConfig(c);
}

const sleepsched::Config& botSleepConfig() { return botSleep.config(); }
bool botSleepAsleep() { return botSleep.asleep(); }

void botSleepRequest(bool sleep) {
  if (sleep) botSleep.sleepNow(); else botSleep.wakeNow();
}

void botSleepTick() {
  if (!botMode.initialized) return;   // enterBotMode() has not run yet
  const uint32_t now = millis();
  const bool reread = !botSleepClockRead || now - botSleepClockAt >= SLEEP_CLOCK_POLL_MS;
  if (reread) {
    botSleepMod = botSleepReadClock();
    botSleepClockAt = now;
    botSleepClockRead = true;
  }
  // Activity is an event for the schedule: pass a new touch on once, never the
  // old stamp again (millis() comparisons go wrong on stamps weeks old).
  if (botMode.lastHumanMs != botSleepSeenHuman) {
    botSleepSeenHuman = botMode.lastHumanMs;
    botSleep.noteActivity(botSleepSeenHuman);
  }
  const sleepsched::Change c = botSleep.update(botSleepMod, now);
  botSleepApply(c);
  if (botSleepBlPending) botSleepApplyBacklight();
  #ifdef BOARD_HAS_STACKCHAN_BASE
  botSleepReconcileRest(now, c != sleepsched::Change::None);
  #endif
  if (reread || c != sleepsched::Change::None) botSleepPublish();
}

// ---- Formatting helpers (web + touch UI) ---------------------------------------
// "HH:MM" -> minute-of-day, or -1 when it is not a valid time.
int16_t botSleepParseHHMM(const char* s) {
  if (s == nullptr) return -1;
  int h = -1, m = -1;
  char tail = 0;
  if (sscanf(s, "%d:%d%c", &h, &m, &tail) != 2) return -1;
  return sleepsched::minuteOfDay(h, m);
}

void botSleepFormatHHMM(uint16_t mod, char* out, size_t len) {
  snprintf(out, len, "%02u:%02u", (unsigned)(mod / 60) % 24, (unsigned)(mod % 60));
}

const char* botSleepCauseName(sleepsched::Cause c) {
  switch (c) {
    case sleepsched::Cause::Schedule: return "schedule";
    case sleepsched::Cause::Manual:   return "manual";
    default:                          return "none";
  }
}

#ifdef BOARD_HAS_STACKCHAN_BASE
// ---- SleepAO (StackChan) ---------------------------------------------------------
// Owns the schedule on StackChan (firmware rules: a subsystem is an owner with
// its own events). Ticks the schedule; SIG_SLEEP_WAKE is the head hold, posted
// by the app AO.
static AoActive scSleepAO;

static void scSleepOnEvent(const AoEvt& e) {
  switch (e.sig) {
    case SIG_TICK:
      botSleepTick();
      break;
    case SIG_SLEEP_WAKE:
      botSleepRequest(false);
      botSleepTick();              // at once, not on the next tick
      break;
    default: break;
  }
}

inline void scSleepAOStart() {
  scSleepAO.name = "sleep";
  scSleepAO.onEvent = scSleepOnEvent;
  scSleepAO.tickMs = SLEEP_AO_TICK_MS;
  aoStart(scSleepAO);
}
#endif

#endif  // HAS_SLEEP_SCHEDULE
