#ifndef AUDIO_SPECTRUM_H
#define AUDIO_SPECTRUM_H

#ifdef TARGET_CORES3

#include <Arduino.h>
#include <M5Unified.h>
#include <arduinoFFT.h>

// ============================================================================
// Audio Spectrum Analyzer — FFT-based frequency band analysis + beat detection
// ============================================================================
// Ported from noodle-v2 audio satellite (audio_task.cpp + beat_detect.cpp),
// adapted for M5Unified mic API on Core S3's ES7210 dual MEMS codec.
//
// Produces bass/mid/treble/rms/beatEnv values (0.0-1.0) consumed by the
// EffectCtx audio fields in effects_ambient.h. Runs in the main loop at ~30Hz.
//
// When enabled, AudioSpectrum owns the mic exclusively.
// ============================================================================

// Forward declaration — speaker state check for muting
extern struct BotSounds botSounds;

// --- FFT Configuration ---
#define FFT_SIZE          512
#define FFT_SAMPLE_RATE   16000
#define FFT_BIN_HZ        (FFT_SAMPLE_RATE / FFT_SIZE)  // ~31.25 Hz/bin

// Band boundaries in FFT bins (16kHz Fs, 512-pt FFT, ~31.25 Hz/bin)
#define BAND_BASS_LO      2     // ~62 Hz
#define BAND_BASS_HI      8     // ~250 Hz
#define BAND_MID_LO       8     // ~250 Hz
#define BAND_MID_HI       64    // ~2000 Hz
#define BAND_TREBLE_LO    64    // ~2000 Hz
#define BAND_TREBLE_HI    256   // ~8000 Hz (Nyquist)

// Gain scale. ES7210 outputs 16-bit with 20dB gain + 2× software magnification.
// Noodle-v2 used 0.00002 for 24-bit INMP441. Start higher for 16-bit, tune on HW.
#define SPECTRUM_SCALE    0.0001f

// Update rate
#define SPECTRUM_UPDATE_MS  33   // ~30Hz

// --- Beat Detection (spectral flux + bass-leaning gate) ---
// Ported from noodle-v2 beat_detect.cpp
#define BEAT_FLUX_HISTORY     30       // ~1s of flux history at 30Hz
#define BEAT_FLUX_THRESHOLD   1.8f     // total flux must be >= 1.8× running avg
#define BEAT_MIN_FLUX         30       // absolute noise floor
#define BEAT_BASS_LEAN_MIN    0.05f    // bass_flux must be >= 5% of total (low for MEMS mic weak bass)
#define BEAT_REFRACTORY_MS    200      // min ms between beats
#define BEAT_ENV_DECAY        0.92f    // beatEnv *= this each frame (~150ms decay at 30Hz)

struct AudioSpectrum {
  // --- PCM capture buffer ---
  int16_t pcmBuf[FFT_SIZE];

  // --- FFT working buffers (float for ESP32-S3 single-precision FPU) ---
  float vReal[FFT_SIZE];
  float vImag[FFT_SIZE];
  ArduinoFFT<float> fft;

  // --- Output fields (0.0-1.0, read by fillCtxAudio) ---
  float bass;
  float mid;
  float treble;
  float rms;
  float beatEnv;     // Smooth beat envelope (1.0 on beat, decays by BEAT_ENV_DECAY)

  // --- State ---
  bool enabled;      // Master toggle (user-facing, persisted in NVS)
  bool alive;        // True when enabled AND producing valid data

  // --- Internal ---
  unsigned long lastUpdateMs;

  // --- Beat detection state ---
  uint16_t prevBass;
  uint16_t prevMid;
  uint16_t prevTreble;
  int32_t  fluxHistory[BEAT_FLUX_HISTORY];
  int      fluxIdx;
  int32_t  fluxSum;
  uint32_t lastBeatMs;

  // Constructor — initialize FFT instance
  AudioSpectrum() : fft(vReal, vImag, FFT_SIZE, FFT_SAMPLE_RATE) {}

  void init() {
    enabled = false;
    alive = false;
    bass = mid = treble = rms = beatEnv = 0.0f;
    lastUpdateMs = 0;
    prevBass = prevMid = prevTreble = 0;
    fluxIdx = 0;
    fluxSum = 0;
    lastBeatMs = 0;
    memset(fluxHistory, 0, sizeof(fluxHistory));

    auto cfg = M5.Mic.config();
    cfg.sample_rate = FFT_SAMPLE_RATE;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = FFT_SIZE;
    M5.Mic.config(cfg);
    M5.Mic.begin();
  }

  void setEnabled(bool on) {
    enabled = on;
    if (!on) {
      alive = false;
      bass = mid = treble = rms = beatEnv = 0.0f;
    }
  }

