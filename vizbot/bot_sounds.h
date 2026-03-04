#ifndef BOT_SOUNDS_H
#define BOT_SOUNDS_H

#ifdef TARGET_CORES3

#include <Arduino.h>
#include <M5Unified.h>

// ============================================================================
// Bot Sounds — Non-blocking Multi-step Tone Sequencer
// ============================================================================
// Drives Core S3 speaker (AW88298 I2S 1W) via M5.Speaker.
// Each sound effect is a sequence of {freq, durationMs, pauseMs} steps.
// update() advances through steps without blocking the main loop.
// ============================================================================

// Sound effect identifiers
enum BotSoundEffect : uint8_t {
  SFX_NONE = 0,
  SFX_BOOT_CHIME,      // C5 -> E5 -> G5 ascending triad
  SFX_TAP_BOOP,        // Quick boop
  SFX_SHAKE_RATTLE,    // Rapid descending notes
  SFX_WAKE_CHIME,      // Gentle ascending wake tone
  SFX_SLEEP_DESCEND,   // Slow descending lullaby
  SFX_BUBBLE_POP,      // Short pop for speech bubbles
  SFX_EXPR_CHIRP,      // Quick chirp on expression change
  SFX_NOTIFICATION,    // Two-tone notification
  SFX_CLAP_REACT       // Sharp reaction to loud sound
};

// A single step in a tone sequence
struct ToneStep {
  uint16_t freq;        // Frequency in Hz (0 = silence/rest)
  uint16_t durationMs;  // How long to play
  uint16_t pauseMs;     // Pause after this step before next
};

// Maximum steps per sound effect
#define SFX_MAX_STEPS 4

// Sine-wave tone generation — eliminates harsh square-wave harmonics
// One cycle is generated and looped via playRaw repeat parameter
#define SINE_BUF_MAX 200  // enough for ~240Hz at 48kHz sample rate

static int16_t _sineBuf[SINE_BUF_MAX];

static void _playSine(uint16_t freq, uint16_t durationMs) {
  uint16_t samplesPerCycle = 48000 / freq;
  if (samplesPerCycle > SINE_BUF_MAX) samplesPerCycle = SINE_BUF_MAX;

  for (uint16_t i = 0; i < samplesPerCycle; i++) {
    _sineBuf[i] = (int16_t)(20000.0f * sinf(2.0f * M_PI * i / samplesPerCycle));
  }

  uint8_t repeats = constrain((uint32_t)freq * durationMs / 1000, 1, 255);
  M5.Speaker.playRaw(_sineBuf, samplesPerCycle, 48000, false, repeats);
}

