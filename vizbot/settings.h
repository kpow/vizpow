#pragma once
// ============================================================================
// settings.h — Persistent settings via ESP32 Preferences (NVS)
// ============================================================================
// Saves user-facing state (brightness, palette, etc.) to flash so they
// survive power cycles.  Writes are debounced — call markSettingsDirty()
// whenever a value changes, then call flushSettingsIfDirty() from the main
// loop.  This avoids hammering NVS on rapid slider moves.

#include <Preferences.h>

// Dirty-flag state — not static, lives in the single .ino compilation unit
bool           settingsDirty    = false;
unsigned long  settingsDirtyAt  = 0;
const unsigned long SETTINGS_DEBOUNCE_MS = 2000;  // wait 2s after last change

// Forward declarations — these are the globals we persist
extern uint8_t brightness;
extern uint8_t effectIndex;
extern uint8_t paletteIndex;
extern bool    autoCycle;
extern uint8_t botBackgroundStyle;
extern uint8_t lcdBrightness;

// ── Load ────────────────────────────────────────────────────────────────────
void loadSettings() {
  Preferences prefs;
  // Open read-write so the namespace is created on first boot
  if (!prefs.begin("vizbot", false)) {
    Serial.println("!! NVS: failed to open 'vizbot' — using defaults");
    return;
  }

  brightness         = prefs.getUChar("bright",  brightness);
  lcdBrightness      = prefs.getUChar("lcdBr",   lcdBrightness);
  effectIndex        = prefs.getUChar("effect",   effectIndex);
  paletteIndex       = prefs.getUChar("palette",  paletteIndex);
  autoCycle          = prefs.getBool ("autoCyc",  autoCycle);
  botBackgroundStyle = prefs.getUChar("bgStyle",  botBackgroundStyle);

  prefs.end();

  Serial.println("Settings loaded from NVS");
  Serial.printf("  bright=%d  lcdBr=%d  fx=%d  pal=%d  auto=%d  bg=%d\n",
    brightness, lcdBrightness, effectIndex, paletteIndex, autoCycle, botBackgroundStyle);
}

// ── Save ────────────────────────────────────────────────────────────────────
void saveSettings() {
  Preferences prefs;
  if (!prefs.begin("vizbot", false)) {  // read-write
    Serial.println("!! NVS: failed to open 'vizbot' for writing");
    return;
  }

  prefs.putUChar("bright",  brightness);
  prefs.putUChar("lcdBr",   lcdBrightness);
  prefs.putUChar("effect",  effectIndex);
  prefs.putUChar("palette", paletteIndex);
  prefs.putBool ("autoCyc", autoCycle);
  prefs.putUChar("bgStyle", botBackgroundStyle);

  prefs.end();
  settingsDirty = false;

  Serial.printf("Settings saved: bright=%d  lcdBr=%d  fx=%d  pal=%d  auto=%d  bg=%d\n",
    brightness, lcdBrightness, effectIndex, paletteIndex, autoCycle, botBackgroundStyle);
}

// ── Dirty flag ──────────────────────────────────────────────────────────────
void markSettingsDirty() {
  settingsDirty   = true;
  settingsDirtyAt = millis();
}

// Call from loop() — writes to flash only after debounce period
void flushSettingsIfDirty() {
  if (settingsDirty && (millis() - settingsDirtyAt >= SETTINGS_DEBOUNCE_MS)) {
    saveSettings();
  }
}
