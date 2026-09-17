#ifndef FACES_BASE_H
#define FACES_BASE_H

#ifdef BOARD_HAS_FACES_BASE

// ============================================================================
// Faces Bottom3 base — the LED half of a stack-chan base, and nothing else
// ============================================================================
// This target is a CoreS3 sitting in an M5Stack Faces Bottom3 (the game base:
// a gamepad panel and two SK6812 strips). It shares exactly one thing with the
// stack-chan base — an addressable strip the effect engine can drive — and none
// of the rest: no PY32 IO expander, no SCS0009 servos, no Si12T head touch, no
// INA226.
//
// So this file provides the three entry points stackchan_leds.h actually uses
// and stackchan_base.h is left out of the build entirely. That matters more than
// it sounds: stackchan_base.h sets every sysStatus.sc*Ready flag to true without
// probing, so on this hardware it would report a healthy servo stack and then
// call scRecoverServos() every five minutes — 4.6 seconds of blocking delay()
// each time, on hardware that has no servos.
//
// Hardware notes:
//
//  * The strips are ten SK6812 (five per side) on GPIO13, driven directly. The
//    stack-chan ring goes through the IO expander over I2C; this does not, so
//    the shared I2C mutex is not involved and a refresh costs ~300us of RMT
//    rather than 13 I2C transactions.
//
//  * GPIO13 is ALSO CoreS3's I2S bit clock. While the mic or speaker holds I2S,
//    the strips show continuous garbage — no firmware fix, it is one wire doing
//    two jobs, and the base's LED-IO switch is not reachable on this unit. Audio
//    is therefore mutually exclusive with the LEDs: see facesAudioMode().
// ============================================================================

#include <Arduino.h>
#include <FastLED.h>

#include "config.h"
#include "system_status.h"

// The strips. Separate from the `leds[]` array the LED-matrix targets use —
// that one is NUM_LEDS on DATA_PIN and unrelated to this base.
static CRGB scBaseLeds[SC_BASE_LED_COUNT];

// !! Hold our own controller. bootStageLEDs() registers the matrix array with
// FastLED before this runs, so it owns FastLED[0] and we are FastLED[1] — and
// a plain FastLED.show() (or FastLED[0].showLeds()) pushes the wrong strip
// while ours stays dark, with no error anywhere. Ask for our controller by
// name and the ambiguity cannot come back.
static CLEDController* scBaseCtrl = nullptr;

inline void scInitBaseLeds() {
  scBaseCtrl =
      &FastLED.addLeds<SK6812, FACES_LED_PIN, GRB>(scBaseLeds, SC_BASE_LED_COUNT);
  for (int i = 0; i < SC_BASE_LED_COUNT; i++) scBaseLeds[i] = CRGB::Black;
  scBaseCtrl->showLeds(255);
  sysStatus.scBaseLedsReady = true;
  Serial.printf("[faces] %d x SK6812 on GPIO%d\n", SC_BASE_LED_COUNT,
                FACES_LED_PIN);
}

inline void scSetBaseLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (!sysStatus.scBaseLedsReady || index >= SC_BASE_LED_COUNT) return;
  scBaseLeds[index] = CRGB(r, g, b);
}

inline void scRefreshBaseLeds() {
  if (!sysStatus.scBaseLedsReady || !scBaseCtrl) return;
  // Controller-scoped show, on OUR controller: FastLED.show() would also push
  // the matrix array this board does not have wired, and FastLED[0] IS that
  // array — see the comment on scBaseCtrl.
  scBaseCtrl->showLeds(255);
}

inline void scShowBaseLedColor(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < SC_BASE_LED_COUNT; i++) scSetBaseLedColor(i, r, g, b);
  scRefreshBaseLeds();
}

// Hand the strips over cleanly when audio takes GPIO13, and take them back
// afterwards. Blanking first matters: SK6812s latch their last frame and are
// powered from the base's 5V rail, so a strip left lit stays lit through the
// whole time the mic owns the pin.
inline void scBaseLedsRelease() {
  if (!sysStatus.scBaseLedsReady) return;
  scShowBaseLedColor(0, 0, 0);
  delay(2);
  sysStatus.scBaseLedsReady = false;
}

inline void scBaseLedsReclaim() {
  sysStatus.scBaseLedsReady = true;
  scShowBaseLedColor(0, 0, 0);
}

#endif  // BOARD_HAS_FACES_BASE
#endif  // FACES_BASE_H
