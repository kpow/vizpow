#ifndef VOICE_CLIENT_H
#define VOICE_CLIENT_H

#ifdef VOICE_ENABLED

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "config.h"

// ============================================================================
// Voice Client — push-to-talk conversation with vizbrain (the Mac service)
// ============================================================================
// Front head-pad tap → record until silence → POST WAV to vizbrain → fetch the
// spoken reply → play it with a moving mouth. See docs/PLAN-vizbot-brain.md.
//
// Threading: the whole turn runs in voiceTask (Core 0, static stack). The
// render loop on Core 1 only reads the volatile hooks declared in bot_mode.h
// (voiceHoldsFace, voiceSpeakT0/voiceEnv, the bubble hand-off) and the web server
// only flips request flags here, so nothing touches gfx or the bus off-core.
//
// Audio: the CoreS3 mic and speaker share I2S_NUM_1. This module owns the
// hand-over for the length of a turn: voiceOwnsAudio pauses the spectrum
// analyzer, then Speaker.end() → Mic.begin() → record → Mic.end() →
// Speaker.begin() → play, and finally restores whatever was running before.
//
// Memory: record and playback buffers live in PSRAM (the first explicit PSRAM
// allocations in the project — ~2 MB of the 8 MB). Allocated on first use,
// kept for the life of the device.
// ============================================================================

extern void cmdSetExpression(uint8_t val);
extern char mdnsHostname[];

#define VOICE_MIC_RATE        16000
#define VOICE_CHUNK           512      // samples per mic read (32 ms)
#define VOICE_MAX_REC_MS      12000
#define VOICE_MIN_SPEECH_MS   300
#define VOICE_HANGOVER_MS     800      // silence that ends an utterance
#define VOICE_END_RATIO       0.15f    // "silence" = below 15% of the speaker's level
#define VOICE_WAIT_SPEECH_MS  6000     // give up if nothing is said
#define VOICE_WARMUP_CHUNKS   4        // ~130 ms ignored after Mic.begin()
#define VOICE_NOISE_CHUNKS    6        // ~190 ms to learn the room's noise
#define VOICE_SPEECH_FLOOR    300.0f   // min RMS that counts as speech
#define VOICE_GARBAGE_NOISE   1500.0f  // noise floor that means the mic isn't really capturing
#define VOICE_PLAY_RATE       24000
#define VOICE_MAX_PLAY_MS     30000
#define VOICE_BRAIN_PORT      4050
#define VOICE_CONNECT_MS      2500
#define VOICE_HTTP_TIMEOUT_MS 25000
#define VOICE_ENV_FRAME_MS    33       // lip-sync envelope resolution

#define VOICE_WAV_HDR_SAMPLES 22       // 44-byte WAV header, in int16 slots
#define VOICE_SENTINEL        INT16_MAX  // never produced by M5Unified's mic task

enum VoiceState : uint8_t {
  VOICE_IDLE = 0, VOICE_LISTENING, VOICE_THINKING, VOICE_SPEAKING, VOICE_ERROR
};
static const char* const VOICE_STATE_NAMES[] = {
  "idle", "listening", "thinking", "speaking", "error"
};

// Hooks read by the render loop (bot_mode.h) and the spectrum analyzer.
extern volatile bool voiceHoldsFace;
extern volatile uint32_t voiceSpeakT0;
extern uint8_t* volatile voiceEnv;
extern volatile uint32_t voiceEnvFrames;
extern volatile bool voiceOwnsAudio;     // audio_spectrum.h
extern void voiceShowBubble(const char* text, uint32_t ms);

struct VoiceClient {
  // Requests (set from Core 1 touch or the web server, consumed by voiceTask)
  volatile bool listenRequested = false;
  volatile bool cancelRequested = false;
  volatile bool playRequested = false;
  char     playId[24] = "";
  char     playText[MAX_SAY_LEN] = "";
  int8_t   playExpr = -1;
  uint32_t playMs = 0;

  volatile VoiceState state = VOICE_IDLE;
  bool     enabled = true;
  char     brainHost[64] = "";     // configured; empty = find _vizbrain._tcp
  char     resolved[64] = "";      // host:port in use

