#ifndef BOT_SOUNDS_H
#define BOT_SOUNDS_H

#ifdef TARGET_CORES3

#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

#ifdef MIDI_SYNTH_ENABLED
#include "midi_synth.h"
#endif

// ============================================================================
// Bot Sounds — MIDI Sequence Engine with M5.Speaker Fallback
// ============================================================================
// Primary: drives SAM2695 MIDI synth via midi_synth.h (multi-voice, GM instruments)
// Fallback: sine-wave tones via M5.Speaker when MIDI module not available
//
// Timeline-based sequencer: events have offsetMs from sequence start, enabling
// polyphony (multiple events at same offset = chord/layered instruments).
// Non-blocking: update() advances via millis(), never blocks the render loop.
// ============================================================================

// --- Sequence IDs ---

enum MidiSequenceId : uint8_t {
  SEQ_NONE = 0,

  // Original 9 SFX (same trigger semantics, upgraded MIDI sounds)
  SEQ_BOOT_CHIME,       // 1  - Startup fanfare
  SEQ_TAP_BOOP,         // 2  - Touch/tap feedback
  SEQ_SHAKE_RATTLE,     // 3  - Shake reaction
  SEQ_WAKE_CHIME,       // 4  - Wake from sleep
  SEQ_SLEEP_DESCEND,    // 5  - Going to sleep
  SEQ_BUBBLE_POP,       // 6  - Speech bubble appears
  SEQ_EXPR_CHIRP,       // 7  - Expression change blip
  SEQ_NOTIFICATION,     // 8  - Alert / cloud command
  SEQ_CLAP_REACT,       // 9  - Loud sound reaction

  // New built-in sequences
  SEQ_SUCCESS,          // 10 - Positive confirmation
  SEQ_ERROR,            // 11 - Negative / error
  SEQ_LEVEL_UP,         // 12 - RPG level-up fanfare
  SEQ_COIN,             // 13 - Coin collect
  SEQ_AMBIENT_CHIME,    // 14 - Gentle wind chime
  SEQ_HEARTBEAT,        // 15 - Two-pulse heartbeat
  SEQ_MYSTERY,          // 16 - Mysterious arpeggio
  SEQ_FANFARE,          // 17 - Short trumpet fanfare
  SEQ_POWER_UP,         // 18 - Ascending power-up
  SEQ_POWER_DOWN,       // 19 - Descending power-down
  SEQ_PING,             // 20 - Network ping
  SEQ_WHISTLE,          // 21 - Short whistle
  SEQ_TYPING,           // 22 - Keyboard click
  SEQ_RAIN_DROP,        // 23 - Single raindrop
  SEQ_COUNTDOWN_TICK,   // 24 - Metronome tick

  // Additional SFX (interaction + character emotion)
  SEQ_SWIPE,            // 25 - Swipe gesture
  SEQ_TOGGLE_ON,        // 26 - Toggle on
  SEQ_TOGGLE_OFF,       // 27 - Toggle off
  SEQ_DISMISS,          // 28 - Dismiss / cancel
  SEQ_CONFIRM,          // 29 - Confirm action
  SEQ_HAPPY_HUM,        // 30 - Cheerful hum
  SEQ_SAD_SIGH,         // 31 - Sad sigh
  SEQ_CURIOUS_BEEP,     // 32 - Curious question
  SEQ_LAUGH,            // 33 - Bouncy laugh
  SEQ_YAWN,             // 34 - Sleepy yawn

  // Full songs
  SEQ_SONG_FUNKY_BOT,   // 35 - Upbeat funky tune
  SEQ_SONG_CHILL_WAVE,  // 36 - Ambient chill
  SEQ_SONG_RETRO_QUEST, // 37 - Retro game theme

  SEQ_BUILTIN_COUNT,    // Sentinel

  // Cloud-delivered sequences start here
  SEQ_CLOUD_BASE = 128
};

// --- MIDI Event (single note/hit in a sequence) ---

struct MidiEvent {
  uint8_t  note;        // MIDI note 0-127 (0 with duration 0 = terminator)
  uint8_t  velocity;    // 0-127
  uint8_t  channel;     // 0-15 (9 = percussion)
  uint8_t  program;     // GM instrument (255 = no change from sequence default)
  uint16_t durationMs;  // How long the note sounds
  uint16_t offsetMs;    // Time from sequence start (same offset = simultaneous)
};

// --- Sequence Definition ---

#define SEQ_MAX_EVENTS 128  // cloud-sequence event cap (matches vizCloud MAX_CLOUD_EVENTS)
#define SEQ_FLAG_LOOP       0x01
#define SEQ_FLAG_PERCUSSION 0x02

struct MidiSequenceDef {
  const char*       name;
  uint8_t           eventCount;
  uint8_t           defaultProgram;  // GM instrument for channel 0
  uint8_t           flags;
  const MidiEvent*  events;          // PROGMEM or heap pointer
};

// ============================================================================
// Built-in PROGMEM Sequences
// ============================================================================
// Events are sorted by offsetMs. Multiple events at same offset play together.
// program=255 means "use sequence default". channel=9 = percussion.

// --- Boot Chime: Vibraphone G4 -> B4 -> D5 ascending triad ---
static const MidiEvent PROGMEM _evBoot[] = {
  {67, 90, 0, 255,  150, 0},     // G4
  {71, 100, 0, 255, 150, 180},   // B4
  {74, 110, 0, 255, 250, 360},   // D5
};

// --- Tap Boop: Marimba quick descending boop ---
static const MidiEvent PROGMEM _evTap[] = {
  {71, 80, 0, 255,  50, 0},      // B4 quick
  {64, 90, 0, 255,  80, 60},     // E4 drop
};

// --- Shake Rattle: Agogo tumbling descent + percussion ---
static const MidiEvent PROGMEM _evShake[] = {
  {74, 80, 0, GM_AGOGO, 60, 0},     // D5
  {71, 80, 0, 255,      60, 80},    // B4
  {67, 80, 0, 255,      60, 160},   // G4
  {62, 90, 0, 255,      80, 240},   // D4
  {PERC_CABASA, 70, 9, 255, 30, 0}, // Cabasa hit
  {PERC_CABASA, 60, 9, 255, 30, 80},
  {PERC_CABASA, 50, 9, 255, 30, 160},
};

