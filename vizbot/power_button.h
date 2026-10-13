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
//
// Low-battery cutoff (BAT_CUTOFF_MV): nothing on the board stops the cell being
// drained flat — a measured run-down went to 2.6 V and rebooted there. Once
// the smoothed battery voltage has sat under the cutoff for BAT_CUTOFF_HOLD_MS
// the latch is dropped the same way.
// ============================================================================

#define PWR_OFF_HOLD_MS   2000
#define PWR_DEBOUNCE_MS   30

struct PowerButton {
  bool armed = false;         // false until the power-on press has been released
  bool down = false;
  unsigned long changeMs = 0;
  unsigned long downMs = 0;
#ifdef BAT_CUTOFF_MV
  unsigned long lowSinceMs = 0;
  bool cutoffSpent = false;   // latch drop left the board running: on a charger
#endif

  void init() {
    pinMode(PWR_KEY_PIN, INPUT_PULLUP);
  }

  // Call every loop.
  void update() {
    bool raw = digitalRead(PWR_KEY_PIN) == LOW;
    unsigned long now = millis();
#ifdef BAT_CUTOFF_MV
    checkBattery(now);
#endif
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

  // Say goodbye, go dark and drop the latch. Returns only if the board is still
  // powered afterwards, which means something external is feeding it.
  void shutDown(const char* msg) {
    if (settingsDirty) saveSettings();
    #ifdef TOUCH_UI_V2
    uiToast(msg, UI_C_OFF, 5000);
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
  }

  void powerOff() {
    DBGLN("PWR button: held — powering off");
    shutDown("Goodbye!");
    ESP.restart();   // still running = external power; come back up
  }

#ifdef BAT_CUTOFF_MV
  void checkBattery(unsigned long now) {
    int mv = (int)(batteryAdc.volts * 1000.0f);
    if (batteryAdc.usb || mv >= BAT_CUTOFF_MV) {
      lowSinceMs = 0;
      if (mv >= BAT_CUTOFF_MV + BAT_CUTOFF_REARM_MV) cutoffSpent = false;
      return;
    }
    if (cutoffSpent) return;
    if (lowSinceMs == 0) { lowSinceMs = now | 1; return; }
    if (now - lowSinceMs < BAT_CUTOFF_HOLD_MS) return;

    DBGLN("Battery empty — powering off");
    #ifdef BAT_RUNDOWN_LOG
    batteryAdc.runLogNote("cutoff");
    #endif
    shutDown("Battery empty");
    // Still running: a charger that sends no USB frames is feeding the board, and
    // losing to the load. Carry on, and leave the cutoff alone until it recovers.
    DBGLN("Battery empty — still powered, staying on");
    digitalWrite(PWR_HOLD_PIN, HIGH);
    setLCDBacklight(lcdBrightness);
    lowSinceMs = 0;
    cutoffSpent = true;
  }
#endif
};

static PowerButton powerButton;

// "Power off" row in the touch UI System menu
void pwrPowerOff() { powerButton.powerOff(); }

#endif // PWR_HOLD_PIN
#endif // POWER_BUTTON_H