  // Call each frame — rate-limited internally to ~30Hz
  void update() {
    if (!enabled) return;

    unsigned long now = millis();
    if (now - lastUpdateMs < SPECTRUM_UPDATE_MS) return;
    lastUpdateMs = now;

    // Decay beat envelope every tick regardless of mute state
    beatEnv *= BEAT_ENV_DECAY;
    if (beatEnv < 0.01f) beatEnv = 0.0f;

    // Mute while speaker is playing to avoid feedback
    if (botSounds.playing) {
      alive = false;
      return;
    }

    // --- Capture mic samples (non-blocking DMA) ---
    if (!M5.Mic.record(pcmBuf, FFT_SIZE, FFT_SAMPLE_RATE)) return;

    // --- Load PCM into FFT real buffer, zero imaginary ---
    for (int i = 0; i < FFT_SIZE; i++) {
      vReal[i] = (float)pcmBuf[i];
      vImag[i] = 0.0f;
    }

    // --- FFT ---
    fft.windowing(FFTWindow::Hann, FFTDirection::Forward);
    fft.compute(FFTDirection::Forward);
    fft.complexToMagnitude();

    // --- Band summation ---
    float sumBass = 0, sumMid = 0, sumTreble = 0, sumTotal = 0;
    for (int b = BAND_BASS_LO;   b < BAND_BASS_HI;   b++) sumBass   += vReal[b];
    for (int b = BAND_MID_LO;    b < BAND_MID_HI;    b++) sumMid    += vReal[b];
    for (int b = BAND_TREBLE_LO; b < BAND_TREBLE_HI; b++) sumTreble += vReal[b];
    for (int b = 1; b < FFT_SIZE / 2; b++)                sumTotal  += vReal[b];

    // --- Scale and clamp to 0-1023 (intermediate, matching noodle-v2 range) ---
    uint16_t bVal = (uint16_t)constrain(sumBass   * SPECTRUM_SCALE, 0.0f, 1023.0f);
    uint16_t mVal = (uint16_t)constrain(sumMid    * SPECTRUM_SCALE, 0.0f, 1023.0f);
    uint16_t tVal = (uint16_t)constrain(sumTreble * SPECTRUM_SCALE, 0.0f, 1023.0f);
    uint16_t rVal = (uint16_t)constrain(sumTotal  * SPECTRUM_SCALE, 0.0f, 1023.0f);

    // --- Normalize to 0.0-1.0 for EffectCtx ---
    bass    = bVal / 1023.0f;
    mid     = mVal / 1023.0f;
    treble  = tVal / 1023.0f;
    rms     = rVal / 1023.0f;

    // --- Beat detection (spectral flux + bass-leaning gate) ---
    // Per-band spectral flux: positive deltas only
    int32_t bassFlux = (int32_t)bVal - (int32_t)prevBass;
    int32_t midFlux  = (int32_t)mVal - (int32_t)prevMid;
    int32_t treFlux  = (int32_t)tVal - (int32_t)prevTreble;
    if (bassFlux < 0) bassFlux = 0;
    if (midFlux  < 0) midFlux  = 0;
    if (treFlux  < 0) treFlux  = 0;
    int32_t totalFlux = bassFlux + midFlux + treFlux;

    prevBass   = bVal;
    prevMid    = mVal;
    prevTreble = tVal;

    // Maintain running flux average via sliding window
    fluxSum -= fluxHistory[fluxIdx];
    fluxHistory[fluxIdx] = totalFlux;
    fluxSum += totalFlux;
    fluxIdx = (fluxIdx + 1) % BEAT_FLUX_HISTORY;
    float fluxAvg = (float)fluxSum / (float)BEAT_FLUX_HISTORY;

    // Bass dominance at this onset moment
    float bassLean = (totalFlux > 0)
                        ? (float)bassFlux / (float)totalFlux
                        : 0.0f;

    // Three-gate beat detection
    bool isBeat = (totalFlux > BEAT_MIN_FLUX) &&
                  ((float)totalFlux > fluxAvg * BEAT_FLUX_THRESHOLD) &&
                  (bassLean > BEAT_BASS_LEAN_MIN) &&
                  ((now - lastBeatMs) > BEAT_REFRACTORY_MS);

    if (isBeat) {
      lastBeatMs = now;
      beatEnv = 1.0f;
    }

    alive = true;

    // Debug: log levels every ~2 seconds
    static unsigned long lastDbg = 0;
    if (now - lastDbg > 2000) {
      lastDbg = now;
      Serial.printf("[AudioFX] bass=%.2f mid=%.2f tre=%.2f rms=%.2f beat=%.2f\n",
                    bass, mid, treble, rms, beatEnv);
    }
  }
};

// Global instance
AudioSpectrum audioSpectrum;

#endif // TARGET_CORES3
#endif // AUDIO_SPECTRUM_H
