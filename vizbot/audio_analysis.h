#ifndef AUDIO_ANALYSIS_H
#define AUDIO_ANALYSIS_H

#ifdef TARGET_CORES3

#include <Arduino.h>
#include <M5Unified.h>

// ============================================================================
// Audio Analysis — Core S3 Dual MEMS Mic (ES7210 I2S)
// ============================================================================
// Non-blocking RMS analysis of microphone input. Computes smoothed level,
// detects spikes (claps/bangs), sustained speech, and extended silence.
// Runs at ~20Hz (50ms rate-limited) to keep CPU impact minimal.
//
// Mutes input while speaker is active to avoid feedback loop.
// ============================================================================

// Forward declaration — speaker state check
extern struct BotSounds botSounds;

// Analysis configuration
#define AUDIO_SAMPLE_COUNT    256    // Samples per analysis frame (512B at 16-bit)
#define AUDIO_UPDATE_MS       50     // Rate limit: analyze every 50ms (~20Hz)
#define AUDIO_SPIKE_MULT      5.0f   // RMS must exceed movingAvg by this factor (high to ignore keyboard/taps)
#define AUDIO_SPEECH_FLOOR    600.0f // Minimum RMS to count as speech (above keyboard/ambient noise)
#define AUDIO_SPEECH_HOLD_MS  2000   // Sustained mid-level RMS for speech detection
#define AUDIO_SILENCE_MS      30000  // No sound above threshold for extended silence
#define AUDIO_MOVING_AVG_ALPHA 0.05f // Smoothing factor for moving average (lower = slower)
#define AUDIO_SMOOTH_ALPHA    0.3f   // Smoothing factor for display level

struct AudioAnalysis {
  // Sample buffer — internal SRAM for DMA compatibility
  int16_t sampleBuffer[AUDIO_SAMPLE_COUNT];

  // Computed levels
  float rmsLevel;        // Raw RMS of current frame
  float smoothLevel;     // Exponentially smoothed RMS (for display)
  float peakLevel;       // Peak RMS seen (decays slowly)
  float movingAvgRMS;    // Long-term moving average (for spike baseline)

  // Event flags (reset each frame, read by bot_mode.h)
  bool spikeDetected;    // Sudden loud sound (clap, bang)
  bool speechDetected;   // Sustained mid-level sound (someone talking)
  bool silenceExtended;  // No sound above threshold for >30 seconds

  // Internal state
  bool enabled;
  unsigned long lastUpdateMs;
  unsigned long speechStartMs;     // When sustained speech began
  bool speechActive;               // Currently detecting speech
  unsigned long lastSoundMs;       // Last time RMS was above silence threshold
  unsigned long lastSpikeMs;       // Debounce: last spike detection time

  void init() {
    enabled = true;
    rmsLevel = 0;
    smoothLevel = 0;
    peakLevel = 0;
    movingAvgRMS = 500.0f;  // Initial baseline (above noise floor)
    spikeDetected = false;
    speechDetected = false;
    silenceExtended = false;
    lastUpdateMs = 0;
    speechStartMs = 0;
    speechActive = false;
    lastSoundMs = millis();
    lastSpikeMs = 0;

    // Configure M5 mic
    auto cfg = M5.Mic.config();
    cfg.sample_rate = 16000;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    M5.Mic.config(cfg);
    M5.Mic.begin();
  }

  // Call each frame — rate-limited internally
  void update() {
    if (!enabled) return;

    unsigned long now = millis();
    if (now - lastUpdateMs < AUDIO_UPDATE_MS) return;
    lastUpdateMs = now;

    // Reset event flags each analysis frame
    spikeDetected = false;
    speechDetected = false;
    silenceExtended = false;

    // Mute while speaker is playing to avoid feedback
    if (botSounds.playing) return;

    // Read mic samples
    if (!M5.Mic.record(sampleBuffer, AUDIO_SAMPLE_COUNT, 16000)) return;

    // Compute RMS
    int64_t sum = 0;
    for (int i = 0; i < AUDIO_SAMPLE_COUNT; i++) {
      int32_t s = sampleBuffer[i];
      sum += s * s;
    }
    rmsLevel = sqrtf((float)sum / AUDIO_SAMPLE_COUNT);

    // Smooth level (for display/animation)
    smoothLevel = smoothLevel * (1.0f - AUDIO_SMOOTH_ALPHA) + rmsLevel * AUDIO_SMOOTH_ALPHA;

    // Peak level (slow decay)
    if (rmsLevel > peakLevel) {
      peakLevel = rmsLevel;
    } else {
      peakLevel *= 0.995f;
    }

    // Update moving average
    movingAvgRMS = movingAvgRMS * (1.0f - AUDIO_MOVING_AVG_ALPHA) +
                   rmsLevel * AUDIO_MOVING_AVG_ALPHA;

    // --- Spike detection (clap/bang) ---
    // RMS must exceed moving average by SPIKE_MULT AND be above absolute floor
    // High thresholds to ignore keyboard clicks, taps, and ambient transients
    if (rmsLevel > movingAvgRMS * AUDIO_SPIKE_MULT &&
        rmsLevel > AUDIO_SPEECH_FLOOR * 3.0f &&
        now - lastSpikeMs > 3000) {  // 3s debounce between spikes
      spikeDetected = true;
      lastSpikeMs = now;
    }

    // --- Speech detection (sustained mid-level sound) ---
    if (rmsLevel > AUDIO_SPEECH_FLOOR && rmsLevel < movingAvgRMS * AUDIO_SPIKE_MULT) {
      if (!speechActive) {
        speechStartMs = now;
        speechActive = true;
      }
      if (now - speechStartMs >= AUDIO_SPEECH_HOLD_MS) {
        speechDetected = true;
      }
    } else {
      speechActive = false;
    }

    // --- Extended silence detection ---
    if (rmsLevel > AUDIO_SPEECH_FLOOR * 0.5f) {
      lastSoundMs = now;
    }
    if (now - lastSoundMs >= AUDIO_SILENCE_MS) {
      silenceExtended = true;
    }
  }

  // Get normalized level (0.0 - 1.0) for display/animation
  float getNormalizedLevel() const {
    if (peakLevel < 1.0f) return 0.0f;
    float norm = smoothLevel / max(peakLevel, 1.0f);
    return constrain(norm, 0.0f, 1.0f);
  }
};

// Global instance
AudioAnalysis audioAnalysis;

#endif // TARGET_CORES3
#endif // AUDIO_ANALYSIS_H