// Sound effect definitions — each is an array of up to 4 steps
// Terminated early by a step with freq=0 AND durationMs=0
// Design: soft & round — lower frequencies, longer durations, gentle intervals
static const ToneStep sfxSequences[][SFX_MAX_STEPS] = {
  // SFX_NONE (index 0) — placeholder
  { {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_BOOT_CHIME — G4 -> B4 -> D5 gentle ascending (major triad, warm register)
  { {392, 120, 50}, {494, 120, 50}, {587, 180, 0}, {0, 0, 0} },

  // SFX_TAP_BOOP — soft round boop (low drop)
  { {494, 60, 15}, {330, 80, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_SHAKE_RATTLE — gentle tumbling descent
  { {587, 60, 20}, {494, 60, 20}, {392, 60, 20}, {294, 80, 0} },

  // SFX_WAKE_CHIME — soft pentatonic rise (D4 -> E4 -> G4)
  { {294, 140, 60}, {330, 140, 60}, {392, 200, 0}, {0, 0, 0} },

  // SFX_SLEEP_DESCEND — slow gentle lullaby (G4 -> E4 -> D4 -> B3)
  { {392, 160, 80}, {330, 160, 80}, {294, 180, 100}, {247, 250, 0} },

  // SFX_BUBBLE_POP — soft pop (rounded, not harsh)
  { {660, 35, 10}, {440, 45, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_EXPR_CHIRP — gentle blip
  { {494, 45, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_NOTIFICATION — warm two-tone (D5 -> G5)
  { {587, 100, 50}, {784, 130, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_CLAP_REACT — surprised bloop (round, not sharp)
  { {587, 50, 15}, {440, 70, 0}, {0, 0, 0}, {0, 0, 0} },
};

// ============================================================================
// BotSounds — Global Sequencer State
// ============================================================================

struct BotSounds {
  bool enabled;          // Master enable (can be toggled via settings)
  uint8_t volume;        // 0-255 volume level
  bool playing;          // True while a sequence is active (used by mic to mute)

  // Current playback state
  BotSoundEffect currentSfx;
  uint8_t stepIndex;     // Current step in sequence
  unsigned long stepEndMs;  // When current step's tone ends
  unsigned long pauseEndMs; // When current step's pause ends
  bool inPause;          // In the pause between steps

  void init() {
    enabled = true;
    volume = 120;
    playing = false;
    currentSfx = SFX_NONE;
    stepIndex = 0;
    stepEndMs = 0;
    pauseEndMs = 0;
    inPause = false;

    // Configure M5 speaker
    auto cfg = M5.Speaker.config();
    cfg.sample_rate = 48000;
    M5.Speaker.config(cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(volume);
  }

  void setVolume(uint8_t vol) {
    volume = vol;
    M5.Speaker.setVolume(vol);
  }

  // Play a predefined sound effect
  void play(BotSoundEffect sfx) {
    if (!enabled || sfx == SFX_NONE) return;
    if (sfx > SFX_CLAP_REACT) return;

    currentSfx = sfx;
    stepIndex = 0;
    inPause = false;
    playing = true;

    // Start first step immediately
    startStep();
  }

  // Play a single arbitrary tone (for web API)
  void playTone(uint16_t freq, uint16_t durationMs) {
    if (!enabled) return;
    _playSine(freq, durationMs);
    playing = true;
    currentSfx = SFX_NONE;
    stepEndMs = millis() + durationMs;
    pauseEndMs = stepEndMs;
    inPause = false;
  }

  // Advance the sequencer — call each frame
  void update() {
    if (!playing) return;

    unsigned long now = millis();

    // Single-tone playback (from playTone)
    if (currentSfx == SFX_NONE) {
      if (now >= stepEndMs) {
        playing = false;
      }
      return;
    }

    // Sequence playback
    if (inPause) {
      if (now >= pauseEndMs) {
        inPause = false;
        stepIndex++;
        if (stepIndex >= SFX_MAX_STEPS) {
          playing = false;
          currentSfx = SFX_NONE;
          return;
        }
        // Check if next step is a terminator
        const ToneStep& step = sfxSequences[currentSfx][stepIndex];
        if (step.freq == 0 && step.durationMs == 0) {
          playing = false;
          currentSfx = SFX_NONE;
          return;
        }
        startStep();
      }
    } else {
      if (now >= stepEndMs) {
        const ToneStep& step = sfxSequences[currentSfx][stepIndex];
        if (step.pauseMs > 0) {
          inPause = true;
          pauseEndMs = now + step.pauseMs;
          M5.Speaker.stop();
        } else {
          // No pause — advance immediately
          stepIndex++;
          if (stepIndex >= SFX_MAX_STEPS) {
            playing = false;
            currentSfx = SFX_NONE;
            M5.Speaker.stop();
            return;
          }
          const ToneStep& next = sfxSequences[currentSfx][stepIndex];
          if (next.freq == 0 && next.durationMs == 0) {
            playing = false;
            currentSfx = SFX_NONE;
            M5.Speaker.stop();
            return;
          }
          startStep();
        }
      }
    }
  }

private:
  void startStep() {
    const ToneStep& step = sfxSequences[currentSfx][stepIndex];
    if (step.freq > 0) {
      _playSine(step.freq, step.durationMs);
    }
    stepEndMs = millis() + step.durationMs;
  }
};

// Global instance
BotSounds botSounds;

#endif // TARGET_CORES3
#endif // BOT_SOUNDS_H
