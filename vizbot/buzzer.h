#ifndef BUZZER_H
#define BUZZER_H

#ifdef BUZZER_PIN

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Passive piezo buzzer (Waveshare 1.69) — LEDC square wave on BUZZER_PIN
// ============================================================================
// Passive = it only sounds while driven with a square wave. When idle the pin
// must sit LOW: Waveshare's FAQ notes a floating buzzer pin keeps the buzzer
// drawing current through the LDO and heats the board.
//
// Fixed LEDC channel 0 (timer 0). LovyanGFX's backlight PWM owns channel 7
// (timer 3), which Arduino's channel allocator can't see.
// ============================================================================

#define BUZZER_LEDC_CHANNEL 0
#define BUZZER_LEDC_RES     10      // duty range 0..1023; 512 = 50% = loudest

struct Buzzer {
  bool attached = false;
  bool sounding = false;
  unsigned long endMs = 0;

  void init() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    attached = ledcAttachChannel(BUZZER_PIN, 2000, BUZZER_LEDC_RES, BUZZER_LEDC_CHANNEL);
    if (attached) ledcWrite(BUZZER_PIN, 0);
  }

  // Start a tone. durationMs == 0 holds until off(). duty 1..512 sets loudness.
  void tone(uint16_t freq, uint16_t durationMs, uint16_t duty = 512) {
    if (!attached || freq == 0) { off(); return; }
    ledcChangeFrequency(BUZZER_PIN, freq, BUZZER_LEDC_RES);
    ledcWrite(BUZZER_PIN, min<uint16_t>(duty, 512));
    sounding = true;
    endMs = durationMs ? millis() + durationMs : 0;
  }

  void off() {
    if (attached) ledcWrite(BUZZER_PIN, 0);   // duty 0 holds the pin LOW
    sounding = false;
    endMs = 0;
  }

  // Call every loop: ends timed tones without blocking.
  void update() {
    if (sounding && endMs && (long)(millis() - endMs) >= 0) off();
  }
};

static Buzzer buzzer;

#endif // BUZZER_PIN
#endif // BUZZER_H