// --- Wake Chime: Vibraphone soft pentatonic rise D4->E4->G4->A4 ---
static const MidiEvent PROGMEM _evWake[] = {
  {62, 70, 0, 255,  160, 0},     // D4
  {64, 80, 0, 255,  160, 200},   // E4
  {67, 90, 0, 255,  200, 400},   // G4
  {69, 100, 0, 255, 300, 620},   // A4
};

// --- Sleep Descend: Warm pad slow descent G4->E4->D4->B3 ---
static const MidiEvent PROGMEM _evSleep[] = {
  {67, 70, 0, GM_PAD_WARM, 200, 0},    // G4
  {64, 65, 0, 255,         200, 280},   // E4
  {62, 60, 0, 255,         220, 560},   // D4
  {59, 55, 0, 255,         300, 840},   // B3
};

// --- Bubble Pop: Blown bottle pop ---
static const MidiEvent PROGMEM _evBubble[] = {
  {76, 100, 0, GM_BLOWN_BOTTLE, 40, 0},   // E5
  {69, 80,  0, 255,             50, 45},   // A4
};

// --- Expression Chirp: Quick celesta blip ---
static const MidiEvent PROGMEM _evChirp[] = {
  {71, 70, 0, GM_CELESTA, 50, 0},   // B4
};

// --- Notification: Celesta two-tone ding D5->G5 ---
static const MidiEvent PROGMEM _evNotify[] = {
  {74, 90, 0, GM_CELESTA, 120, 0},    // D5
  {79, 100, 0, 255,       160, 150},   // G5
};

// --- Clap React: Hand clap + surprised bloop ---
static const MidiEvent PROGMEM _evClap[] = {
  {PERC_HAND_CLAP, 110, 9, 255, 30, 0},
  {74, 80, 0, GM_MARIMBA, 50, 20},     // D5
  {69, 70, 0, 255,        70, 80},      // A4
};

// --- Success: Music box ascending confirm ---
static const MidiEvent PROGMEM _evSuccess[] = {
  {72, 90, 0, GM_MUSIC_BOX, 100, 0},    // C5
  {76, 100, 0, 255,         100, 120},   // E5
  {79, 110, 0, 255,         180, 240},   // G5
};

// --- Error: Low buzz ---
static const MidiEvent PROGMEM _evError[] = {
  {48, 100, 0, GM_ELECTRIC_PIANO, 80, 0},   // C3
  {48, 90,  0, 255,               80, 120},  // C3 again
};

// --- Level Up: Classic RPG fanfare ---
static const MidiEvent PROGMEM _evLevelUp[] = {
  {60, 100, 0, GM_BRIGHT_PIANO, 80, 0},     // C4
  {64, 100, 0, 255,             80, 90},     // E4
  {67, 100, 0, 255,             80, 180},    // G4
  {72, 110, 0, 255,             200, 280},   // C5 hold
  {PERC_TRIANGLE, 80, 9, 255,  30, 280},    // Triangle hit
};

// --- Coin: Xylophone quick double ---
static const MidiEvent PROGMEM _evCoin[] = {
  {83, 110, 0, GM_XYLOPHONE, 60, 0},    // B5
  {88, 120, 0, 255,          100, 70},   // E6
};

// --- Ambient Chime: Tubular bells gentle ring ---
static const MidiEvent PROGMEM _evAmbient[] = {
  {72, 50, 0, GM_TUBULAR_BELLS, 400, 0},    // C5 soft
  {79, 40, 0, 255,              400, 300},   // G5 softer
};

// --- Heartbeat: Two gentle bass pulses ---
static const MidiEvent PROGMEM _evHeartbeat[] = {
  {48, 80, 0, GM_PIANO,  100, 0},     // C3
  {48, 60, 0, 255,       80, 150},    // C3 softer
};

// --- Mystery: Celesta chromatic arpeggio ---
static const MidiEvent PROGMEM _evMystery[] = {
  {64, 70, 0, GM_CELESTA, 120, 0},    // E4
  {68, 75, 0, 255,        120, 150},  // Ab4
  {71, 80, 0, 255,        120, 300},  // B4
  {75, 85, 0, 255,        200, 450},  // Eb5
};

// --- Fanfare: Bright piano short trumpet-like ---
static const MidiEvent PROGMEM _evFanfare[] = {
  {67, 100, 0, GM_BRIGHT_PIANO, 80, 0},     // G4
  {67, 100, 0, 255,             60, 100},    // G4
  {72, 110, 0, 255,             80, 180},    // C5
  {76, 120, 0, 255,             120, 280},   // E5
  {79, 120, 0, 255,             250, 380},   // G5 hold
  {PERC_SNARE, 90, 9, 255,     30, 0},      // Snare roll start
};

// --- Power Up: Glockenspiel ascending ---
static const MidiEvent PROGMEM _evPowerUp[] = {
  {60, 80, 0, GM_GLOCKENSPIEL, 60, 0},     // C4
  {64, 85, 0, 255,             60, 60},     // E4
  {67, 90, 0, 255,             60, 120},    // G4
  {72, 95, 0, 255,             60, 180},    // C5
  {76, 100, 0, 255,            60, 240},    // E5
  {79, 110, 0, 255,            120, 300},   // G5
};

// --- Power Down: Glockenspiel descending ---
static const MidiEvent PROGMEM _evPowerDown[] = {
  {79, 90, 0, GM_GLOCKENSPIEL, 60, 0},     // G5
  {76, 85, 0, 255,             60, 70},     // E5
  {72, 80, 0, 255,             60, 140},    // C5
  {67, 75, 0, 255,             60, 210},    // G4
  {60, 70, 0, 255,             120, 280},   // C4
};

// --- Ping: Steel drum quick double tap ---
static const MidiEvent PROGMEM _evPing[] = {
  {79, 90, 0, GM_STEEL_DRUMS, 60, 0},     // G5
  {84, 100, 0, 255,           80, 80},     // C6
};

// --- Whistle: Blown bottle ascending slide ---
static const MidiEvent PROGMEM _evWhistle[] = {
  {72, 90, 0, GM_BLOWN_BOTTLE, 80, 0},    // C5
  {76, 95, 0, 255,             80, 60},    // E5
  {79, 100, 0, 255,            120, 120},  // G5
};

// --- Typing: Woodblock quick click ---
static const MidiEvent PROGMEM _evTyping[] = {
  {76, 80, 0, GM_WOODBLOCK, 20, 0},
};

