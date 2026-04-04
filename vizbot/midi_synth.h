#ifndef MIDI_SYNTH_H
#define MIDI_SYNTH_H

#ifdef MIDI_SYNTH_ENABLED

#include <Arduino.h>
#include <M5_SAM2695.h>
#include "config.h"

// ============================================================================
// MIDI Synthesizer Driver — SAM2695 via official M5Stack library
// ============================================================================
// Wraps https://github.com/m5stack/M5-SAM2695
// Core S3 Port C: GPIO 18 (TXD2), GPIO 17 (RXD2)
// ============================================================================

// GM instrument presets (subset)
#define GM_PIANO          0
#define GM_BRIGHT_PIANO   1
#define GM_ELECTRIC_PIANO 4
#define GM_CELESTA        8
#define GM_GLOCKENSPIEL   9
#define GM_MUSIC_BOX     10
#define GM_VIBRAPHONE    11
#define GM_MARIMBA       12
#define GM_XYLOPHONE     13
#define GM_TUBULAR_BELLS 14
#define GM_NYLON_GUITAR  24
#define GM_STEEL_GUITAR  25
#define GM_BLOWN_BOTTLE  76
#define GM_LEAD_SAW      81
#define GM_PAD_WARM      89
#define GM_AGOGO        113
#define GM_STEEL_DRUMS  114
#define GM_WOODBLOCK    115
#define GM_SYNTH_DRUM   118

// GM percussion note numbers (channel 10 / index 9)
#define PERC_BASS_DRUM   36
#define PERC_SNARE       38
#define PERC_HAND_CLAP   39
#define PERC_CLOSED_HH   42
#define PERC_OPEN_HH     46
#define PERC_COWBELL     56
#define PERC_HI_BONGO    60
#define PERC_LOW_BONGO   61
#define PERC_CABASA      69
#define PERC_WOODBLOCK   76
#define PERC_TRIANGLE    81

// MIDI channels
#define MIDI_CH_PERCUSSION 9
#define MIDI_MAX_CHANNELS  16

// Thin wrapper around M5_SAM2695 official library
struct MidiSynth {
  M5_SAM2695 sam;
  bool ready;

  void init() {
    ready = false;

    // Core S3 Port C: TX=17 sends data to SAM2695 RXD
    sam.begin(&Serial2, MIDI_BAUD, MIDI_RX_PIN, MIDI_TX_PIN);
    DBG("MIDI Synth: Serial2 RX=");
    DBG(MIDI_RX_PIN);
    DBG(" TX=");
    DBGLN(MIDI_TX_PIN);

    delay(100);
    sam.reset();
    delay(300);

    sam.setMasterVolume(127);
    delay(20);

    sam.setInstrument(0, 0, GM_PIANO);
    delay(20);

    // Test note
    DBGLN("MIDI Synth: test note C5...");
    sam.setNoteOn(0, 72, 127);
    delay(600);
    sam.setNoteOff(0, 72, 0);
    delay(50);

    ready = true;
    DBGLN("MIDI Synth: init OK");
  }

  // Re-establish UART after boot — something during boot reclaims GPIO 17/18.
  // Call once after boot_sequence completes and before first real playback.
  void reinit() {
    if (!ready) return;
    Serial2.end();
    delay(20);
    Serial2.begin(MIDI_BAUD, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);
    delay(50);
    sam.setMasterVolume(100);
    delay(10);
    DBGLN("MIDI Synth: UART re-initialized");
  }

  // --- Forwarding to official lib ---

  void noteOn(uint8_t ch, uint8_t note, uint8_t velocity) {
    sam.setNoteOn(ch, note, velocity);
  }

  void noteOff(uint8_t ch, uint8_t note, uint8_t velocity = 0) {
    sam.setNoteOff(ch, note, velocity);
  }

  void programChange(uint8_t ch, uint8_t program) {
    sam.setInstrument(0, ch, program);  // bank 0
  }

  void controlChange(uint8_t ch, uint8_t controller, uint8_t value) {
    // Direct CC send via raw serial (official lib doesn't expose generic CC)
    uint8_t cmd[] = {(uint8_t)(0xB0 | (ch & 0x0F)), (uint8_t)(controller & 0x7F), (uint8_t)(value & 0x7F)};
    Serial2.write(cmd, 3);
  }

  void setVolume(uint8_t vol255) {
    sam.setMasterVolume(vol255 >> 1);  // 0-255 -> 0-127
  }

  void setChannelVolume(uint8_t ch, uint8_t vol127) {
    sam.setVolume(ch, vol127);
  }

  void setReverb(uint8_t ch, uint8_t level) {
    controlChange(ch, 0x5B, level & 0x7F);
  }

  void allNotesOff(uint8_t ch) {
    sam.setAllNotesOff(ch);
  }

  void allNotesOffAll() {
    for (uint8_t ch = 0; ch < MIDI_MAX_CHANNELS; ch++) {
      sam.setAllNotesOff(ch);
    }
  }

  void systemReset() {
    sam.reset();
  }
};

// Global instance
MidiSynth midiSynth;

#endif // MIDI_SYNTH_ENABLED
#endif // MIDI_SYNTH_H
