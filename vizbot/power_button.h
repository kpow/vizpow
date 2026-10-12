#ifndef POWER_BUTTON_H
#define POWER_BUTTON_H

#ifdef PWR_HOLD_PIN

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PWR button (Waveshare 1.69) — hold to power off on battery
// ============================================================================
// On battery the PWR button feeds the board only while it is held; setup()
// latches PWR_HOLD_PIN (SYS_EN) HIGH to stay on after release. SYS_OUT
// (PWR_KEY_PIN) reads LOW while the button is down. Holding it for
// PWR_OFF_HOLD_MS drops the latch, so the board dies when the button is
// released.
//
// On USB the latch has no effect — the board stays powered — so we restart
// instead of sitting there dark with the latch released.
// ============================================================================

#define PWR_OFF_HOLD_MS   2000
#define PWR_DEBOUNCE_MS   30

struct PowerButton {
  bool armed = false;         // false until the power-on press has been released
  bool down = false;
  unsigned long changeMs = 0;
  unsigned long downMs = 0;

  void init() {
    pinMode(PWR_KEY_PIN, INPUT_PULLUP);
  }

  // Call every loop.
  void update() {
    bool raw = digitalRead(PWR_KEY_PIN) == LOW;
    unsigned long now = millis();
    if (raw != down) {
      if (changeMs == 0) changeMs = now;
      if (now - changeMs < PWR_DEBOUNCE_MS) return;
      down = raw;
      changeMs = 0;
      downMs = now;
      DBGLN(down ? "PWR button: down" : "PWR button: up");
      return;
    }
    changeMs = 0;
    if (!down) armed = true;
    if (armed && down && now - downMs >= PWR_OFF_HOLD_MS) powerOff();
  }

  void powerOff() {
    DBGLN("PWR button: held — powering off");
    if (settingsDirty) saveSettings();
    #ifdef TOUCH_UI_V2
    uiToast("Goodbye!", UI_C_OFF, 5000);
    uiSound(SEQ_POWER_DOWN);
    #endif
    // Let the toast and the power-down sound play out
    unsigned long until = millis() + 800;
    while ((long)(millis() - until) < 0) {
      runBotMode();
      #ifdef HAS_SOUND
      botSounds.update();
      #endif
      #ifdef BUZZER_PIN
      buzzer.update();
      #endif
    }
    #ifdef BUZZER_PIN
    buzzer.off();
    #endif
    #ifdef BOARD_HAS_RING_LEDS
    scShowBaseLedColor(0, 0, 0);
    #endif
    setLCDBacklight(0);

    digitalWrite(PWR_HOLD_PIN, LOW);
    while (digitalRead(PWR_KEY_PIN) == LOW) delay(10);   // button still feeds the board
    delay(500);
    ESP.restart();   // still running = external power; come back up
  }
};

static PowerButton powerButton;

// "Power off" row in the touch UI System menu
void pwrPowerOff() { powerButton.powerOff(); }

#endif // PWR_HOLD_PIN
#endif // POWER_BUTTON_H
