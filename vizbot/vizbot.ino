/*
 * vizBot — ESP32-S3 Bot Face Firmware
 *
 * An animated companion face for the ESP32-S3-Touch-LCD-1.69.
 * Renders expressive bot face with ambient effect backgrounds.
 *
 * Features:
 * - 20 facial expressions with smooth transitions
 * - Ambient effect backgrounds (hi-res LCD)
 * - Speech bubbles and time overlay
 * - Shake/tap reactions via IMU + touch
 * - WiFi AP web interface for control
 *
 * Hardware:
 * - ESP32-S3-Touch-LCD-1.69
 * - QMI8658 6-axis IMU (I2C)
 * - CST816T Touch Controller (I2C)
 *
 * License: MIT
 */

#include <FastLED.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_wifi.h>   // esp_wifi_set_max_tx_power (WiFi.setTxPower no-ops before start event)
#include <esp_heap_caps.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#ifdef TARGET_CORES3
#include <M5Unified.h>
#else
#include "SensorQMI8658.hpp"
#endif

#include "config.h"
#include "mem.h"            // Memory owner: every runtime buffer, by name (see /debug/mem)
#ifdef VIZ_RAM_TRACE
void ramTraceBegin();       // ram_trace.cpp (bench builds)
#endif
#include "device_id.h"  // Per-device unique SSID + mDNS hostname (from eFuse MAC)
#include "palettes.h"
#include "loop_stats.h"     // Real loop/push timing for GET /state (must come before display_lcd.h)
#include "display_lcd.h"    // Must come before any file that calls gfx->methods() (defines DisplayProxy)
#include "buzzer.h"         // 1.69 passive piezo (LEDC square wave); no-op without BUZZER_PIN
#include "battery_adc.h"    // 1.69 battery level on ADC1 (must come before the touch UI); no-op without BAT_ADC_PIN
#include "tween.h"          // Tween animation system (must come before bot_mode.h)
#include "midi_synth.h"     // SAM2695 driver (CoreS3) + GM constants (must come before bot_sounds.h)
#include "bot_sounds.h"     // Sequence engine: MIDI / M5.Speaker / piezo (must come before bot_mode.h)
#ifdef TARGET_CORES3
#include "audio_spectrum.h"  // FFT spectrum analyzer (must come before effects_ambient.h)
#include "proximity_light.h" // Core S3 proximity & ambient light sensor
#endif
#include "effects_ambient.h"
#include "album_art.h"      // Covers from vizbrain drawn instead of the face (must come before bot_mode.h)
#include "bot_mode.h"
#include "info_mode.h"
#include "bot_sleep.h"      // Sleep API (owner: bot_sleep_owner.h); before the touch UI and web server
#include "wled_display.h"
#include "wled_weather_view.h"
#include "wled_scheduled_content.h"
#ifdef CLOUD_ENABLED
#include <ArduinoJson.h>
#include "content_cache.h"
#include "cloud_client.h"
#endif
#ifdef VOICE_ENABLED
#include "voice_client.h"   // vizbrain push-to-talk (needs bot_mode/bot_sounds/audio_spectrum)
#endif
#include "web_server.h"
#include "settings.h"
#if defined(TOUCH_ENABLED)
#include "touch_control.h"
#ifdef HAS_BASE_LEDS
#include "stackchan_leds.h"
#endif
#ifdef BOARD_HAS_FACES_BASE
#include "faces_chooser.h"
#endif
#ifdef BOARD_HAS_STACKCHAN_BASE
// Head touch and idle servo drift need the servo/expander hardware; the Faces
// base has neither, so they are not built for it.
#include "stackchan_touch.h"
#include "stackchan_idle.h"
#endif
#endif
#include "power_button.h"   // 1.69 PWR button hold-to-power-off; no-op without PWR_HOLD_PIN
#include "pendant_shake.h" // low power shake reactions; no-op without HAS_LOW_POWER_MODE
#include "bot_sleep_owner.h" // Sleep schedule owner; no-op without HAS_SLEEP_SCHEDULE
#include "task_manager.h"
#include "esp_now_mesh.h"
#include "wifi_provisioning.h"
#include "boot_sequence.h"

