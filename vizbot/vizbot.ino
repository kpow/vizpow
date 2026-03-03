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
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#ifdef TARGET_CORES3
#include <M5Unified.h>
#else
#include "SensorQMI8658.hpp"
#endif

#include "config.h"
#include "device_id.h"  // Per-device unique SSID + mDNS hostname (from eFuse MAC)
#include "palettes.h"
#include "display_lcd.h"    // Must come before any file that calls gfx->methods() (defines DisplayProxy)
#include "tween.h"          // Tween animation system (must come before bot_mode.h)
#include "effects_ambient.h"
#include "bot_mode.h"
#include "info_mode.h"
#include "wled_display.h"
#include "wled_weather_view.h"
#ifdef CLOUD_ENABLED
#include <ArduinoJson.h>
#include "content_cache.h"
#include "cloud_client.h"
#endif
#include "web_server.h"
#include "settings.h"
#if defined(TOUCH_ENABLED)
#include "touch_control.h"
#endif
#include "task_manager.h"
#include "wifi_provisioning.h"
#include "boot_sequence.h"

// Global objects
CRGB leds[NUM_LEDS];
#ifndef TARGET_CORES3
SensorQMI8658 imu;
#endif
WebServer server(80);
DNSServer dnsServer;
bool wifiEnabled = false;

// State variables
uint8_t effectIndex = 0;
uint8_t paletteIndex = 0;
uint8_t brightness = DEFAULT_BRIGHTNESS;
uint8_t lcdBrightness = 255;
uint8_t speed = 20;
bool autoCycle = true;
uint8_t currentMode = MODE_BOT;

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

// Sustained shake tracking (for info mode toggle)
unsigned long shakeStartTime = 0;        // When continuous shaking began
bool shakeSustainedTriggered = false;    // Whether sustained shake already fired
uint8_t shakeAboveCount = 0;            // Total frames above threshold
uint8_t shakeGapFrames = 0;             // Frames below threshold (grace period)
#define SHAKE_GAP_TOLERANCE 6           // Allow up to 6 frames (~200ms) below threshold

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
  // BMI270 via M5Unified — no I2C mutex needed (M5Unified manages internally)
  M5.Imu.getAccel(&accelX, &accelY, &accelZ);
  M5.Imu.getGyro(&gyroX, &gyroY, &gyroZ);
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
  WiFi.setTxPower(WIFI_TX_POWER);

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
  Serial.begin(115200);
  delay(500);

  DBGLN("\n=== vizBot starting ===");
  initDeviceID();   // Compute unique apSSID / mdnsHostname from eFuse MAC

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
  M5.begin(cfg);
  #endif

  // Initialize task infrastructure (I2C mutex + command queue)
  initTaskManager();

  // Initialize LCD first — we need it to show the boot screen
  initLCD();

  // Run the visual boot sequence (initializes all subsystems with onscreen feedback)
  // This handles: LEDs, I2C, IMU, Touch, WiFi AP, Web Server
  runBootSequence();

  // Load persistent settings from NVS (brightness, palette, etc.)
  loadSettings();

  // Load WLED display settings from NVS
  loadWledSettings();

  // Apply loaded settings to hardware
  FastLED.setBrightness(brightness);
  setLCDBacklight(lcdBrightness);

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

  // Enter bot mode
  enterBotMode();

  // Start WiFi server task on Core 0 (render stays on Core 1)
  if (wifiEnabled) {
    startWifiTask();
  }

  // Cloud sync now runs inside wifiServerTask via pollCloudSync() —
  // no separate task needed. initCloudClient() already called above.
}

void loop() {
  // Web server runs in its own FreeRTOS task on Core 0 — no handleClient() here

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
      shakeGapFrames = 0;  // Reset gap counter on any above-threshold frame

      // Check if sustained shake threshold met (~2 seconds of shaking)
      if (!shakeSustainedTriggered && shakeAboveCount >= 8 &&
          (millis() - shakeStartTime) >= SUSTAINED_SHAKE_DURATION_MS) {
        shakeSustainedTriggered = true;
        DBGLN("Sustained shake detected — toggling info mode");
        // Toggle info mode
        if (infoMode.active) {
          infoMode.beginExitTransition();
        } else {
          infoMode.beginEnterTransition();
        }
      }
    } else if (shakeStartTime > 0) {
      // Below threshold but we were shaking — allow brief gaps
      shakeGapFrames++;
      if (shakeGapFrames > SHAKE_GAP_TOLERANCE) {
        // Shake truly ended — if it was short, treat as quick shake (dizzy)
        if (!shakeSustainedTriggered && !infoMode.active) {
          if (shakeAboveCount >= 2 && !botMode.shakeReacting) {
            botMode.onShake();
          }
        }
        shakeStartTime = 0;
        shakeAboveCount = 0;
        shakeGapFrames = 0;
        shakeSustainedTriggered = false;
      }
    }

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

  // Auto-cycle ambient effects and palettes (skip while in info mode)
  if (!infoMode.active) {
    if (autoCycle && millis() - lastChange > 20000) {
      lastChange = millis();
      effectIndex = nextShuffledEffect();
    }
    if (autoCycle && millis() - lastPaletteChange > 5000) {
      lastPaletteChange = millis();
      if (!wledIsSyncing()) {
        paletteIndex = nextShuffledPalette();
        currentPalette = palettes[paletteIndex];
      }
    }
  }

  // Advance all active tweens (before rendering so values are current)
  tweenManager.update();

  // Apply queued commands from WiFi/touch before rendering
  drainCommandQueue();

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

  // Run the appropriate mode
  if (infoMode.active) {
    runInfoMode();
  } else {
    runBotMode();
  }

  delay(BOT_FRAME_DELAY_MS);
}
