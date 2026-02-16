#pragma once
// ============================================================================
// settings.h — Persistent settings via ESP32 Preferences (NVS)
// ============================================================================
// Saves user-facing state (brightness, palette, etc.) to flash so they
// survive power cycles.  Writes are debounced — call markSettingsDirty()
// whenever a value changes, then call flushSettingsIfDirty() from the main
// loop.  This avoids hammering NVS on rapid slider moves.

#include <Preferences.h>

static Preferences settingsPrefs;
static bool        settingsDirty    = false;
static unsigned long settingsDirtyAt = 0;
static const unsigned long SETTINGS_DEBOUNCE_MS = 2000;  // wait 2s after last change

// Forward declarations — these are the globals we persist
extern uint8_t brightness;
extern uint8_t effectIndex;
extern uint8_t paletteIndex;
extern bool    autoCycle;
extern uint8_t botBackgroundStyle;

// LCD backlight brightness (defined in touch_control.h)
extern uint8_t lcdBrightness;

// ── Load ────────────────────────────────────────────────────────────────────
void loadSettings() {
  settingsPrefs.begin("vizbot", true);  // read-only

  brightness         = settingsPrefs.getUChar("bright",  brightness);
  lcdBrightness      = settingsPrefs.getUChar("lcdBr",   lcdBrightness);
  effectIndex        = settingsPrefs.getUChar("effect",   effectIndex);
  paletteIndex       = settingsPrefs.getUChar("palette",  paletteIndex);
  autoCycle          = settingsPrefs.getBool ("autoCyc",  autoCycle);
  botBackgroundStyle = settingsPrefs.getUChar("bgStyle",  botBackgroundStyle);

  settingsPrefs.end();

  Serial.println("Settings loaded from NVS");
  Serial.printf("  brightness=%d  lcdBr=%d  effect=%d  palette=%d  auto=%d  bg=%d\n",
    brightness, lcdBrightness, effectIndex, paletteIndex, autoCycle, botBackgroundStyle);
}

// ── Save ────────────────────────────────────────────────────────────────────
void saveSettings() {
  settingsPrefs.begin("vizbot", false);  // read-write

  settingsPrefs.putUChar("bright",  brightness);
  settingsPrefs.putUChar("lcdBr",   lcdBrightness);
  settingsPrefs.putUChar("effect",  effectIndex);
  settingsPrefs.putUChar("palette", paletteIndex);
  settingsPrefs.putBool ("autoCyc", autoCycle);
  settingsPrefs.putUChar("bgStyle", botBackgroundStyle);

  settingsPrefs.end();
  settingsDirty = false;

  Serial.println("Settings saved to NVS");
}

// ── Dirty flag ──────────────────────────────────────────────────────────────
void markSettingsDirty() {
  settingsDirty    = true;
  settingsDirtyAt  = millis();
}

// Call from loop() — writes to flash only after debounce period
void flushSettingsIfDirty() {
  if (settingsDirty && (millis() - settingsDirtyAt >= SETTINGS_DEBOUNCE_MS)) {
    saveSettings();
  }
}