  // PSRAM buffers
  int16_t* recBuf = nullptr;       // WAV header + samples
  size_t   recCapSamples = 0;
  int16_t* playBuf = nullptr;
  size_t   playCapSamples = 0;
  uint8_t* env = nullptr;
  size_t   envCap = 0;

  // Diagnostics for /brain/status
  char     lastError[80] = "";
  char     lastHeard[MAX_SAY_LEN] = "";
  char     lastReply[MAX_SAY_LEN] = "";
  uint32_t turns = 0, failures = 0, micRestarts = 0;
  float    lastNoise = 0, lastPeak = 0, lastStartThr = 0;
  uint32_t lastRecMs = 0, lastSpeechMs = 0, lastBrainMs = 0, lastFetchMs = 0, lastPlayMs = 0;
  float    lastEndThr = 0;
  uint32_t lastFirstDataMs = 0;    // reply request → first audio byte
  uint32_t lastStarved = 0;        // times the speaker ran dry mid-reply
  uint32_t lastSpeakerMs = 0;      // time to bring the speaker up
  uint32_t lastRecSamples = 0;     // samples captured (for /brain/lastwav)
  uint16_t rmsTrace[240];          // per-chunk RMS of the last recording (32 ms each)
  uint16_t rmsTraceLen = 0;

  bool busy() const { return state != VOICE_IDLE; }
};

VoiceClient voice;

// ---- Settings (NVS, same "vizbot" namespace as the rest) -------------------

void loadVoiceSettings() {
  Preferences prefs;
  if (!prefs.begin("vizbot", true)) return;
  String h = prefs.getString("brainHost", "");
  strncpy(voice.brainHost, h.c_str(), sizeof(voice.brainHost) - 1);
  voice.enabled = prefs.getBool("voiceOn", true);
  prefs.end();
}

void saveVoiceSettings() {
  Preferences prefs;
  if (!prefs.begin("vizbot", false)) return;
  prefs.putString("brainHost", voice.brainHost);
  prefs.putBool("voiceOn", voice.enabled);
  prefs.end();
}

// ---- Small helpers ------------------------------------------------------------

static void voiceSetError(const char* msg) {
  strncpy(voice.lastError, msg, sizeof(voice.lastError) - 1);
  voice.lastError[sizeof(voice.lastError) - 1] = '\0';
  Serial.printf("[Voice] error: %s\n", msg);
}