// --- Rain Drop: Glockenspiel single soft note ---
static const MidiEvent PROGMEM _evRain[] = {
  {84, 50, 0, GM_GLOCKENSPIEL, 300, 0},   // C6 very soft, long ring
};

// --- Countdown Tick: Woodblock metronome ---
static const MidiEvent PROGMEM _evTick[] = {
  {PERC_WOODBLOCK, 90, 9, 255, 20, 0},
};

// --- Swipe: Celesta quick ascending glissando ---
static const MidiEvent PROGMEM _evSwipe[] = {
  {72, 60, 0, GM_CELESTA, 30, 0},     // C5
  {76, 70, 0, 255,        30, 30},    // E5
  {79, 80, 0, 255,        30, 60},    // G5
  {84, 90, 0, 255,        50, 90},    // C6
};

// --- Toggle On: Music box bright two-note up ---
static const MidiEvent PROGMEM _evToggleOn[] = {
  {67, 90, 0, GM_MUSIC_BOX, 60, 0},    // G4
  {79, 100, 0, 255,         80, 70},   // G5
};

// --- Toggle Off: Music box two-note down ---
static const MidiEvent PROGMEM _evToggleOff[] = {
  {79, 90, 0, GM_MUSIC_BOX, 60, 0},    // G5
  {67, 80, 0, 255,          80, 70},   // G4
};

// --- Dismiss: Marimba quick descending fade ---
static const MidiEvent PROGMEM _evDismiss[] = {
  {76, 80, 0, GM_MARIMBA, 40, 0},     // E5
  {72, 70, 0, 255,        40, 40},    // C5
  {67, 55, 0, 255,        50, 80},    // G4
};

// --- Confirm: Glockenspiel bright double ding ---
static const MidiEvent PROGMEM _evConfirm[] = {
  {79, 100, 0, GM_GLOCKENSPIEL, 80, 0},    // G5
  {84, 110, 0, 255,             120, 100},  // C6
};

// --- Happy Hum: Vibraphone cheerful ascending phrase ---
static const MidiEvent PROGMEM _evHappyHum[] = {
  {67, 80, 0, GM_VIBRAPHONE, 100, 0},    // G4
  {71, 85, 0, 255,           80, 100},   // B4
  {74, 90, 0, 255,           80, 180},   // D5
  {79, 95, 0, 255,           120, 260},  // G5
  {83, 100, 0, 255,          180, 340},  // B5
};

// --- Sad Sigh: Warm pad slow descending minor ---
static const MidiEvent PROGMEM _evSadSigh[] = {
  {72, 70, 0, GM_PAD_WARM, 250, 0},     // C5
  {68, 65, 0, 255,         250, 300},   // Ab4
  {65, 55, 0, 255,         300, 600},   // F4
  {63, 50, 0, 255,         400, 900},   // Eb4
};

// --- Curious Beep: Celesta question-mark melody ---
static const MidiEvent PROGMEM _evCurious[] = {
  {67, 80, 0, GM_CELESTA, 60, 0},      // G4
  {64, 75, 0, 255,        60, 80},     // E4
  {71, 90, 0, 255,        120, 160},   // B4 (up = question)
};

// --- Laugh: Xylophone bouncy descending staccato ---
static const MidiEvent PROGMEM _evLaugh[] = {
  {84, 100, 0, GM_XYLOPHONE, 30, 0},    // C6
  {81, 95, 0, 255,           30, 50},   // A5
  {79, 90, 0, 255,           30, 100},  // G5
  {76, 85, 0, 255,           30, 150},  // E5
  {74, 80, 0, 255,           30, 200},  // D5
  {72, 75, 0, 255,           40, 250},  // C5
};

// --- Yawn: Blown bottle slow rise and fall ---
static const MidiEvent PROGMEM _evYawn[] = {
  {60, 60, 0, GM_BLOWN_BOTTLE, 200, 0},    // C4
  {64, 65, 0, 255,             200, 200},  // E4
  {67, 70, 0, 255,             250, 400},  // G4 peak
  {64, 60, 0, 255,             200, 650},  // E4 falling
  {60, 50, 0, 255,             300, 850},  // C4 low
};

// ============================================================================
// Full Songs — multi-channel MIDI compositions
// ============================================================================
// Channels: 0 = melody, 1 = bass, 9 = percussion
// Songs stay within 8 simultaneous notes for MAX_ACTIVE_NOTES limit

