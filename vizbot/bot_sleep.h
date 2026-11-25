#pragma once
// ============================================================================
// Sleep — API (docs/plan-sleep.md)
// ============================================================================
// Declarations only, so the touch UI and web server (included before the LED
// and settings code the owner needs) can call it. The owner, which is the only
// code that puts the bot to sleep or wakes him, is bot_sleep_owner.h.
// Everything here except botSleepSnapshot() is loop-thread only; the web task
// goes through the command queue (cmdSleepConfig / cmdSleepRequest).
// ============================================================================

#include "sleep_schedule.h"

#ifdef HAS_SLEEP_SCHEDULE

struct BotSleepSnapshot {
  sleepsched::Config cfg;
  bool asleep;
  sleepsched::Cause cause;
  bool clockKnown;
};

// A change from the web: only the fields in mask are applied, onto the config
// as it is when the loop gets to it, so two quick edits cannot undo each other.
enum BotSleepField : uint8_t {
  SLEEP_F_ON = 1, SLEEP_F_AT = 2, SLEEP_F_WAKE = 4, SLEEP_F_NAP = 8,
};
struct BotSleepPatch {
  uint8_t mask;              // BotSleepField bits
  sleepsched::Config cfg;    // values for the fields in mask
};

// Loop thread
void botSleepBegin(const sleepsched::Config& c);
void botSleepTick();
void botSleepSetConfig(const sleepsched::Config& c);
void botSleepApplyPatch(const BotSleepPatch& p);
const sleepsched::Config& botSleepConfig();
bool botSleepAsleep();
void botSleepRequest(bool sleep);   // true = sleep now, false = wake (hold-to-wake)

// Any task
BotSleepSnapshot botSleepSnapshot();
void cmdSleepConfig(const BotSleepPatch& p);        // task_manager.h
void cmdSleepRequest(bool sleep);                   // task_manager.h
String botSleepJson();                              // web_server.h: GET /bot/sleep, /state

// Formatting (pure)
int16_t botSleepParseHHMM(const char* s);           // "HH:MM" -> minute-of-day, -1 if invalid
void botSleepFormatHHMM(uint16_t mod, char* out, size_t len);
const char* botSleepCauseName(sleepsched::Cause c);

#endif  // HAS_SLEEP_SCHEDULE
