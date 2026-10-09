#ifndef RING_LEDS_H
#define RING_LEDS_H

#ifdef BOARD_HAS_RING_LEDS

// ============================================================================
// Interior LED ring on the 1.69 — a third backend for the base LED engine
// ============================================================================
// A small WS2812-style ring wired to RING_LED_PIN, lighting the inside of the
// bot. Like the Faces strips it is driven directly over RMT, so it only has to
// provide the entry points stackchan_leds.h calls; the effect engine, modes and
// web/touch controls are shared with the stack-chan and Faces bases.
//
// Hardware notes:
//
//  * Power it from 5V (VBUS), not the 3V3 rail. The 1.69's LDO already looks
//    marginal under full WiFi TX (see WIFI_TX_POWER in config.h); a ring pulling
//    a few hundred mA from the same rail would make that worse. The 3.3V data
//    line drives a 5V WS2812 fine over a short wire.
//
//  * bootStageLEDs() registers the 8x8 matrix array on DATA_PIN with FastLED on
//    every target, so it owns FastLED[0] and the ring is FastLED[1]. Same trap
//    as faces_base.h: show through our own controller, never FastLED.show().
// ============================================================================

#include <Arduino.h>
#include <FastLED.h>

#include "config.h"
#include "system_status.h"

static CRGB scBaseLeds[SC_BASE_LED_COUNT];
static CLEDController* scBaseCtrl = nullptr;

inline void scInitBaseLeds() {
  scBaseCtrl = &FastLED.addLeds<RING_LED_CHIPSET, RING_LED_PIN, RING_LED_ORDER>(
      scBaseLeds, SC_BASE_LED_COUNT);
  for (int i = 0; i < SC_BASE_LED_COUNT; i++) scBaseLeds[i] = CRGB::Black;
  scBaseCtrl->showLeds(255);
  sysStatus.scBaseLedsReady = true;
  Serial.printf("[ring] %d LEDs on GPIO%d\n", SC_BASE_LED_COUNT, RING_LED_PIN);
}

inline void scSetBaseLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (!sysStatus.scBaseLedsReady || index >= SC_BASE_LED_COUNT) return;
  scBaseLeds[index] = CRGB(r, g, b);
}

inline void scRefreshBaseLeds() {
  if (!sysStatus.scBaseLedsReady || !scBaseCtrl) return;
  scBaseCtrl->showLeds(255);   // our controller only — see header comment
}

inline void scShowBaseLedColor(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < SC_BASE_LED_COUNT; i++) scSetBaseLedColor(i, r, g, b);
  scRefreshBaseLeds();
}

#endif  // BOARD_HAS_RING_LEDS
#endif  // RING_LEDS_H