// --- Funky Bot: Upbeat chiptune-style groove (~18s) ---
// C major pentatonic melody, walking bass, steady beat
static const MidiEvent PROGMEM _evFunkyBot[] = {
  // -- Bar 1 (0ms): Intro groove --
  {PERC_CLOSED_HH, 80, 9, 255,  20, 0},
  {48, 90, 1, GM_ELECTRIC_PIANO, 200, 0},     // C3 bass
  {72, 100, 0, GM_BRIGHT_PIANO,  150, 0},     // C5 melody
  {PERC_BASS_DRUM, 100, 9, 255, 20, 250},
  {PERC_CLOSED_HH, 70, 9, 255,  20, 250},
  {76, 100, 0, 255,             150, 250},    // E5
  {PERC_CLOSED_HH, 80, 9, 255,  20, 500},
  {PERC_SNARE, 90, 9, 255,      20, 500},
  {50, 85, 1, 255,              200, 500},    // D3 bass
  {79, 105, 0, 255,             150, 500},    // G5
  {PERC_CLOSED_HH, 70, 9, 255,  20, 750},
  {76, 95, 0, 255,              100, 750},    // E5

  // -- Bar 2 (1000ms): Melodic phrase --
  {PERC_BASS_DRUM, 100, 9, 255, 20, 1000},
  {PERC_CLOSED_HH, 80, 9, 255,  20, 1000},
  {52, 90, 1, 255,              200, 1000},   // E3 bass
  {84, 110, 0, 255,             200, 1000},   // C6 melody
  {PERC_CLOSED_HH, 70, 9, 255,  20, 1250},
  {81, 100, 0, 255,             150, 1250},   // A5
  {PERC_CLOSED_HH, 80, 9, 255,  20, 1500},
  {PERC_SNARE, 90, 9, 255,      20, 1500},
  {55, 85, 1, 255,              200, 1500},   // G3 bass
  {79, 105, 0, 255,             200, 1500},   // G5
  {PERC_CLOSED_HH, 70, 9, 255,  20, 1750},
  {76, 95, 0, 255,              100, 1750},   // E5

  // -- Bar 3 (2000ms): Funky riff --
  {PERC_BASS_DRUM, 100, 9, 255, 20, 2000},
  {PERC_CLOSED_HH, 80, 9, 255,  20, 2000},
  {48, 90, 1, 255,              200, 2000},   // C3 bass
  {72, 95, 0, 255,              80, 2000},    // C5 staccato
  {74, 100, 0, 255,             80, 2100},    // D5
  {PERC_CLOSED_HH, 70, 9, 255,  20, 2250},
  {76, 105, 0, 255,             150, 2200},   // E5
  {PERC_CLOSED_HH, 80, 9, 255,  20, 2500},
  {PERC_SNARE, 90, 9, 255,      20, 2500},
  {50, 85, 1, 255,              200, 2500},   // D3 bass
  {79, 110, 0, 255,             200, 2500},   // G5
  {PERC_CLOSED_HH, 70, 9, 255,  20, 2750},
  {81, 100, 0, 255,             100, 2750},   // A5

  // -- Bar 4 (3000ms): Climax phrase --
  {PERC_BASS_DRUM, 100, 9, 255, 20, 3000},
  {PERC_OPEN_HH, 85, 9, 255,    30, 3000},
  {55, 90, 1, 255,              200, 3000},   // G3 bass
  {84, 110, 0, 255,             200, 3000},   // C6 melody high
  {PERC_CLOSED_HH, 70, 9, 255,  20, 3250},
  {81, 105, 0, 255,             150, 3250},   // A5
  {PERC_CLOSED_HH, 80, 9, 255,  20, 3500},
  {PERC_SNARE, 95, 9, 255,      20, 3500},
  {52, 85, 1, 255,              200, 3500},   // E3 bass
  {79, 100, 0, 255,             200, 3500},   // G5
  {PERC_CLOSED_HH, 70, 9, 255,  20, 3750},
  {76, 90, 0, 255,              100, 3750},   // E5

  // -- Bar 5 (4000ms): Repeat groove variation --
  {PERC_BASS_DRUM, 100, 9, 255, 20, 4000},
  {PERC_CLOSED_HH, 80, 9, 255,  20, 4000},
  {48, 90, 1, 255,              200, 4000},   // C3
  {74, 100, 0, 255,             150, 4000},   // D5
  {PERC_CLOSED_HH, 70, 9, 255,  20, 4250},
  {76, 105, 0, 255,             100, 4250},   // E5
  {79, 110, 0, 255,             150, 4350},   // G5
  {PERC_CLOSED_HH, 80, 9, 255,  20, 4500},
  {PERC_SNARE, 90, 9, 255,      20, 4500},
  {50, 85, 1, 255,              200, 4500},   // D3
  {81, 100, 0, 255,             200, 4500},   // A5
  {PERC_CLOSED_HH, 70, 9, 255,  20, 4750},
  {84, 110, 0, 255,             100, 4750},   // C6

  // -- Bar 6 (5000ms): Bridge --
  {PERC_BASS_DRUM, 100, 9, 255, 20, 5000},
  {PERC_CLOSED_HH, 80, 9, 255,  20, 5000},
  {53, 90, 1, 255,              200, 5000},   // F3
  {77, 100, 0, 255,             200, 5000},   // F5 (new note!)
  {PERC_CLOSED_HH, 70, 9, 255,  20, 5250},
  {79, 100, 0, 255,             150, 5250},   // G5
  {PERC_CLOSED_HH, 80, 9, 255,  20, 5500},
  {PERC_SNARE, 90, 9, 255,      20, 5500},
  {55, 85, 1, 255,              200, 5500},   // G3
  {81, 105, 0, 255,             200, 5500},   // A5
  {PERC_CLOSED_HH, 70, 9, 255,  20, 5750},
  {79, 95, 0, 255,              100, 5750},   // G5

  // -- Bar 7 (6000ms): Return to hook --
  {PERC_BASS_DRUM, 100, 9, 255, 20, 6000},
  {PERC_OPEN_HH, 85, 9, 255,    30, 6000},
  {48, 90, 1, 255,              200, 6000},   // C3
  {84, 115, 0, 255,             200, 6000},   // C6
  {PERC_CLOSED_HH, 70, 9, 255,  20, 6250},
  {81, 105, 0, 255,             150, 6250},   // A5
  {PERC_CLOSED_HH, 80, 9, 255,  20, 6500},
  {PERC_SNARE, 95, 9, 255,      20, 6500},
  {52, 85, 1, 255,              200, 6500},   // E3
  {79, 100, 0, 255,             150, 6500},   // G5
  {76, 95, 0, 255,              150, 6750},   // E5

  // -- Bar 8 (7000ms): Ending --
  {PERC_BASS_DRUM, 110, 9, 255, 20, 7000},
  {PERC_SNARE, 100, 9, 255,     20, 7000},
  {48, 95, 1, 255,              300, 7000},   // C3 bass hold
  {72, 110, 0, 255,             100, 7000},   // C5
  {76, 110, 0, 255,             100, 7100},   // E5
  {79, 115, 0, 255,             100, 7200},   // G5
  {84, 120, 0, 255,             400, 7300},   // C6 final hold
  {PERC_HAND_CLAP, 100, 9, 255, 20, 7300},
};

