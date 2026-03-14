#ifndef WLED_SCHEDULED_CONTENT_H
#define WLED_SCHEDULED_CONTENT_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// ============================================================================
// Scheduled Content — Periodic Weather + Emoji Cycles on WLED
// ============================================================================
// Every N minutes (default 30): 2-min weather display + 4-min emoji slideshow.
// Speech interrupts the cycle (pauses display, resumes after).
// Only one bot per WLED runs the scheduler (first-online claims ownership).
//
// Flow: IDLE -> WEATHER -> GAP -> EMOJI -> IDLE
// ============================================================================

#define SCHED_DEFAULT_INTERVAL_MS  1800000  // 30 minutes
#define SCHED_WEATHER_DURATION_MS  120000   // 2 minutes
#define SCHED_EMOJI_DURATION_MS    240000   // 4 minutes
#define SCHED_GAP_DURATION_MS      3000     // 3s gap between weather and emoji
#define SCHED_EMOJI_COUNT          20       // Random emojis per cycle

// Forward declarations — mesh functions defined in esp_now_mesh.h (included after us)
extern bool meshAnyPeerSchedOwnerForIP(uint32_t localIP);
extern void meshSetSchedOwner(bool owner);
extern bool meshIsSchedOwner();

// These are defined in headers included before us (wled_display.h, wled_weather_view.h, etc.)
// but we declare them here for clarity
extern uint32_t wledGetIPAsU32();

struct ScheduledContentState {
  bool     enabled;
  uint32_t cycleIntervalMs;
  unsigned long lastCycleStartMs;

  enum Phase : uint8_t {
    SCHED_IDLE = 0,
    SCHED_WEATHER,
    SCHED_GAP,
    SCHED_EMOJI
  } phase;

  unsigned long phaseStartMs;
  bool     isOwner;           // Are we the scheduler for our WLED?
  bool     speechInterrupted; // Speech paused the current phase
  uint8_t  randomEmojis[SCHED_EMOJI_COUNT];
};

static ScheduledContentState schedContent = {};

// ============================================================================
// NVS Persistence
// ============================================================================

void loadScheduleSettings() {
  Preferences prefs;
  prefs.begin("schedule", true);
  schedContent.enabled = prefs.getBool("enabled", false);
  schedContent.cycleIntervalMs = prefs.getUInt("intervalMs", SCHED_DEFAULT_INTERVAL_MS);
  prefs.end();

  schedContent.phase = ScheduledContentState::SCHED_IDLE;
  schedContent.lastCycleStartMs = millis();
  schedContent.isOwner = false;
  schedContent.speechInterrupted = false;
}

void saveScheduleSettings() {
  Preferences prefs;
  prefs.begin("schedule", false);
  prefs.putBool("enabled", schedContent.enabled);
  prefs.putUInt("intervalMs", schedContent.cycleIntervalMs);
  prefs.end();
}

// ============================================================================
// Emoji Randomization (Fisher-Yates shuffle, pick 20 from ICON_COUNT)
// ============================================================================

static void schedPickRandomEmojis() {
  uint8_t pool[ICON_COUNT];
  for (uint8_t i = 0; i < ICON_COUNT; i++) pool[i] = i;

  // Fisher-Yates partial shuffle for first SCHED_EMOJI_COUNT items
  for (uint8_t i = 0; i < SCHED_EMOJI_COUNT && i < ICON_COUNT; i++) {
    uint8_t j = i + random(ICON_COUNT - i);
    uint8_t tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
    schedContent.randomEmojis[i] = pool[i];
  }
}

// ============================================================================
// Speech Interruption
// ============================================================================

void schedOnSpeechStart() {
  if (schedContent.phase == ScheduledContentState::SCHED_IDLE) return;

  // Pause current display
  schedContent.speechInterrupted = true;
  if (schedContent.phase == ScheduledContentState::SCHED_EMOJI) {
    wledEmojiStop();
  }
  // Weather: just stop updating — speech takes over WLED
}

void schedOnSpeechEnd() {
  if (!schedContent.speechInterrupted) return;
  schedContent.speechInterrupted = false;

  // Resume: weather and emoji phases use wall-clock timers,
  // so the remaining time is automatically shorter.
  if (schedContent.phase == ScheduledContentState::SCHED_EMOJI) {
    // Re-start emoji display with remaining time
    wledEmojiStart();
  }
  // Weather: wledWeatherViewUpdate() will resume on next poll
}

// ============================================================================
// Main Poll — called from task_manager.h (Core 0)
// ============================================================================

void pollScheduledContent() {
  if (!schedContent.enabled) return;
  if (!wledStreamAllowed) return;
  if (schedContent.speechInterrupted) return;

  unsigned long now = millis();

  // Scheduler ownership: only one bot per WLED runs cycles
  uint32_t localIP = wledGetIPAsU32();
  if (localIP == 0) return;  // No WLED configured

  if (meshAnyPeerSchedOwnerForIP(localIP)) {
    // Another bot owns the schedule for our WLED
    if (schedContent.isOwner) {
      schedContent.isOwner = false;
      meshSetSchedOwner(false);
    }
    return;
  }

  // We are the scheduler (or standalone)
  if (!schedContent.isOwner) {
    schedContent.isOwner = true;
    meshSetSchedOwner(true);
    DBGLN("Sched: claimed scheduler ownership");
  }

  switch (schedContent.phase) {
    case ScheduledContentState::SCHED_IDLE:
      if ((now - schedContent.lastCycleStartMs) >= schedContent.cycleIntervalMs) {
        // Start new cycle: weather phase
        schedContent.phase = ScheduledContentState::SCHED_WEATHER;
        schedContent.phaseStartMs = now;
        schedContent.lastCycleStartMs = now;
        requestWeatherFetch();
        wledWeatherViewReset();
        DBGLN("Sched: starting weather phase");
      }
      break;

    case ScheduledContentState::SCHED_WEATHER:
      wledWeatherViewUpdate();
      if ((now - schedContent.phaseStartMs) >= SCHED_WEATHER_DURATION_MS) {
        wledWeatherViewOnExit();
        schedContent.phase = ScheduledContentState::SCHED_GAP;
        schedContent.phaseStartMs = now;
        DBGLN("Sched: weather done, gap");
      }
      break;

    case ScheduledContentState::SCHED_GAP:
      if ((now - schedContent.phaseStartMs) >= SCHED_GAP_DURATION_MS) {
        // Start emoji phase: pick random emojis and start slideshow
        schedPickRandomEmojis();
        wledEmojiClear();
        for (uint8_t i = 0; i < SCHED_EMOJI_COUNT; i++) {
          wledEmojiAdd(schedContent.randomEmojis[i]);
        }
        wledEmojiStart();
        schedContent.phase = ScheduledContentState::SCHED_EMOJI;
        schedContent.phaseStartMs = now;
        DBGLN("Sched: starting emoji phase");
      }
      break;

    case ScheduledContentState::SCHED_EMOJI:
      if ((now - schedContent.phaseStartMs) >= SCHED_EMOJI_DURATION_MS) {
        wledEmojiStop();
        schedContent.phase = ScheduledContentState::SCHED_IDLE;
        DBGLN("Sched: cycle complete");
      }
      break;
  }
}

#endif // WLED_SCHEDULED_CONTENT_H
