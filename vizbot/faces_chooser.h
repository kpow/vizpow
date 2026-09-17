#ifndef FACES_CHOOSER_H
#define FACES_CHOOSER_H

#ifdef BOARD_HAS_FACES_BASE

// ============================================================================
// Handing the device back to the chooser
// ============================================================================
// This CoreS3 carries three apps: a chooser in the `factory` partition, the
// kFun game console in ota_0, and this firmware in ota_1. The chooser is the
// top level — power on, pick one.
//
// Two things are needed to keep that true, and they are not the same thing:
//
//  armChooserNextBoot()  Called once at startup. Choosing an app writes otadata,
//                        and otadata outranks the factory partition on every
//                        subsequent boot — so without this, picking vizBot once
//                        would mean the device booted straight into vizBot
//                        forever after, and the chooser would never be seen
//                        again. Re-arming at startup costs one otadata write per
//                        boot and makes "power on, choose" true every time.
//
//  bootToChooser()       Called from the menu. Same thing, plus an orderly
//                        shutdown and a restart now.
//
// The game firmware does exactly the same two things — see Shell::armChooser()
// and Shell::bootToChooser() in ~/projects/tinyfun/common/engine/Shell.cpp.
// ============================================================================

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "faces_base.h"

extern void saveSettings();

inline const esp_partition_t* facesChooserPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                  ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
}

inline void armChooserNextBoot() {
  const esp_partition_t* p = facesChooserPartition();
  if (!p) return;  // no chooser installed: this board boots straight here
  const esp_err_t err = esp_ota_set_boot_partition(p);
  Serial.printf("[faces] next boot -> chooser at 0x%06lx: %s\n",
                (unsigned long)p->address, esp_err_to_name(err));
}

// ---------------------------------------------------------------------------
// The way out, on the gamepad
// ---------------------------------------------------------------------------
// vizBot has no button input of any kind — its own UI is touch and web — but
// this base has a Gamepad3 panel on it, and the game console in the other
// partition leaves by holding START+SELECT. The same grip should work here, so
// the gesture means one thing on the device rather than one thing per firmware.
//
// This is deliberately NOT an input layer: one I2C register read per frame and
// a hold timer, nothing routed anywhere. START+SELECT is also the only chord
// the console reserves, so it can never collide with a game.
constexpr uint8_t kFacesPadAddr   = 0x08;
constexpr uint8_t kFacesRegKey    = 0x00;
constexpr uint8_t kFacesBitSelect = 1 << 6;
constexpr uint8_t kFacesBitStart  = 1 << 7;
constexpr uint32_t kFacesChordMs  = 800;

inline void bootToChooser();

inline void facesChordPoll() {
  static uint32_t chordStart = 0;
  static bool     armed      = false;

  uint8_t mask = 0xFF;  // active-LOW: all ones = nothing pressed
  if (!M5.In_I2C.readRegister(kFacesPadAddr, kFacesRegKey, &mask, 1, 100000)) {
    chordStart = 0;
    return;  // no panel fitted: nothing to do, and no false positives
  }

  // The panel's STM32 reports garbage for a frame or two after power-on (0x03
  // has been seen, which reads as six keys held). Wait for one clean read
  // before believing anything, or the chord fires during boot.
  if (!armed) {
    if (mask == 0xFF) armed = true;
    return;
  }

  const bool held = (mask & kFacesBitStart) == 0 && (mask & kFacesBitSelect) == 0;
  if (!held) {
    chordStart = 0;
    return;
  }
  const uint32_t now = millis();
  if (!chordStart) {
    chordStart = now;
    return;
  }
  if (now - chordStart >= kFacesChordMs) {
    chordStart = 0;
    Serial.println("[faces] START+SELECT -> chooser");
    bootToChooser();
  }
}

inline void bootToChooser() {
  const esp_partition_t* p = facesChooserPartition();
  if (!p) return;

  // Settings writes are debounced by two seconds, so whatever was changed in
  // the last moment before this is still only in RAM.
  saveSettings();

  // Blank the strips before going. They latch their last frame and are powered
  // from the base's 5V rail, so a lit strip would sit there through the
  // chooser's entire boot.
  scShowBaseLedColor(0, 0, 0);
  delay(4);

  if (esp_ota_set_boot_partition(p) != ESP_OK) return;
  delay(40);
  esp_restart();
}

#endif  // BOARD_HAS_FACES_BASE
#endif  // FACES_CHOOSER_H