// --- Chill Wave: Ambient vibraphone + pad (~20s) ---
// Cmaj7/Am7 progression, soft percussion, dreamy feel
static const MidiEvent PROGMEM _evChillWave[] = {
  // -- Section 1 (0ms): Cmaj7 pad entry --
  {48, 50, 1, GM_PAD_WARM, 2000, 0},          // C3 pad
  {67, 70, 0, GM_VIBRAPHONE, 400, 200},        // G4
  {PERC_TRIANGLE, 40, 9, 255,   30, 200},
  {72, 65, 0, 255,              500, 800},     // C5
  {71, 60, 0, 255,              400, 1400},    // B4
  {PERC_TRIANGLE, 35, 9, 255,   30, 1600},

  // -- Section 2 (2000ms): Am7 shift --
  {45, 50, 1, 255,             2000, 2000},    // A2 pad
  {69, 70, 0, 255,              400, 2200},    // A4
  {PERC_TRIANGLE, 40, 9, 255,   30, 2400},
  {76, 65, 0, 255,              500, 2800},    // E5
  {74, 60, 0, 255,              400, 3400},    // D5
  {PERC_CLOSED_HH, 30, 9, 255,  20, 3500},

  // -- Section 3 (4000ms): Fmaj7 --
  {41, 50, 1, 255,             2000, 4000},    // F2 pad
  {72, 70, 0, 255,              400, 4200},    // C5
  {PERC_TRIANGLE, 40, 9, 255,   30, 4200},
  {77, 65, 0, 255,              500, 4800},    // F5
  {76, 60, 0, 255,              400, 5400},    // E5
  {PERC_TRIANGLE, 35, 9, 255,   30, 5600},

  // -- Section 4 (6000ms): G7 resolve --
  {43, 50, 1, 255,             2000, 6000},    // G2 pad
  {74, 70, 0, 255,              400, 6200},    // D5
  {PERC_TRIANGLE, 40, 9, 255,   30, 6200},
  {79, 65, 0, 255,              500, 6800},    // G5
  {77, 60, 0, 255,              400, 7400},    // F5
  {PERC_CLOSED_HH, 30, 9, 255,  20, 7600},

  // -- Section 5 (8000ms): Repeat Cmaj7, higher register --
  {48, 50, 1, 255,             2000, 8000},    // C3 pad
  {79, 70, 0, 255,              400, 8200},    // G5
  {PERC_TRIANGLE, 40, 9, 255,   30, 8400},
  {84, 60, 0, 255,              500, 8800},    // C6
  {83, 55, 0, 255,              400, 9400},    // B5
  {PERC_TRIANGLE, 35, 9, 255,   30, 9400},

  // -- Section 6 (10000ms): Am7 --
  {45, 50, 1, 255,             2000, 10000},   // A2 pad
  {81, 65, 0, 255,              400, 10200},   // A5
  {PERC_TRIANGLE, 40, 9, 255,   30, 10400},
  {84, 60, 0, 255,              500, 10800},   // C6
  {79, 55, 0, 255,              500, 11400},   // G5

  // -- Section 7 (12000ms): Fmaj7 variation --
  {41, 50, 1, 255,             2000, 12000},   // F2 pad
  {77, 65, 0, 255,              500, 12200},   // F5
  {PERC_TRIANGLE, 40, 9, 255,   30, 12200},
  {76, 60, 0, 255,              400, 12800},   // E5
  {72, 55, 0, 255,              500, 13400},   // C5
  {PERC_TRIANGLE, 35, 9, 255,   30, 13600},

  // -- Section 8 (14000ms): G7 → resolve --
  {43, 50, 1, 255,             2000, 14000},   // G2 pad
  {79, 65, 0, 255,              400, 14200},   // G5
  {PERC_TRIANGLE, 40, 9, 255,   30, 14200},
  {74, 60, 0, 255,              500, 14800},   // D5
  {71, 55, 0, 255,              500, 15400},   // B4

  // -- Section 9 (16000ms): Final Cmaj7 fade --
  {48, 45, 1, 255,             3000, 16000},   // C3 pad long
  {72, 55, 0, 255,              600, 16200},   // C5
  {PERC_TRIANGLE, 35, 9, 255,   30, 16400},
  {76, 50, 0, 255,              600, 17000},   // E5
  {79, 45, 0, 255,              800, 17800},   // G5 fade
  {84, 40, 0, 255,              1000, 18600},  // C6 final ring
};