static bool voiceAllocBuffers() {
  if (!voice.recBuf) {
    voice.recCapSamples = (size_t)VOICE_MIC_RATE * VOICE_MAX_REC_MS / 1000;
    voice.recBuf = (int16_t*)heap_caps_malloc(
        (voice.recCapSamples + VOICE_WAV_HDR_SAMPLES) * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  }
  if (!voice.playBuf) {
    voice.playCapSamples = (size_t)VOICE_PLAY_RATE * VOICE_MAX_PLAY_MS / 1000;
    voice.playBuf = (int16_t*)heap_caps_malloc(voice.playCapSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  }
  if (!voice.env) {
    voice.envCap = VOICE_MAX_PLAY_MS / VOICE_ENV_FRAME_MS + 2;
    voice.env = (uint8_t*)heap_caps_malloc(voice.envCap, MALLOC_CAP_SPIRAM);
  }
  return voice.recBuf && voice.playBuf && voice.env;
}

static float voiceChunkRms(const int16_t* s, size_t n) {
  double acc = 0;
  for (size_t i = 0; i < n; i++) acc += (double)s[i] * s[i];
  return sqrtf((float)(acc / n));
}

static void voiceWriteWavHeader(uint8_t* h, uint32_t samples) {
  uint32_t dataBytes = samples * 2;
  uint32_t riff = 36 + dataBytes;
  uint32_t rate = VOICE_MIC_RATE, byteRate = VOICE_MIC_RATE * 2;
  memcpy(h, "RIFF", 4);       memcpy(h + 4, &riff, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  uint32_t fmtLen = 16;       memcpy(h + 16, &fmtLen, 4);
  uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
  memcpy(h + 20, &pcm, 2);    memcpy(h + 22, &ch, 2);
  memcpy(h + 24, &rate, 4);   memcpy(h + 28, &byteRate, 4);
  memcpy(h + 32, &align, 2);  memcpy(h + 34, &bits, 2);
  memcpy(h + 36, "data", 4);  memcpy(h + 40, &dataBytes, 4);
}

// ---- Audio hand-over ----------------------------------------------------------

static void voiceTakeMic() {
  voiceOwnsAudio = true;
  vTaskDelay(pdMS_TO_TICKS(60));            // let audioSpectrum.update() see the flag
  if (M5.Speaker.isRunning()) M5.Speaker.end();
  // Always restart the mic. After a warm reset (e.g. a USB flash) the
  // already-running mic can deliver replayed stale DMA buffers instead of
  // audio; an end/begin cycle re-initialises the codec. Measured on HW.
  if (M5.Mic.isRunning()) M5.Mic.end();
  M5.Mic.begin();
}

static void voiceTakeSpeaker() {
  if (M5.Mic.isRunning()) M5.Mic.end();
  if (!M5.Speaker.isRunning()) M5.Speaker.begin();
  M5.Speaker.setVolume(botSounds.volume);
}

// Put the audio hardware back the way boot left it.
static void voiceRestoreAudio() {
  if (M5.Mic.isRunning()) M5.Mic.end();
  if (botSounds.useMidi) {
    if (M5.Speaker.isRunning()) M5.Speaker.end();
  } else if (!M5.Speaker.isRunning()) {
    M5.Speaker.begin();
    M5.Speaker.setVolume(botSounds.volume);
  }
  if (audioSpectrum.enabled) M5.Mic.begin();
  audioSpectrum.micRunning = M5.Mic.isRunning();
  voiceOwnsAudio = false;
}

// ---- Recording with end-of-speech detection --------------------------------
// Returns samples kept (0 = nothing usable). Adaptive thresholds: the quietest
// of the first ~190 ms sets the noise floor; speech starts above 3x noise and
// ends after VOICE_HANGOVER_MS below 2x noise.

static size_t voiceRecord(bool& cancelled, bool& garbage) {
  int16_t* buf = voice.recBuf + VOICE_WAV_HDR_SAMPLES;
  const size_t maxSamples = voice.recCapSamples - (voice.recCapSamples % VOICE_CHUNK);
  size_t n = 0, analyzed = 0;
  float noise = 0, peak = 0, startThr = VOICE_SPEECH_FLOOR, endThr = VOICE_SPEECH_FLOOR * 0.6f;
  int noiseN = 0, loudRun = 0;
  bool speech = false, done = false;
  float speechLevel = 0;
  uint32_t speechStartMs = 0, lastLoudMs = 0;
  cancelled = false;
  garbage = false;
  voice.rmsTraceLen = 0;

  int recordFails = 0;
  while (!done && n + VOICE_CHUNK <= maxSamples) {
    if (voice.cancelRequested) { cancelled = true; break; }
    // M5Unified clamps samples to INT16_MAX-16, so INT16_MAX in a chunk's last
    // slot means "not written yet". Don't infer completion from queue order:
    // that assumption read unwritten (stale PSRAM) chunks on hardware.
    buf[n + VOICE_CHUNK - 1] = VOICE_SENTINEL;
    if (!M5.Mic.record(buf + n, VOICE_CHUNK, VOICE_MIC_RATE)) {   // blocks for a free slot
      if (++recordFails > 20) { garbage = true; break; }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    n += VOICE_CHUNK;

    while (!done && analyzed + VOICE_CHUNK <= n && buf[analyzed + VOICE_CHUNK - 1] != VOICE_SENTINEL) {
      float rms = voiceChunkRms(buf + analyzed, VOICE_CHUNK);
      analyzed += VOICE_CHUNK;
      uint32_t tMs = analyzed / (VOICE_MIC_RATE / 1000);
      if (voice.rmsTraceLen < 240) voice.rmsTrace[voice.rmsTraceLen++] = (uint16_t)fminf(rms, 65535.0f);
      if (analyzed <= VOICE_WARMUP_CHUNKS * VOICE_CHUNK) continue;   // codec settling after begin()
      if (rms > peak) peak = rms;

      if (noiseN < VOICE_NOISE_CHUNKS) {
        noise = (noiseN == 0) ? rms : fminf(noise, rms);
        noiseN++;
        startThr = fmaxf(VOICE_SPEECH_FLOOR, noise * 3.0f);
        endThr   = fmaxf(VOICE_SPEECH_FLOOR * 0.6f, noise * 2.0f);
        // A real room idles around 50-150 RMS. A floor in the thousands means
        // the codec is replaying stale DMA buffers (seen on the first listen
        // after a warm reset): bail so the caller can restart the mic.
        if (noiseN == VOICE_NOISE_CHUNKS && noise > VOICE_GARBAGE_NOISE) { garbage = true; done = true; }
        // Speech may begin right away; count it if it's already loud.
      }
      if (!speech) {
        if (rms > startThr) {
          if (++loudRun >= 2) { speech = true; speechStartMs = tMs; lastLoudMs = tMs; }
        } else {
          loudRun = 0;
        }
        if (!speech && tMs > VOICE_WAIT_SPEECH_MS) done = true;
      } else {
        // Silence is relative to how loud the speaker was: someone talking at
        // the bot is far louder than a meeting or TV across the room, which
        // otherwise kept recordings open to the cap.
        if (rms > speechLevel) speechLevel = rms;
        else speechLevel = speechLevel * 0.98f + rms * 0.02f;
        float thr = fmaxf(endThr, speechLevel * VOICE_END_RATIO);
        if (rms > thr) lastLoudMs = tMs;
        if (tMs - lastLoudMs >= VOICE_HANGOVER_MS) done = true;
      }
    }
  }
  while (M5.Mic.isRecording()) vTaskDelay(1);

  voice.lastNoise = noise;
  voice.lastPeak = peak;
  voice.lastStartThr = startThr;
  voice.lastEndThr = endThr;
  voice.lastRecMs = n / (VOICE_MIC_RATE / 1000);
  voice.lastRecSamples = n;
  voice.lastSpeechMs = speech ? (lastLoudMs - speechStartMs) : 0;

  if (cancelled || !speech || voice.lastSpeechMs < VOICE_MIN_SPEECH_MS) return 0;
  // Keep everything up to 300 ms past the last loud chunk.
  size_t keep = (size_t)(lastLoudMs + 300) * (VOICE_MIC_RATE / 1000);
  return keep < n ? keep : n;
}

// ---- HTTP (plain, LAN only) -----------------------------------------------

static bool voiceResolveBrain() {
  if (voice.brainHost[0]) {
    if (strchr(voice.brainHost, ':')) strncpy(voice.resolved, voice.brainHost, sizeof(voice.resolved) - 1);
    else snprintf(voice.resolved, sizeof(voice.resolved), "%s:%d", voice.brainHost, VOICE_BRAIN_PORT);
    return true;
  }
  if (voice.resolved[0]) return true;
  int n = MDNS.queryService("vizbrain", "tcp");
  if (n <= 0) return false;
  IPAddress ip = MDNS.address(0);
  snprintf(voice.resolved, sizeof(voice.resolved), "%s:%u", ip.toString().c_str(), MDNS.port(0));
  Serial.printf("[Voice] brain at %s\n", voice.resolved);
  return true;
}

// Bytes are staged through internal RAM; lwIP shouldn't DMA from PSRAM.
static uint8_t voiceIoBuf[8192];

// POST the WAV; returns HTTP status (or -1) and fills resp (NUL-terminated).
static int voicePostWav(const uint8_t* data, size_t len, char* resp, size_t respCap) {
  char url[128];
  snprintf(url, sizeof(url), "http://%s/v1/voice", voice.resolved);
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = VOICE_HTTP_TIMEOUT_MS;
  cfg.buffer_size = 1024;
  cfg.buffer_size_tx = 4096;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return -1;
  esp_http_client_set_header(c, "Content-Type", "audio/wav");
  esp_http_client_set_header(c, "X-Bot-Id", mdnsHostname);
  int status = -1;
  if (esp_http_client_open(c, len) == ESP_OK) {
    size_t sent = 0;
    bool ok = true;
    while (sent < len) {
      size_t chunk = min(sizeof(voiceIoBuf), len - sent);
      memcpy(voiceIoBuf, data + sent, chunk);
      int w = esp_http_client_write(c, (const char*)voiceIoBuf, chunk);
      if (w <= 0) { ok = false; break; }
      sent += w;
    }
    if (ok) {
      esp_http_client_fetch_headers(c);
      status = esp_http_client_get_status_code(c);
      size_t got = 0;
      int r;
      while (got < respCap - 1 && (r = esp_http_client_read(c, resp + got, respCap - 1 - got)) > 0) got += r;
      resp[got] = '\0';
    }
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return status;
}

// ---- Speaking: stream the reply clip and play it as it arrives -------------
// The clip is downloaded into playBuf and handed to the speaker in 0.2 s
// pieces (M5's speaker queues two per channel), so he starts talking ~0.3 s
// after the first bytes instead of after the whole clip. Long replies used to
// spend 2-3 s downloading in silence.

#define VOICE_SEG_SAMPLES      4800   // first piece: 0.2 s, for a fast start
#define VOICE_SEG_MAX_SAMPLES  36000  // later pieces up to 1.5 s, so a read that
                                      // blocks between sentences can't starve the speaker
#define VOICE_PREROLL_SAMPLES  7200   // 0.3 s buffered before the first piece plays
#define VOICE_READ_BYTES       4096   // esp_http_client_read blocks until it fills this

// Returns samples played (0 = the clip couldn't be fetched).
// hintMs: the brain's estimate for how long to show the bubble (the clip may
// still be growing when playback starts, so its length isn't known).
static size_t voiceStreamSpeak(const char* id, const char* bubble, int8_t expr, uint32_t hintMs) {
  char url[128];
  snprintf(url, sizeof(url), "http://%s/v1/audio/%s", voice.resolved, id);
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = VOICE_HTTP_TIMEOUT_MS;
  cfg.buffer_size = 4096;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return 0;
  uint32_t tReq = millis();
  int64_t contentLen = -1;
  voice.lastFirstDataMs = 0;
  voice.lastStarved = 0;
  // A live clip arrives chunked (length unknown), so only the status decides.
  if (esp_http_client_open(c, 0) != ESP_OK ||
      ((contentLen = esp_http_client_fetch_headers(c)), esp_http_client_get_status_code(c) != 200)) {
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return 0;
  }

  const size_t capBytes = voice.playCapSamples * sizeof(int16_t);
  const size_t perFrame = VOICE_PLAY_RATE * VOICE_ENV_FRAME_MS / 1000;
  uint8_t* dst = (uint8_t*)voice.playBuf;
  size_t got = 0, queued = 0, envFrames = 0;
  float envPeak = 2000.0f;      // running loudness reference for the mouth
  uint32_t t0 = 0;
  bool eof = false;
  uint32_t clipMs = contentLen > 0 ? (uint32_t)(contentLen / 2 * 1000 / VOICE_PLAY_RATE) : 0;

  uint32_t tSpk = millis();
  voiceTakeSpeaker();
  voice.lastSpeakerMs = millis() - tSpk;
  for (;;) {
    if (voice.cancelRequested) break;
    if (!eof) {
      int r = (got < capBytes)
          ? esp_http_client_read(c, (char*)voiceIoBuf, min((size_t)VOICE_READ_BYTES, capBytes - got)) : 0;
      if (r > 0) {
        if (got == 0) voice.lastFirstDataMs = millis() - tReq;
        memcpy(dst + got, voiceIoBuf, r);
        got += r;
      } else eof = true;
    }
    const size_t avail = got / 2;

    // Mouth envelope for every complete 33 ms frame (and the tail at EOF).
    while (envFrames < voice.envCap &&
           ((envFrames + 1) * perFrame <= avail || (eof && envFrames * perFrame < avail))) {
      size_t a = envFrames * perFrame, n = min(perFrame, avail - a);
      float rms = voiceChunkRms(voice.playBuf + a, n);
      if (rms > envPeak) envPeak = rms;
      voice.env[envFrames++] = (uint8_t)fminf(12.0f, rms * 12.0f / envPeak);
    }
    voiceEnvFrames = envFrames;

    // Queue pieces whenever the speaker has a free slot (never block on it).
    if (t0 || avail >= VOICE_PREROLL_SAMPLES || eof) {
      while (M5.Speaker.isPlaying(0) < 2 && queued < avail) {
        if (t0 && M5.Speaker.isPlaying(0) == 0) voice.lastStarved++;   // speaker ran dry mid-reply
        size_t seg = t0 ? VOICE_SEG_MAX_SAMPLES : VOICE_SEG_SAMPLES;
        size_t n = min(seg, avail - queued);
        if (n < VOICE_SEG_SAMPLES && !eof) break;   // wait for at least a small piece
        if (!t0) {
          if (expr >= 0 && expr < BOT_NUM_EXPRESSIONS) cmdSetExpression((uint8_t)expr);
          if (bubble && bubble[0]) voiceShowBubble(bubble, (clipMs ? clipMs : (hintMs ? hintMs : 6000)) + 1500);
          voice.state = VOICE_SPEAKING;
          voice.lastFetchMs = millis() - tReq;      // request → first sound
          t0 = millis();
          voiceEnv = voice.env;
          voiceSpeakT0 = t0;
        }
        M5.Speaker.playRaw(voice.playBuf + queued, n, VOICE_PLAY_RATE, false, 1, 0, false);
        queued += n;
      }
    }
    if (eof && queued >= avail) break;
    if (eof) vTaskDelay(pdMS_TO_TICKS(10));        // only waiting on speaker slots now
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);

  // Let the queued audio finish, mouth still moving.
  while (t0 && M5.Speaker.isPlaying(0) && !voice.cancelRequested) {
    vTaskDelay(pdMS_TO_TICKS(15));
  }
  if (voice.cancelRequested) M5.Speaker.stop(0);
  voiceSpeakT0 = 0;
  voice.lastPlayMs = t0 ? millis() - t0 : 0;
  return queued;
}

// ---- Turns ---------------------------------------------------------------------

static void voiceFinish(VoiceState how) {
  voiceRestoreAudio();
  voiceSpeakT0 = 0;
  if (how == VOICE_ERROR) {
    voice.failures++;
    voice.state = VOICE_ERROR;
    cmdSetExpression(EXPR_CONFUSED);
    vTaskDelay(pdMS_TO_TICKS(2500));
  }
  voiceHoldsFace = false;
  voice.cancelRequested = false;
  voice.state = VOICE_IDLE;
}

static void voiceRunTurn() {
  if (!voiceAllocBuffers()) {
    voiceSetError("PSRAM allocation failed");
    voiceShowBubble("Out of memory", 2500);
    voice.failures++;
    return;
  }
  voice.lastError[0] = '\0';
  voiceHoldsFace = true;
  voice.cancelRequested = false;

  // 1. Listen
  voice.state = VOICE_LISTENING;
  cmdSetExpression(EXPR_FOCUSED);
  voiceShowBubble("Listening...", VOICE_MAX_REC_MS);
  voiceTakeMic();
  bool cancelled = false, garbage = false;
  size_t samples = voiceRecord(cancelled, garbage);
  for (int retry = 0; garbage && retry < 2; retry++) {
    voice.micRestarts++;
    Serial.println("[Voice] mic delivering stale data, restarting it");
    M5.Mic.end();
    vTaskDelay(pdMS_TO_TICKS(100));
    M5.Mic.begin();
    samples = voiceRecord(cancelled, garbage);
  }
  if (M5.Mic.isRunning()) M5.Mic.end();

  if (cancelled) {
    voiceShowBubble("Never mind.", 1500);
    cmdSetExpression(EXPR_NEUTRAL);
    voiceFinish(VOICE_IDLE);
    return;
  }
  if (samples == 0) {
    voiceShowBubble("I didn't hear anything.", 2500);
    voiceSetError("no speech detected");
    voiceFinish(VOICE_ERROR);
    return;
  }

  // 2. Think
  voice.state = VOICE_THINKING;
  cmdSetExpression(EXPR_THINKING);
  voiceShowBubble("Hmm...", 20000);
  if (!voiceResolveBrain()) {
    voiceSetError("vizbrain not found (mDNS _vizbrain._tcp)");
    voiceShowBubble("My brain is offline.", 3000);
    voiceFinish(VOICE_ERROR);
    return;
  }
  voiceWriteWavHeader((uint8_t*)voice.recBuf, samples);
  static char resp[1536];
  uint32_t t0 = millis();
  int status = voicePostWav((const uint8_t*)voice.recBuf,
                            (samples + VOICE_WAV_HDR_SAMPLES) * sizeof(int16_t), resp, sizeof(resp));
  voice.lastBrainMs = millis() - t0;
  if (status != 200) {
    char msg[80];
    snprintf(msg, sizeof(msg), "brain POST failed (status %d)", status);
    voiceSetError(msg);
    if (!voice.brainHost[0]) voice.resolved[0] = '\0';   // re-discover next time
    voiceShowBubble(status == 503 ? "Still thinking about the last one." : "My brain is offline.", 3000);
    voiceFinish(VOICE_ERROR);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    voiceSetError("bad JSON from brain");
    voiceFinish(VOICE_ERROR);
    return;
  }
  strncpy(voice.lastHeard, doc["heard"] | "", sizeof(voice.lastHeard) - 1);
  strncpy(voice.lastReply, doc["bubble"] | "", sizeof(voice.lastReply) - 1);
  const char* clip = doc["audio_id"] | "";
  int8_t expr = doc["expression"] | -1;
  uint32_t hintMs = doc["audio_ms"] | 0;

  // 3. Speak (streamed: he starts talking while the clip downloads)
  size_t n = clip[0] ? voiceStreamSpeak(clip, voice.lastReply, expr, hintMs) : 0;
  if (n == 0 && !voice.cancelRequested) {
    voiceShowBubble(voice.lastReply, 4000);   // at least show the answer
    voiceSetError("reply audio fetch failed");
    voiceFinish(VOICE_ERROR);
    return;
  }
  voice.turns++;
  voiceFinish(VOICE_IDLE);
}

// Brain-initiated speech (/brain/play): typed turns, greetings, events.
static void voiceRunPlay() {
  if (!voiceAllocBuffers() || !voiceResolveBrain()) {
    voiceSetError("play: no buffers or brain");
    return;
  }
  voiceHoldsFace = true;
  voice.state = VOICE_THINKING;
  voiceOwnsAudio = true;
  vTaskDelay(pdMS_TO_TICKS(60));
  if (voiceStreamSpeak(voice.playId, voice.playText, voice.playExpr, voice.playMs) == 0) {
    voiceSetError("play: clip fetch failed");
    voiceShowBubble(voice.playText, 4000);
  }
  voiceFinish(VOICE_IDLE);
}

// ---- Entry points ----------------------------------------------------------------

// Front head-pad tap: start listening, or cancel if already listening.
void voiceOnTalkTap() {
  if (!voice.enabled) return;
  if (voice.state == VOICE_LISTENING || voice.state == VOICE_SPEAKING) {
    voice.cancelRequested = true;
  } else if (voice.state == VOICE_IDLE) {
    voice.listenRequested = true;
  }
}

static StackType_t voiceTaskStack[8192];   // BSS, keeps the TLS heap block whole
static StaticTask_t voiceTaskTCB;

void voiceTask(void*) {
  for (;;) {
    if (voice.listenRequested) {
      voice.listenRequested = false;
      voiceRunTurn();
    } else if (voice.playRequested) {
      voice.playRequested = false;
      voiceRunPlay();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void startVoiceTask() {
  loadVoiceSettings();
  xTaskCreateStaticPinnedToCore(voiceTask, "voice", sizeof(voiceTaskStack), nullptr, 1,
                                voiceTaskStack, &voiceTaskTCB, 0);
  Serial.printf("[Voice] task started (enabled=%d, brain=%s)\n", voice.enabled,
                voice.brainHost[0] ? voice.brainHost : "mDNS");
}

#endif // VOICE_ENABLED
#endif // VOICE_CLIENT_H