#ifdef BOARD_HAS_STACKCHAN_BASE
// Head pad log context (stackchan_touch.h): what else was busy at this moment.
// Reads flags only; called from the loop task on every head touch read.
uint16_t scTouchContext() {
  uint16_t c = 0;
  const uint32_t now = millis();
#ifdef VOICE_ENABLED
  if (voicePhase == 1) c |= 1u << 0;             // listening
  if (voicePhase == 2) c |= 1u << 1;             // thinking
  if (voicePhase == 3) c |= 1u << 2;             // speaking
  if (wake.active)     c |= 1u << 3;             // wake mic streaming
  if (wake.open)       c |= 1u << 4;             // ...and sending audio over WiFi
#endif
  if ((int32_t)(scHeadBusyUntilMs - now) > 0) c |= 1u << 5;   // a head move (plus 1.2 s settle)
  if (scServosResting) c |= 1u << 6;                          // servo rail off
#ifdef CLOUD_ENABLED
  if (cloudState == CLOUD_SYNCING || cloudState == CLOUD_REGISTERING) c |= 1u << 7;
#endif
  if (scCamRunning)       c |= 1u << 8;
  if (partyBlob.active)   c |= 1u << 9;
  if (M5.Speaker.isPlaying()) c |= 1u << 10;
  if (Update.isRunning()) c |= 1u << 11;          // firmware upload
  if (scTouch_state.chillMode) c |= 1u << 12;
  return c;
}
#endif

#ifdef BOARD_HAS_STACKCHAN_BASE
// ---- App AO (stage 1 of the Active Object move, ao_core.h) -------------------
// Acts on what TouchAO publishes. The nod and shake still block, inside this
// handler, until MotionAO (stage 2) turns them into stepped moves.
static AoActive scAppAO;
static uint32_t scIdleHoldUntilMs = 0;   // TouchAO wants the head still until then

static void scAppOnEvent(const AoEvt& e) {
  switch (e.sig) {
    case SIG_TOUCH_TAP:
      // Back half = shake. Front half = talk to vizbrain on voice builds (tap
      // again to cancel), nod otherwise. Middle is never published.
      if (e.a == SC_ZONE_BACK) scFireShake();
      else {
        #ifdef VOICE_ENABLED
        voiceOnTalkTap();
        #else
        scFireNod();
        #endif
      }
      break;
    case SIG_TOUCH_HOLD:
      scFireChillMode();
      break;
    case SIG_TOUCH_NEED_STILL: {
      const uint32_t until = millis() + (uint32_t)e.a;
      if ((int32_t)(until - scIdleHoldUntilMs) > 0) scIdleHoldUntilMs = until;
      break;
    }
    default: break;
  }
}

// ---- Probe: state edges nobody publishes yet -------------------------------------
// The mic, speaker (amp), camera, cloud sync and voice phase are switched from
// ~20 places on three tasks. Until AudioAO (stage 3) and NetAO (stage 5) own
// them, this watches their flags once per loop pass, just before TouchAO runs,
// and publishes each change.
static void scProbeTick() {
  static bool mic = false, spk = false, cam = false, cloud = false, wakeTx = false;
  static uint8_t phase = 0;
  const bool micNow = M5.Mic.isRunning();
  if (micNow != mic) { mic = micNow; aoPublish(mic ? SIG_MIC_START : SIG_MIC_STOP, SRC_AUDIO); }
  const bool spkNow = M5.Speaker.isRunning();
  if (spkNow != spk) { spk = spkNow; aoPublish(spk ? SIG_AMP_ON : SIG_AMP_OFF, SRC_AUDIO); }
  const bool camNow = scCamRunning;
  if (camNow != cam) { cam = camNow; aoPublish(cam ? SIG_CAMERA_START : SIG_CAMERA_END, SRC_CAMERA); }
#ifdef CLOUD_ENABLED
  const bool cloudNow = cloudState == CLOUD_SYNCING || cloudState == CLOUD_REGISTERING;
  if (cloudNow != cloud) { cloud = cloudNow; aoPublish(cloud ? SIG_NET_TX_START : SIG_NET_TX_END, SRC_NET, 2); }
#endif
#ifdef VOICE_ENABLED
  if (voicePhase != phase) { phase = voicePhase; aoPublish(SIG_VOICE_PHASE, SRC_VOICE, phase); }
  if (wake.open != wakeTx) { wakeTx = wake.open; aoPublish(wakeTx ? SIG_NET_TX_START : SIG_NET_TX_END, SRC_NET, 3); }
#endif
  (void)cloud; (void)phase; (void)wakeTx;
}