// --- Retro Quest: Game adventure theme (~16s) ---
// Em/G/C/D progression, music box melody, 8-bit feel
static const MidiEvent PROGMEM _evRetroQuest[] = {
  // -- Bar 1 (0ms): Em - Hero theme intro --
  {PERC_BASS_DRUM, 90, 9, 255,  20, 0},
  {40, 80, 1, GM_ELECTRIC_PIANO, 400, 0},      // E2 bass
  {76, 100, 0, GM_MUSIC_BOX,    150, 0},       // E5 melody
  {79, 95, 0, 255,              150, 200},     // G5
  {PERC_CLOSED_HH, 60, 9, 255,  20, 250},
  {83, 105, 0, 255,             200, 400},     // B5
  {PERC_SNARE, 80, 9, 255,      20, 500},
  {79, 90, 0, 255,              150, 600},     // G5

  // -- Bar 2 (800ms): G - Rising action --
  {PERC_BASS_DRUM, 90, 9, 255,  20, 800},
  {43, 80, 1, 255,              400, 800},     // G2 bass
  {79, 100, 0, 255,             150, 800},     // G5
  {83, 95, 0, 255,              150, 1000},    // B5
  {PERC_CLOSED_HH, 60, 9, 255,  20, 1050},
  {84, 105, 0, 255,             200, 1200},    // C6
  {PERC_SNARE, 80, 9, 255,      20, 1300},
  {86, 100, 0, 255,             200, 1400},    // D6

  // -- Bar 3 (1600ms): C - Triumphant --
  {PERC_BASS_DRUM, 90, 9, 255,  20, 1600},
  {36, 80, 1, 255,              400, 1600},    // C2 bass
  {88, 110, 0, 255,             200, 1600},    // E6 high!
  {PERC_CLOSED_HH, 60, 9, 255,  20, 1850},
  {86, 100, 0, 255,             150, 1800},    // D6
  {84, 95, 0, 255,              150, 2000},    // C6
  {PERC_SNARE, 80, 9, 255,      20, 2100},
  {83, 90, 0, 255,              200, 2200},    // B5

  // -- Bar 4 (2400ms): D - Resolution --
  {PERC_BASS_DRUM, 90, 9, 255,  20, 2400},
  {38, 80, 1, 255,              400, 2400},    // D2 bass
  {86, 105, 0, 255,             200, 2400},    // D6
  {PERC_CLOSED_HH, 60, 9, 255,  20, 2650},
  {84, 100, 0, 255,             200, 2600},    // C6
  {83, 95, 0, 255,              200, 2800},    // B5
  {PERC_SNARE, 80, 9, 255,      20, 2900},
  {79, 90, 0, 255,              300, 3000},    // G5 hold

  // -- Bar 5 (3200ms): Em - Second verse lower --
  {PERC_BASS_DRUM, 90, 9, 255,  20, 3200},
  {PERC_COWBELL, 60, 9, 255,    20, 3200},
  {40, 80, 1, 255,              400, 3200},    // E2
  {64, 95, 0, 255,              150, 3200},    // E4
  {67, 90, 0, 255,              150, 3400},    // G4
  {PERC_CLOSED_HH, 60, 9, 255,  20, 3450},
  {71, 100, 0, 255,             200, 3600},    // B4
  {PERC_SNARE, 80, 9, 255,      20, 3700},
  {74, 95, 0, 255,              150, 3800},    // D5

  // -- Bar 6 (4000ms): G - Build up --
  {PERC_BASS_DRUM, 90, 9, 255,  20, 4000},
  {43, 80, 1, 255,              400, 4000},    // G2
  {76, 100, 0, 255,             150, 4000},    // E5
  {79, 100, 0, 255,             150, 4200},    // G5
  {PERC_CLOSED_HH, 60, 9, 255,  20, 4250},
  {83, 105, 0, 255,             200, 4400},    // B5
  {PERC_SNARE, 80, 9, 255,      20, 4500},
  {84, 110, 0, 255,             200, 4600},    // C6

  // -- Bar 7 (4800ms): Am - Dramatic turn --
  {PERC_BASS_DRUM, 95, 9, 255,  20, 4800},
  {33, 80, 1, 255,              400, 4800},    // A1 bass
  {84, 105, 0, 255,             200, 4800},    // C6
  {81, 100, 0, 255,             150, 5000},    // A5
  {PERC_CLOSED_HH, 60, 9, 255,  20, 5050},
  {76, 95, 0, 255,              200, 5200},    // E5
  {PERC_SNARE, 85, 9, 255,      20, 5300},
  {81, 100, 0, 255,             200, 5400},    // A5

  // -- Bar 8 (5600ms): B7 - Tension --
  {PERC_BASS_DRUM, 95, 9, 255,  20, 5600},
  {35, 80, 1, 255,              400, 5600},    // B1 bass
  {83, 110, 0, 255,             100, 5600},    // B5 quick
  {80, 105, 0, 255,             100, 5700},    // Ab5
  {83, 110, 0, 255,             100, 5800},    // B5
  {PERC_SNARE, 85, 9, 255,      20, 5900},
  {86, 115, 0, 255,             300, 5900},    // D6 dramatic

  // -- Bar 9 (6400ms): Em - Final chorus --
  {PERC_BASS_DRUM, 100, 9, 255, 20, 6400},
  {PERC_OPEN_HH, 75, 9, 255,   30, 6400},
  {40, 85, 1, 255,              400, 6400},    // E2
  {88, 115, 0, 255,             200, 6400},    // E6 high
  {PERC_CLOSED_HH, 60, 9, 255,  20, 6650},
  {86, 110, 0, 255,             150, 6600},    // D6
  {84, 105, 0, 255,             200, 6800},    // C6
  {PERC_SNARE, 90, 9, 255,      20, 6900},
  {83, 100, 0, 255,             200, 7000},    // B5

  // -- Bar 10 (7200ms): G → ending --
  {PERC_BASS_DRUM, 100, 9, 255, 20, 7200},
  {43, 85, 1, 255,              400, 7200},    // G2
  {79, 100, 0, 255,             150, 7200},    // G5
  {83, 105, 0, 255,             200, 7400},    // B5
  {PERC_SNARE, 90, 9, 255,      20, 7500},
  {86, 110, 0, 255,             300, 7600},    // D6

  // -- Ending (7800ms): Final Em chord --
  {PERC_BASS_DRUM, 110, 9, 255, 20, 7800},
  {PERC_HAND_CLAP, 90, 9, 255,  20, 7800},
  {40, 90, 1, 255,              500, 7800},    // E2 bass hold
  {76, 110, 0, 255,             500, 7800},    // E5 chord
  {79, 110, 0, 255,             500, 7800},    // G5 chord
  {83, 115, 0, 255,             600, 7800},    // B5 chord ring
};

// --- Sequence Lookup Table ---

static const MidiSequenceDef builtinSequences[] = {
  // SEQ_NONE (0)
  { "None",           0, 0,               0, nullptr },
  // Original 9
  { "Boot Chime",     3, GM_VIBRAPHONE,   0, _evBoot },
  { "Tap Boop",       2, GM_MARIMBA,      0, _evTap },
  { "Shake Rattle",   7, GM_AGOGO,        SEQ_FLAG_PERCUSSION, _evShake },
  { "Wake Chime",     4, GM_VIBRAPHONE,   0, _evWake },
  { "Sleep Descend",  4, GM_PAD_WARM,     0, _evSleep },
  { "Bubble Pop",     2, GM_BLOWN_BOTTLE, 0, _evBubble },
  { "Expr Chirp",     1, GM_CELESTA,      0, _evChirp },
  { "Notification",   2, GM_CELESTA,      0, _evNotify },
  { "Clap React",     3, GM_MARIMBA,      SEQ_FLAG_PERCUSSION, _evClap },
  // New sequences (10-24)
  { "Success",        3, GM_MUSIC_BOX,    0, _evSuccess },
  { "Error",          2, GM_ELECTRIC_PIANO, 0, _evError },
  { "Level Up",       5, GM_BRIGHT_PIANO, SEQ_FLAG_PERCUSSION, _evLevelUp },
  { "Coin",           2, GM_XYLOPHONE,    0, _evCoin },
  { "Ambient Chime",  2, GM_TUBULAR_BELLS,0, _evAmbient },
  { "Heartbeat",      2, GM_PIANO,        0, _evHeartbeat },
  { "Mystery",        4, GM_CELESTA,      0, _evMystery },
  { "Fanfare",        6, GM_BRIGHT_PIANO, SEQ_FLAG_PERCUSSION, _evFanfare },
  { "Power Up",       6, GM_GLOCKENSPIEL, 0, _evPowerUp },
  { "Power Down",     5, GM_GLOCKENSPIEL, 0, _evPowerDown },
  { "Ping",           2, GM_STEEL_DRUMS,  0, _evPing },
  { "Whistle",        3, GM_BLOWN_BOTTLE, 0, _evWhistle },
  { "Typing",         1, GM_WOODBLOCK,    0, _evTyping },
  { "Rain Drop",      1, GM_GLOCKENSPIEL, 0, _evRain },
  { "Countdown",      1, 0,               SEQ_FLAG_PERCUSSION, _evTick },
  // Additional SFX (25-34)
  { "Swipe",          4, GM_CELESTA,      0, _evSwipe },
  { "Toggle On",      2, GM_MUSIC_BOX,    0, _evToggleOn },
  { "Toggle Off",     2, GM_MUSIC_BOX,    0, _evToggleOff },
  { "Dismiss",        3, GM_MARIMBA,      0, _evDismiss },
  { "Confirm",        2, GM_GLOCKENSPIEL, 0, _evConfirm },
  { "Happy Hum",      5, GM_VIBRAPHONE,   0, _evHappyHum },
  { "Sad Sigh",       4, GM_PAD_WARM,     0, _evSadSigh },
  { "Curious Beep",   3, GM_CELESTA,      0, _evCurious },
  { "Laugh",          6, GM_XYLOPHONE,    0, _evLaugh },
  { "Yawn",           5, GM_BLOWN_BOTTLE, 0, _evYawn },
  // Full songs (35-37)
  { "Funky Bot",      sizeof(_evFunkyBot)/sizeof(MidiEvent),   GM_BRIGHT_PIANO, SEQ_FLAG_PERCUSSION, _evFunkyBot },
  { "Chill Wave",     sizeof(_evChillWave)/sizeof(MidiEvent),  GM_VIBRAPHONE,   SEQ_FLAG_PERCUSSION, _evChillWave },
  { "Retro Quest",    sizeof(_evRetroQuest)/sizeof(MidiEvent), GM_MUSIC_BOX,    SEQ_FLAG_PERCUSSION, _evRetroQuest },
};

