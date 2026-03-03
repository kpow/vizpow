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

// Sound effect definitions — each is an array of up to 4 steps
// Terminated early by a step with freq=0 AND durationMs=0
static const ToneStep sfxSequences[][SFX_MAX_STEPS] = {
  // SFX_NONE (index 0) — placeholder
  { {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_BOOT_CHIME — C5 -> E5 -> G5 ascending triad
  { {523, 80, 30}, {659, 80, 30}, {784, 120, 0}, {0, 0, 0} },

  // SFX_TAP_BOOP — quick boop
  { {880, 40, 20}, {440, 60, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_SHAKE_RATTLE — rapid descending notes
  { {1200, 40, 10}, {900, 40, 10}, {600, 40, 10}, {300, 60, 0} },

  // SFX_WAKE_CHIME — gentle ascending
  { {440, 100, 40}, {554, 100, 40}, {659, 150, 0}, {0, 0, 0} },

  // SFX_SLEEP_DESCEND — slow descending lullaby
  { {659, 120, 60}, {554, 120, 60}, {440, 150, 80}, {330, 200, 0} },

  // SFX_BUBBLE_POP — short pop
  { {1400, 25, 10}, {700, 35, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_EXPR_CHIRP — quick chirp
  { {1000, 30, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_NOTIFICATION — two-tone
  { {880, 80, 40}, {1100, 100, 0}, {0, 0, 0}, {0, 0, 0} },

  // SFX_CLAP_REACT — sharp reaction
  { {1500, 30, 10}, {800, 50, 0}, {0, 0, 0}, {0, 0, 0} },
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
    volume = 200;
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
    M5.Speaker.tone(freq, durationMs);
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
      M5.Speaker.tone(step.freq, step.durationMs);
    }
    stepEndMs = millis() + step.durationMs;
  }
};

// Global instance
BotSounds botSounds;

#endif // TARGET_CORES3
#endif // BOT_SOUNDS_H