static void scAoStart() {
  aoTimelineBegin();
  scTouchAOStart();                    // dispatched first
  scAppAO.name = "app";
  scAppAO.onEvent = scAppOnEvent;
  scAppAO.tickMs = 60000;              // no time work yet
  aoStart(scAppAO);
  aoSubscribe(scAppAO, SIG_TOUCH_TAP);
  aoSubscribe(scAppAO, SIG_TOUCH_HOLD);
  aoSubscribe(scAppAO, SIG_TOUCH_NEED_STILL);
  scMotionAOStart();                   // after the app AO: it acts on taps by posting gestures
  scBusAOStart();                      // power sensors: one owner, a snapshot for everyone else
  aoPublish(SIG_BOOT_READY, SRC_APP);
}
#endif

// Global objects
CRGB leds[NUM_LEDS];
#ifndef TARGET_CORES3
SensorQMI8658 imu;
#endif
WebServer server(80);
DNSServer dnsServer;
bool wifiEnabled = false;
bool lowPowerMode = false;   // System > Low power (HAS_LOW_POWER_MODE boards); applied at boot

// State variables
uint8_t effectIndex = 0;
uint8_t paletteIndex = 0;
uint8_t brightness = DEFAULT_BRIGHTNESS;
uint8_t lcdBrightness = 255;
uint8_t speed = 20;
bool autoCycle = true;
uint8_t currentMode = MODE_BOT;
#ifdef MIDI_SYNTH_ENABLED
uint8_t midiSynthPort = MIDI_SYNTH_PORT_DEFAULT;   // 0=Port C, 1=Port A (web-configurable)
#endif

// Shuffle bag for ambient effect cycling (used for bot background)
uint8_t effectShuffleBag[NUM_AMBIENT_EFFECTS];
uint8_t effectShufflePos = 0;
uint8_t effectShuffleSize = 0;
unsigned long lastChange = 0;
unsigned long lastPaletteChange = 0;

uint8_t paletteShuffleBag[NUM_PALETTES];
uint8_t paletteShufflePos = 0;

// Current palette
CRGBPalette16 currentPalette;

// IMU data
float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;

// Weather location (runtime, saved to NVS)
char weatherLat[12] = WEATHER_LAT_DEFAULT;
char weatherLon[12] = WEATHER_LON_DEFAULT;

// Time zone (runtime, saved to NVS) — POSIX TZ string, default US Eastern
char timezoneTZ[TZ_BUF_LEN] = TZ_DEFAULT;

// Auto-brightness (toggled by cloud command)
bool autoBrightnessEnabled = false;
static unsigned long lastAutoBrightnessMs = 0;

// Mesh scan request (stub for ESP-NOW peer discovery)
volatile bool meshScanRequested = false;

// Sustained shake tracking (for info mode toggle)
unsigned long shakeStartTime = 0;        // When continuous shaking began
bool shakeSustainedTriggered = false;    // Whether sustained shake already fired
uint8_t shakeAboveCount = 0;            // Total frames above threshold
unsigned long shakeGapStart = 0;         // When the reading dipped below threshold (0 = not in a gap)
#define SHAKE_GAP_TOLERANCE_MS 460      // Grace below threshold: the 6 frames it was tuned on at the old 77 ms loop

// Fisher-Yates shuffle
void shuffleArray(uint8_t* arr, uint8_t size) {
  for (uint8_t i = size - 1; i > 0; i--) {
    uint8_t j = random(i + 1);
    uint8_t tmp = arr[i];
    arr[i] = arr[j];
    arr[j] = tmp;
  }
}

void resetEffectShuffle() {
  effectShuffleSize = NUM_AMBIENT_EFFECTS;
  for (uint8_t i = 0; i < effectShuffleSize; i++) {
    effectShuffleBag[i] = i;
  }
  shuffleArray(effectShuffleBag, effectShuffleSize);
  effectShufflePos = 0;
}

void resetPaletteShuffle() {
  for (uint8_t i = 0; i < NUM_PALETTES; i++) {
    paletteShuffleBag[i] = i;
  }
  shuffleArray(paletteShuffleBag, NUM_PALETTES);
  paletteShufflePos = 0;
}