#define BUILTIN_SEQ_COUNT (sizeof(builtinSequences) / sizeof(builtinSequences[0]))

// ============================================================================
// Cloud Sequence Runtime Storage
// ============================================================================

#define MAX_CLOUD_SEQUENCES 20

MidiEvent  cloudSeqEvents[MAX_CLOUD_SEQUENCES][SEQ_MAX_EVENTS];
MidiSequenceDef cloudSequences[MAX_CLOUD_SEQUENCES];
uint8_t cloudSequenceCount = 0;

// ============================================================================
// Sine-wave fallback (M5.Speaker) — used when MIDI synth not available
// ============================================================================

#define SINE_BUF_MAX 200

static int16_t _sineBuf[SINE_BUF_MAX];

static void _playSine(uint16_t freq, uint16_t durationMs) {
  if (freq == 0) return;
  uint16_t samplesPerCycle = 48000 / freq;
  if (samplesPerCycle > SINE_BUF_MAX) samplesPerCycle = SINE_BUF_MAX;

  for (uint16_t i = 0; i < samplesPerCycle; i++) {
    _sineBuf[i] = (int16_t)(20000.0f * sinf(2.0f * M_PI * i / samplesPerCycle));
  }

  uint8_t repeats = constrain((uint32_t)freq * durationMs / 1000, 1, 255);
  M5.Speaker.playRaw(_sineBuf, samplesPerCycle, 48000, false, repeats);
}

// Convert MIDI note number to frequency (A4=440Hz)
static uint16_t midiNoteToFreq(uint8_t note) {
  if (note == 0) return 0;
  return (uint16_t)(440.0f * powf(2.0f, (note - 69) / 12.0f));
}

// ============================================================================
// Active Note Tracking (for proper note-off)
// ============================================================================

#define MAX_ACTIVE_NOTES 8

struct ActiveNote {
  uint8_t  channel;
  uint8_t  note;
  unsigned long offMs;  // millis() when note should be turned off
  bool     active;
};

// ============================================================================
// BotSounds — Global Sequencer
// ============================================================================

struct BotSounds {
  bool enabled;
  bool useMidi;          // true = SAM2695, false = M5.Speaker fallback
  uint8_t volume;        // 0-255 (firmware scale)
  bool playing;          // true while sequence active (mic mutes on this)

  // Playback state
  MidiSequenceId currentSeqId;
  const MidiSequenceDef* currentDef;
  uint8_t  eventIndex;       // Next event to fire
  unsigned long seqStartMs;  // millis() when sequence started
  unsigned long seqEndMs;    // millis() when last note-off finishes

  // Active note tracking for MIDI note-off
  ActiveNote activeNotes[MAX_ACTIVE_NOTES];

  // For fallback single-tone playback (web API freq+dur)
  bool singleTone;
  unsigned long singleToneEndMs;

  void init() {
    enabled = true;
    volume = 120;
    playing = false;
    currentSeqId = SEQ_NONE;
    currentDef = nullptr;
    eventIndex = 0;
    seqStartMs = 0;
    seqEndMs = 0;
    singleTone = false;
    singleToneEndMs = 0;

    for (uint8_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
      activeNotes[i].active = false;
    }

    // Try MIDI synth first
    useMidi = false;
#ifdef MIDI_SYNTH_ENABLED
    if (midiSynth.ready) {
      useMidi = true;
      midiSynth.setVolume(volume);
      DBGLN("BotSounds: MIDI synth mode");
    } else {
      DBGLN("BotSounds: MIDI synth not ready, using speaker fallback");
    }
#endif

    if (!useMidi) {
      auto cfg = M5.Speaker.config();
      cfg.sample_rate = 48000;
      M5.Speaker.config(cfg);
      M5.Speaker.begin();
      M5.Speaker.setVolume(volume);
      DBGLN("BotSounds: M5.Speaker fallback mode");
    }
  }

  void setVolume(uint8_t vol) {
    volume = vol;
    if (useMidi) {
#ifdef MIDI_SYNTH_ENABLED
      midiSynth.setVolume(vol);  // setVolume maps 0-255 to 0-127 internally
#endif
    } else {
      M5.Speaker.setVolume(vol);
    }
  }

  // --- Play a built-in or cloud sequence by ID ---

  void play(MidiSequenceId seqId) {
    if (!enabled || seqId == SEQ_NONE) return;

    const MidiSequenceDef* def = lookupSequence(seqId);
    if (!def || def->eventCount == 0) return;

    playDef(def, seqId);
  }

  // --- Play an arbitrary sequence definition (for cloud sequences) ---

  void playDef(const MidiSequenceDef* def, MidiSequenceId id = SEQ_NONE) {
    if (!enabled || !def || def->eventCount == 0) return;

    // Stop current playback
    stop();

    currentSeqId = id;
    currentDef = def;
    eventIndex = 0;
    singleTone = false;
    playing = true;
    seqStartMs = millis();

    // Calculate sequence end time (latest note-off)
    uint16_t maxEnd = 0;
    for (uint8_t i = 0; i < def->eventCount; i++) {
      MidiEvent ev;
      memcpy_P(&ev, &def->events[i], sizeof(MidiEvent));
      uint16_t end = ev.offsetMs + ev.durationMs;
      if (end > maxEnd) maxEnd = end;
    }
    seqEndMs = seqStartMs + maxEnd + 50;  // 50ms grace

    // Set program for default channel
    if (useMidi) {
#ifdef MIDI_SYNTH_ENABLED
      midiSynth.programChange(0, def->defaultProgram);
      delay(15);  // SAM2695 needs time after instrument change before first note
#endif
    }
  }

