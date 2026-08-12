#ifndef MIDI_SYNTH_H
#define MIDI_SYNTH_H

#ifdef MIDI_SYNTH_ENABLED

#include <Arduino.h>
#include <M5_SAM2695.h>
#include "config.h"

// Runtime-selected synth Grove port (0=Port C, 1=Port A). Defined in vizbot.ino,
// loaded from NVS, changeable on the web config page.
extern uint8_t midiSynthPort;
static inline uint8_t midiTxPin() {
  return midiSynthPort == MIDI_SYNTH_PORT_A ? MIDI_PORTA_TX_PIN : MIDI_PORTC_TX_PIN;
}
static inline uint8_t midiRxPin() {
  return midiSynthPort == MIDI_SYNTH_PORT_A ? MIDI_PORTA_RX_PIN : MIDI_PORTC_RX_PIN;
}

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

    // TX pin sends data to SAM2695 RXD (Port C = G17, Port A = G2)
    sam.begin(&Serial2, MIDI_BAUD, midiRxPin(), midiTxPin());
    DBG("MIDI Synth: port=");
    DBG(midiSynthPort == MIDI_SYNTH_PORT_A ? "A" : "C");
    DBG(" Serial2 RX=");
    DBG(midiRxPin());
    DBG(" TX=");
    DBGLN(midiTxPin());

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
    Serial2.begin(MIDI_BAUD, SERIAL_8N1, midiRxPin(), midiTxPin());
    delay(50);
    sam.setMasterVolume(100);
    delay(10);
    DBGLN("MIDI Synth: UART re-initialized");
  }

  // Switch the synth to a different Grove port at runtime (0=Port C, 1=Port A).
  // Re-points the UART at the new pins and re-arms the SAM2695.
  void setPort(uint8_t port) {
    midiSynthPort = (port == MIDI_SYNTH_PORT_A) ? MIDI_SYNTH_PORT_A : MIDI_SYNTH_PORT_C;
    Serial2.end();
    delay(20);
    Serial2.begin(MIDI_BAUD, SERIAL_8N1, midiRxPin(), midiTxPin());
    delay(50);
    sam.reset();
    delay(200);
    sam.setMasterVolume(100);
    ready = true;
    DBG("MIDI Synth: port switched to ");
    DBGLN(midiSynthPort == MIDI_SYNTH_PORT_A ? "A" : "C");
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