uint8_t nextShuffledEffect() {
  if (effectShufflePos >= effectShuffleSize) {
    resetEffectShuffle();
  }
  return effectShuffleBag[effectShufflePos++];
}

uint8_t nextShuffledPalette() {
  if (paletteShufflePos >= NUM_PALETTES) {
    resetPaletteShuffle();
  }
  return paletteShuffleBag[paletteShufflePos++];
}

// Personality-driven effect selection: 70% from favorites, 30% full shuffle
uint8_t nextPersonalityEffect() {
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
  const RuntimePersonality* p = botMode.personality;
  if (p && p->favoriteEffectCount > 0 && random(100) < 70) {
    return p->favoriteEffects[random(p->favoriteEffectCount)];
  }
  #endif
  return nextShuffledEffect();
}

// Personality-driven palette selection: 70% from favorites, 30% full shuffle
uint8_t nextPersonalityPalette() {
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
  const RuntimePersonality* p = botMode.personality;
  if (p && p->favoritePaletteCount > 0 && random(100) < 70) {
    return p->favoritePalettes[random(p->favoritePaletteCount)];
  }
  #endif
  return nextShuffledPalette();
}

// Pick a saying category from the personality's bitmask (for idle sayings)
SayingCategory pickPersonalitySayCategory() {
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
  const RuntimePersonality* p = botMode.personality;
  if (p && p->sayingCategoryMask != 0) {
    // Collect enabled categories
    SayingCategory cats[16];
    uint8_t count = 0;
    for (uint8_t i = 0; i < SAY_CATEGORY_COUNT && count < 16; i++) {
      if (p->sayingCategoryMask & (1 << i)) {
        cats[count++] = (SayingCategory)i;
      }
    }
    if (count > 0) return cats[random(count)];
  }
  #endif
  return SAY_IDLE;
}

// Helper function to show output on configured displays
void showDisplay() {
  #if defined(DISPLAY_LED_ONLY) || defined(DISPLAY_DUAL)
    FastLED.show();
  #endif
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
    renderToLCD();
  #endif
}

// Sparkle intro animation at startup
void introAnimation() {
  unsigned long startTime = millis();
  while (millis() - startTime < INTRO_DURATION_MS) {
    fadeToBlackBy(leds, NUM_LEDS, INTRO_FADE_RATE);
    int pos = random16(NUM_LEDS);
    leds[pos] = CHSV(random8(), 255, INTRO_SPARKLE_BRIGHTNESS);
    showDisplay();
    delay(20);
  }
  FastLED.clear();
  showDisplay();
}

void readIMU() {
  #ifdef TARGET_CORES3
  // BMI270 via M5Unified: M5's bus mutex covers each transaction, but not the
  // camera's SCCB window, which holds the app lock. Skip this frame if busy.
  if (!i2cTry(0)) return;
  M5.Imu.getAccel(&accelX, &accelY, &accelZ);
  M5.Imu.getGyro(&gyroX, &gyroY, &gyroZ);
  i2cRelease();
  #else
  if (!i2cAcquire()) return;  // Skip this cycle if bus is busy
  if (imu.getDataReady()) {
    imu.getAccelerometer(accelX, accelY, accelZ);
    imu.getGyroscope(gyroX, gyroY, gyroZ);
  }
  i2cRelease();
  #endif
}

// Start or restart the WiFi AP hotspot (used by touch menu toggle)
void startWifiAP() {
  if (!wifiEnabled) {
    WiFi.mode(WIFI_AP);
    delay(100);
  }
  bool apStarted = WiFi.softAP(apSSID, WIFI_PASSWORD, 1, false, 4);
  DBGLN("--- WiFi AP restart ---");
  DBG("softAP returned: ");
  DBGLN(apStarted ? "YES" : "NO");

  WiFi.setSleep(false);
  esp_wifi_set_max_tx_power(WIFI_TX_POWER);

  // Wait for AP to actually start
  uint8_t retries = 0;
  while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0) && retries < 20) {
    delay(100);
    retries++;
  }

  if (!wifiEnabled && !sysStatus.webServerReady) {
    setupWebServer();
    sysStatus.webServerReady = true;
  }
  wifiEnabled = true;
  sysStatus.wifiReady = true;
  sysStatus.apIP = WiFi.softAPIP();

  // Start captive portal DNS + mDNS if not already running
  if (!sysStatus.dnsReady) {
    startDNS();
    sysStatus.dnsReady = true;
  }
  if (!sysStatus.mdnsReady) {
    sysStatus.mdnsReady = startMDNS();
  }

  // Radio was off at boot (low power mode): the server task and mesh never started
  if (!wifiTaskHandle) startWifiTask();
  initMesh();
}

