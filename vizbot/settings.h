#ifndef SETTINGS_H
#define SETTINGS_H

#include <Preferences.h>
#include "config.h"

// ============================================================================
// Persistent Settings — ESP32 NVS (Non-Volatile Storage)
// ============================================================================
// Saves user preferences to flash so they survive power cycles.
// Uses the ESP32 Preferences library (wear-leveled NVS partition).
//
// Write strategy: settings are saved lazily — only when a value actually
// changes. NVS has ~100k write cycles per key, so avoid saving every frame.
// A dirty flag + periodic flush keeps writes minimal.
// ============================================================================

static Preferences prefs;

// Dirty flag — set when any setting changes, cleared after save
static bool settingsDirty = false;
static unsigned long lastSettingsSaveMs = 0;

// Minimum interval between NVS writes (debounce rapid changes like sliders)
#define SETTINGS_SAVE_INTERVAL_MS 3000

// Namespace for NVS (max 15 chars)
#define NVS_NAMESPACE "vizbot"

// External globals that we persist
extern uint8_t brightness;
extern bool autoCycle;
extern uint8_t botBackgroundStyle;
extern uint16_t botFaceColor;

// Forward declarations for bot mode setters
extern void setBotBackgroundStyle(uint8_t style);
extern void setBotFaceColor(uint16_t color);
extern void setBotPersonality(uint8_t index);
extern uint8_t getBotPersonality();
extern uint8_t getBotBackgroundStyle();
extern bool isBotTimeOverlayEnabled();
extern void toggleBotTimeOverlay();

// ============================================================================
// Load Settings — call during boot, AFTER bot mode is initialized
// ============================================================================

void loadSettings() {
  prefs.begin(NVS_NAMESPACE, true);  // read-only mode

  // Load with sensible defaults (first boot will use these)
  brightness = prefs.getUChar("brightness", DEFAULT_BRIGHTNESS);
  autoCycle = prefs.getBool("autoCycle", true);

  uint8_t bgStyle = prefs.getUChar("bgStyle", 4);  // default: ambient
  uint16_t faceColor = prefs.getUShort("faceColor", 0xFFFF);  // default: white
  uint8_t personality = prefs.getUChar("personality", 0);  // default: chill
  bool timeOverlay = prefs.getBool("timeOverlay", false);

  prefs.end();

  // Apply loaded values
  FastLED.setBrightness(brightness);
  setBotBackgroundStyle(bgStyle);
  setBotFaceColor(faceColor);
  setBotPersonality(personality);

  // Set time overlay to loaded state
  if (timeOverlay != isBotTimeOverlayEnabled()) {
    toggleBotTimeOverlay();
  }

  DBG("Settings loaded: bright=");
  DBG(brightness);
  DBG(" bg=");
  DBG(bgStyle);
  DBG(" color=0x");
  DBG(faceColor, HEX);
  DBG(" pers=");
  DBG(personality);
  DBG(" time=");
  DBGLN(timeOverlay);
}

// ============================================================================
// Save Settings — call periodically when dirty flag is set
// ============================================================================
// Only writes keys that have actually changed to minimize NVS wear.

void saveSettings() {
  prefs.begin(NVS_NAMESPACE, false);  // read-write mode

  // Only write values that differ from stored values (reduces wear)
  if (prefs.getUChar("brightness", 0) != brightness)
    prefs.putUChar("brightness", brightness);

  if (prefs.getBool("autoCycle", true) != autoCycle)
    prefs.putBool("autoCycle", autoCycle);

  uint8_t bgStyle = getBotBackgroundStyle();
  if (prefs.getUChar("bgStyle", 0) != bgStyle)
    prefs.putUChar("bgStyle", bgStyle);

  extern uint16_t botFaceColor;
  if (prefs.getUShort("faceColor", 0) != botFaceColor)
    prefs.putUShort("faceColor", botFaceColor);

  uint8_t personality = getBotPersonality();
  if (prefs.getUChar("personality", 0) != personality)
    prefs.putUChar("personality", personality);

  bool timeOverlay = isBotTimeOverlayEnabled();
  if (prefs.getBool("timeOverlay", false) != timeOverlay)
    prefs.putBool("timeOverlay", timeOverlay);

  prefs.end();

  settingsDirty = false;
  lastSettingsSaveMs = millis();
  DBGLN("Settings saved to NVS");
}

// ============================================================================
// Mark Dirty — call whenever a persisted setting changes
// ============================================================================

void markSettingsDirty() {
  settingsDirty = true;
}

// ============================================================================
// Flush if Dirty — call from main loop, respects debounce interval
// ============================================================================

void flushSettingsIfDirty() {
  if (!settingsDirty) return;
  if (millis() - lastSettingsSaveMs < SETTINGS_SAVE_INTERVAL_MS) return;
  saveSettings();
}

#endif // SETTINGS_H