  // --- Play a single arbitrary tone (legacy web API: freq + duration) ---

  void playTone(uint16_t freq, uint16_t durationMs) {
    if (!enabled) return;
    stop();

    singleTone = true;
    playing = true;
    singleToneEndMs = millis() + durationMs;

    if (useMidi) {
#ifdef MIDI_SYNTH_ENABLED
      // Convert freq to nearest MIDI note
      uint8_t note = 69 + (uint8_t)(12.0f * log2f(freq / 440.0f) + 0.5f);
      note = constrain(note, 0, 127);
      midiSynth.programChange(0, GM_VIBRAPHONE);
      midiSynth.noteOn(0, note, 100);
      addActiveNote(0, note, singleToneEndMs);
#endif
    } else {
      _playSine(freq, durationMs);
    }
  }

  // --- Stop all playback ---

  void stop() {
    if (useMidi) {
#ifdef MIDI_SYNTH_ENABLED
      // Turn off all tracked notes
      for (uint8_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
        if (activeNotes[i].active) {
          midiSynth.noteOff(activeNotes[i].channel, activeNotes[i].note);
          activeNotes[i].active = false;
        }
      }
#endif
    } else {
      M5.Speaker.stop();
    }

    playing = false;
    currentSeqId = SEQ_NONE;
    currentDef = nullptr;
    eventIndex = 0;
    singleTone = false;
  }

  // --- Non-blocking update — call each frame ---

  void update() {
    if (!playing) return;

    unsigned long now = millis();

    // Handle single-tone playback
    if (singleTone) {
      // Check note-offs for MIDI
      if (useMidi) updateNoteOffs(now);
      if (now >= singleToneEndMs) {
        playing = false;
        singleTone = false;
      }
      return;
    }

    // Handle sequence playback
    if (!currentDef) {
      playing = false;
      return;
    }

    // Fire events whose offsetMs has been reached
    while (eventIndex < currentDef->eventCount) {
      MidiEvent ev;
      memcpy_P(&ev, &currentDef->events[eventIndex], sizeof(MidiEvent));

      unsigned long eventFireMs = seqStartMs + ev.offsetMs;
      if (now < eventFireMs) break;  // Not time yet

      // Fire this event
      fireEvent(ev, now);
      eventIndex++;
    }

    // Handle note-offs for MIDI
    if (useMidi) updateNoteOffs(now);

    // Check if sequence is complete
    if (now >= seqEndMs) {
      if (currentDef->flags & SEQ_FLAG_LOOP) {
        // Restart
        eventIndex = 0;
        seqStartMs = now;
        uint16_t maxEnd = 0;
        for (uint8_t i = 0; i < currentDef->eventCount; i++) {
          MidiEvent e;
          memcpy_P(&e, &currentDef->events[i], sizeof(MidiEvent));
          uint16_t end = e.offsetMs + e.durationMs;
          if (end > maxEnd) maxEnd = end;
        }
        seqEndMs = seqStartMs + maxEnd + 50;
      } else {
        playing = false;
        currentSeqId = SEQ_NONE;
        currentDef = nullptr;
      }
    }
  }

private:

  const MidiSequenceDef* lookupSequence(MidiSequenceId id) {
    if (id < BUILTIN_SEQ_COUNT) {
      return &builtinSequences[id];
    }
    // Cloud sequences
    if (id >= SEQ_CLOUD_BASE) {
      uint8_t cloudIdx = id - SEQ_CLOUD_BASE;
      if (cloudIdx < cloudSequenceCount) {
        return &cloudSequences[cloudIdx];
      }
    }
    return nullptr;
  }

  void fireEvent(const MidiEvent& ev, unsigned long now) {
    if (ev.note == 0 && ev.durationMs == 0) return;  // Terminator

    if (useMidi) {
#ifdef MIDI_SYNTH_ENABLED
      // Set program if event overrides
      uint8_t ch = ev.channel;
      if (ev.program != 255 && ch != MIDI_CH_PERCUSSION) {
        midiSynth.programChange(ch, ev.program);
      }
      midiSynth.noteOn(ch, ev.note, ev.velocity);
      addActiveNote(ch, ev.note, now + ev.durationMs);
#endif
    } else {
      // Fallback: play as sine-wave tone (ignore channel/program)
      if (ev.channel != MIDI_CH_PERCUSSION) {
        uint16_t freq = midiNoteToFreq(ev.note);
        _playSine(freq, ev.durationMs);
      }
      // Percussion events are skipped in fallback mode
    }
  }

  void addActiveNote(uint8_t ch, uint8_t note, unsigned long offMs) {
    // Find an empty slot
    for (uint8_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
      if (!activeNotes[i].active) {
        activeNotes[i].channel = ch;
        activeNotes[i].note = note;
        activeNotes[i].offMs = offMs;
        activeNotes[i].active = true;
        return;
      }
    }
    // All slots full — steal the earliest one
    uint8_t earliest = 0;
    unsigned long earliestMs = ULONG_MAX;
    for (uint8_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
      if (activeNotes[i].offMs < earliestMs) {
        earliestMs = activeNotes[i].offMs;
        earliest = i;
      }
    }
#ifdef MIDI_SYNTH_ENABLED
    midiSynth.noteOff(activeNotes[earliest].channel, activeNotes[earliest].note);
#endif
    activeNotes[earliest].channel = ch;
    activeNotes[earliest].note = note;
    activeNotes[earliest].offMs = offMs;
    activeNotes[earliest].active = true;
  }

  void updateNoteOffs(unsigned long now) {
#ifdef MIDI_SYNTH_ENABLED
    for (uint8_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
      if (activeNotes[i].active && now >= activeNotes[i].offMs) {
        midiSynth.noteOff(activeNotes[i].channel, activeNotes[i].note);
        activeNotes[i].active = false;
      }
    }
#endif
  }
};

// Global instance
BotSounds botSounds;

#endif // TARGET_CORES3
#endif // BOT_SOUNDS_H