// Stop the WiFi AP hotspot
void stopWifiAP() {
  stopDNS();
  sysStatus.dnsReady = false;
  sysStatus.mdnsReady = false;
  MDNS.end();
  WiFi.softAPdisconnect(true);
  wifiEnabled = false;
  sysStatus.wifiReady = false;
  DBGLN("WiFi AP stopped");
}

// Toggle WiFi AP on/off
void toggleWifiAP() {
  if (wifiEnabled) {
    stopWifiAP();
  } else {
    startWifiAP();
  }
}

void setup() {
  #ifdef PWR_HOLD_PIN
  pinMode(PWR_HOLD_PIN, OUTPUT);
  digitalWrite(PWR_HOLD_PIN, HIGH);   // Latch battery power first — before the 500 ms serial delay
  powerButton.init();
  #endif
  #ifdef BAT_ADC_PIN
  batteryAdc.init();
  #endif
  memInit();   // mem.h: TLS memory goes to PSRAM. Before any TLS.
#ifdef VIZ_RAM_TRACE
  ramTraceBegin();   // bench: internal-RAM allocation tracer (ram_trace.cpp)
#endif
  Serial.begin(115200);
  delay(500);

  DBGLN("\n=== vizBot starting ===");
  #ifdef HAS_LOW_POWER_MODE
  loadLowPowerMode();   // before the boot sequence, which starts the radio
  pendantShake.init();
  if (lowPowerMode) {
    setCpuFrequencyMhz(LOW_POWER_CPU_MHZ);
    DBGLN("Low power mode: radio off, CPU " + String(getCpuFrequencyMhz()) + " MHz");
  }
  #endif
  #ifdef BUZZER_PIN
  buzzer.init();    // Drive the piezo pin LOW first — floating, it draws current and heats the board
  #endif
  initDeviceID();   // Compute unique apSSID / mdnsHostname from eFuse MAC
  otaMarkBootValid();
  #ifdef BOARD_HAS_FACES_BASE
  // Hand the NEXT boot back to the chooser. Without this, the otadata entry the
  // chooser wrote to start us keeps pointing here and the device would boot
  // straight into vizBot from now on, never showing the chooser again.
  armChooserNextBoot();
  #endif // Tell bootloader this firmware is OK (prevents OTA rollback)

  // Memory baseline — logged early before any allocations fragment the heap
  DBG("Internal heap: ");
  DBG(ESP.getFreeHeap() / 1024);
  DBG("KB free, largest block=");
  DBG(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
  DBGLN("KB");

  // PSRAM detection — needed to decide canvas strategy (16-bit PSRAM vs 8-bit internal)
  if (psramFound()) {
    sysStatus.psramAvailable = true;
    sysStatus.psramSizeKB = ESP.getPsramSize() / 1024;
    DBG("PSRAM: ");
    DBG(sysStatus.psramSizeKB);
    DBG("KB total, ");
    DBG(ESP.getFreePsram() / 1024);
    DBGLN("KB free");
  } else {
    sysStatus.psramAvailable = false;
    sysStatus.psramSizeKB = 0;
    DBGLN("PSRAM: not found");
  }

  // Core S3: M5Unified must init first — it sets up AW9523 expander,
  // AXP2101 PMU, ILI9342C display, BMI270 IMU, and capacitive touch.
  #ifdef TARGET_CORES3
  auto cfg = M5.config();
  #ifdef BOARD_HAS_FACES_BASE
  // The speaker's I2S bit clock is GPIO13 — the Faces base's LED data line. Let
  // M5Unified bring the speaker up here and the I2S peripheral free-runs on that
  // pin for the rest of the session, so the strips never show anything this
  // firmware writes. Both stay off until audio mode asks for them.
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  #endif
  // StackChan: leave cfg.output_power at M5's default (on), as M5's own firmware does.
  // Starting with the 5 V output off (the old skunky battery-care start) brought on
  // the cold-boot phantom head-touch storm: 1837 contacts in an hour on 2026-11-24,
  // against 2 in 20 min with it on, same unit, same settings.
  M5.begin(cfg);
  #ifdef BOARD_HAS_STACKCHAN_BASE
  plogCaptureBoot();   // why it last switched off, before anything else touches the power chip
  scBatteryCareBegin();   // charge to 4.1 V at 300 mA; 5 V output on unless it blocks USB
  #endif
  #ifdef BOARD_HAS_FACES_BASE
  M5.Speaker.end();
  #endif
  #endif

  // Initialize task infrastructure (I2C mutex + command queue)
  initTaskManager();

  // Initialize LCD first — we need it to show the boot screen
  initLCD();

  // Run the visual boot sequence (initializes all subsystems with onscreen feedback)
  // This handles: LEDs, I2C, IMU, Touch, WiFi AP, Web Server
  runBootSequence();

  #if defined(HAS_SOUND) && !defined(TARGET_CORES3)
  botSounds.init();   // CoreS3 does this in the boot sequence's sensor stage
  #endif

  #ifdef HAS_BASE_LEDS
  scLeds.init();      // state only (no hardware); before loadSettings() so saved ring values stick
  #endif

  // Load persistent settings from NVS (brightness, palette, etc.) — after
  // botSounds.init(), which would otherwise reset the saved volume
  loadSettings();

  // Load WLED display settings from NVS
  loadWledSettings();
  loadScheduleSettings();

  // Apply loaded settings to hardware
  FastLED.setBrightness(brightness);
  setLCDBacklight(lcdBrightness);
  #ifdef TARGET_CORES3
  // Never boot into a near-black screen, but keep a dimmed setting from the touch UI
  if (lcdBrightness < 26) {
    lcdBrightness = 255;
    setLCDBacklight(255);
  }
  #endif

  // Set palette from saved index
  currentPalette = palettes[paletteIndex % NUM_PALETTES];

  // Initialize tween animation system
  tweenManager.init();

  // Initialize shuffle bags (ambient effects cycle as bot background)
  resetEffectShuffle();
  resetPaletteShuffle();

  // Apply saved background style
  setBotBackgroundStyle(botBackgroundStyle);

  // Cloud sync runs non-blocking via pollCloudSync() in the WiFi task.
  // No boot-time TLS — avoids blocking setup() for 10-14s on DNS/connect timeout
  // when WiFi signal is marginal. First sync happens ~2s after WiFi task starts.

  // Initialize StackChan base LED effects + head touch + idle servo
  #if defined(BOARD_HAS_FACES_BASE) || defined(BOARD_HAS_RING_LEDS)
  // boot_sequence.h owns the stack-chan LED bring-up; the Faces strips and the
  // 1.69 ring have no expander to wait for, so they come up here in one call.
  scInitBaseLeds();
  #endif
  #ifdef BOARD_HAS_STACKCHAN_BASE
  scAoStart();   // Active Object core + TouchAO + app AO (ao_core.h)
  scIdleServo.init();
  #endif

  // Enter bot mode
  enterBotMode();

  // Start WiFi server task on Core 0 (render stays on Core 1)
  if (wifiEnabled) {
    startWifiTask();
    initMesh();  // ESP-NOW mesh discovery (requires AP interface active)
  }

  // Cloud sync runs in its own Core 0 task so its blocking TLS work never
  // starves the HTTP server (keeps the control page responsive).
  #ifdef CLOUD_ENABLED
  startCloudTask();
  #endif

  #ifdef VOICE_ENABLED
  startVoiceTask();
  #endif
}

void loop() {
  uint32_t loopStartMs = millis();
  loopStatsBegin();
  #ifdef KALEIDO_SERIAL_STATS
  kaleidoDevSerial();   // bench aid: scene keys + stats over USB serial (kaleido_scene.h)
  #endif
  hangBeatLoopMs = loopStartMs;

  // Only read IMU if it initialized successfully
  if (sysStatus.imuReady) {
    readIMU();
  }

  // Shake detection: sustained shake toggles info mode, quick shake = dizzy
  if (sysStatus.imuReady && botMode.initialized) {
    float mag = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);

    // Track sustained shaking for info mode toggle
    // Acceleration oscillates during shaking, so we allow brief dips below threshold
    if (mag > SHAKE_SUSTAIN_THRESHOLD) {
      if (shakeStartTime == 0) {
        shakeStartTime = millis();
        shakeAboveCount = 0;
      }
      shakeAboveCount++;
      shakeGapStart = 0;  // Any above-threshold frame ends the gap

      // Check if sustained shake threshold met (~2 seconds of shaking)
      if (!shakeSustainedTriggered && shakeAboveCount >= 8 &&
          (millis() - shakeStartTime) >= SUSTAINED_SHAKE_DURATION_MS) {
        shakeSustainedTriggered = true;
        if (botMode.asleep()) {
          // Asleep, a long shake is a jolt, not a gesture: he peeks and sleeps on
          // (holding the screen is what wakes him).
          botMode.peek();
        } else
        #ifdef HAS_LOW_POWER_MODE
        if (lowPowerMode) {
          // Radio off: there is no weather for info mode to show, and a pendant
          // shakes all day. The gesture gets a random reaction on a budget
          // instead — see pendant_shake.h.
          pendantShakeSustained();
        } else
        #endif
        {
          DBGLN("Sustained shake detected — toggling info mode");
          // Toggle info mode
          if (infoMode.active) {
            infoMode.beginExitTransition();
          } else {
            infoMode.beginEnterTransition();
          }
        }
      }
    } else if (shakeStartTime > 0) {
      // Below threshold but we were shaking — allow brief gaps
      if (!shakeGapStart) shakeGapStart = millis();
      if (millis() - shakeGapStart > SHAKE_GAP_TOLERANCE_MS) {
        // Shake truly ended — if it was short, treat as quick shake (dizzy)
        if (!shakeSustainedTriggered && !infoMode.active) {
          bool quiet = false;
          #ifdef HAS_LOW_POWER_MODE
          // Same budget as the sustained reactions: during a cool-off the
          // pendant stays still-faced however much it gets jostled.
          quiet = lowPowerMode && pendantShakeCoolingOff();
          #endif
          if (shakeAboveCount >= 2 && !botMode.shakeReacting && !quiet) {
            botMode.onShake();
          }
        }
        shakeStartTime = 0;
        shakeAboveCount = 0;
        shakeGapStart = 0;
        shakeSustainedTriggered = false;
      }
    }

    #ifdef HAS_LOW_POWER_MODE
    // Drives the dizzy meter's escalation and its settle once shaking stops.
    if (lowPowerMode) pendantShakeUpdate(mag > SHAKE_SUSTAIN_THRESHOLD);
    #endif

    // Regular interaction tracking (unchanged)
    if (mag > 1.3f) {
      botMode.registerInteraction();
    }
  }

  // Handle touch gestures (only if touch initialized)
  #if defined(TOUCH_ENABLED)
  if (sysStatus.touchReady) {
    handleTouch();
  }
  #endif

  // StackChan head touch (Si12T capacitive sensor)
  #ifdef BOARD_HAS_STACKCHAN_BASE
  {
    if (scTouch_state.chillMode && (int32_t)(millis() - scTouch_state.chillEndMs) >= 0) scTouch_state.chillMode = false;
    scProbeTick();   // publish mic / amp / camera / cloud edges first...
    aoDispatch();    // ...then TouchAO reads the head, then the app AO acts on taps
  }
  #ifdef VOICE_ENABLED
  voiceSuppressEvents = scTouch_state.chillMode;
  const bool voiceTurn = voice.busy();
  #else
  const bool voiceTurn = false;
  #endif
  // MotionAO's freeze watchdog stands down during a voice turn (stackchan_base.h).
  scMotionGate(SC_MG_VOICE, voiceTurn);
  plogTick();   // power log: battery lows and a row every 15 s (power_log.h)
  scBatteryCareTick();   // rest the servos, then switch off, as the battery runs down

  // Idle head drift — suppressed during chill mode and voice turns
  // ...and while TouchAO waits for a still head to take its reference.
  const bool touchWantsStill = (int32_t)(scIdleHoldUntilMs - millis()) > 0;
  // ...and while MotionAO runs a gesture, rail cycle or rest.
  if (!scTouch_state.chillMode && !voiceTurn && !partyBlob.code && !touchWantsStill &&   // code mode moves the head itself
      !scMotionBusy()) {
    scIdleServo.update(botMode.personalityIndex, botMode.shakeReacting);
  }
  #endif

  // Auto-cycle ambient effects and palettes (skip while in info mode)
  if (!infoMode.active) {
    if (autoCycle && millis() - lastChange > 20000) {
      lastChange = millis();
      effectIndex = nextPersonalityEffect();
    }
    if (autoCycle && millis() - lastPaletteChange > 5000) {
      lastPaletteChange = millis();
      if (!wledIsSyncing()) {
        paletteIndex = nextPersonalityPalette();
        currentPalette = palettes[paletteIndex];
      }
    }
  }

  // Advance all active tweens (before rendering so values are current)
  tweenManager.update();

  // Advance sound effect sequencer, spectrum analyzer, and proximity (Core S3 only)
  #ifdef HAS_SOUND
  botSounds.update();
  #endif
  #ifdef TARGET_CORES3
  audioSpectrum.update();
  proxLight.update();
  #endif
  #ifdef BUZZER_PIN
  buzzer.update();
  #endif
  #ifdef PWR_HOLD_PIN
  powerButton.update();
  #endif
  #ifdef BAT_ADC_PIN
  batteryAdc.update();
  #endif

  // Apply queued commands from WiFi/touch before rendering
  drainCommandQueue();

  #ifdef HAS_SLEEP_SCHEDULE
  botSleepTick();   // after the queue, so a web "wake" lands this frame
  #endif

  // Sync local palette to WLED when bot sends DDP frames
  int8_t wledPal = wledConsumePalSync();
  if (wledPal >= 0) {
    paletteIndex = (uint8_t)wledPal;
    currentPalette = palettes[paletteIndex];
  }

  // Drive WLED weather display while info mode is active
  static bool prevInfoActive = false;
  if (infoMode.active && !prevInfoActive) {
    wledWeatherViewReset();                    // reset card cycle on entry
  }
  if (!infoMode.active && prevInfoActive && wledIsSyncing()) {
    wledWeatherViewOnExit();                   // linger 2.5s before WLED restores
  }
  prevInfoActive = infoMode.active;

  if (infoMode.active && wledIsSyncing()) {
    wledWeatherViewUpdate();
  }

  // Poll WiFi provisioning state machine (scan results, STA connect, AP linger)
  pollWifiProvisioning();

  // Flush dirty settings to NVS (debounced — waits 2s after last change)
  flushSettingsIfDirty();

  // Auto-brightness from ambient light sensor (Core S3 only)
  #ifdef TARGET_CORES3
  if (autoBrightnessEnabled && sysStatus.proxLightReady) {
    unsigned long now = millis();
    if (now - lastAutoBrightnessMs >= 2000) {
      lastAutoBrightnessMs = now;
      uint16_t lux = proxLight.ambientLux;
      uint8_t newBright;
      if (lux <= 10)       newBright = 3;
      else if (lux <= 100) newBright = map(lux, 10, 100, 5, 15);
      else if (lux <= 500) newBright = map(lux, 100, 500, 15, 35);
      else                 newBright = 50;
      // Only apply if change > 2 to avoid flicker
      if (abs((int)newBright - (int)brightness) > 2) {
        brightness = newBright;
        FastLED.setBrightness(brightness);
      }
    }
  }
  #endif

  // Run the appropriate mode
  if (infoMode.active) {
    runInfoMode();
  } else {
    runBotMode();
  }

  // Update StackChan base LED ring effects
  #ifdef HAS_BASE_LEDS
  {
    uint32_t t0 = micros();
    scLeds.update();
    uint32_t us = micros() - t0;
    if (us > scLeds.updateMaxUs) scLeds.updateMaxUs = us;
  }
  #endif

  #ifdef BOARD_HAS_FACES_BASE
  // START+SELECT held: the same grip that leaves the game console.
  facesChordPoll();
  #endif

  loopStatsWorkDone();
  // Deadline pacing: wait out only what is left of the frame, so a slow frame
  // isn't made slower by a fixed delay. delay(1) at least, so the idle task
  // on this core still runs.
  uint32_t workMs = millis() - loopStartMs;
  delay(workMs + 1 < BOT_FRAME_PERIOD_MS ? BOT_FRAME_PERIOD_MS - workMs : 1);
}
