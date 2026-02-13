/*
 * vizPow — ESP32-S3 Controller
 * 
 * A motion-reactive LED matrix controller for wearable displays.
 * Uses Waveshare ESP32-S3-Matrix with onboard 8x8 WS2812B and QMI8658 IMU.
 * 
 * Features:
 * - 12 motion-reactive effects (accelerometer/gyroscope driven)
 * - 15 ambient effects (no motion input)
 * - 15 color palettes
 * - WiFi AP web interface for control
 * - Adjustable brightness and speed
 * 
 * Hardware:
 * - Waveshare ESP32-S3-Matrix
 * - 8x8 WS2812B LED Matrix (GPIO14)
 * - QMI8658 6-axis IMU (I2C: SDA=GPIO11, SCL=GPIO12)
 * 
 * License: MIT
 */

#include <FastLED.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "SensorQMI8658.hpp"

#include "config.h"
#if defined(AUTO_WIFI_USB_DETECT)
  #include "hal/usb_serial_jtag_ll.h"
#endif
#include "palettes.h"
#include "effects_motion.h"
#include "effects_ambient.h"
#include "effects_emoji.h"
#include "display_lcd.h"
#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
#include "bot_mode.h"
#endif
#include "web_server.h"
#if defined(TOUCH_ENABLED)
#include "touch_control.h"
#endif

// Global objects
CRGB leds[NUM_LEDS];
SensorQMI8658 imu;
WebServer server(80);
Preferences preferences;
bool wifiEnabled = false;

// WiFi STA credentials (loaded from NVS, fallback to config.h defaults)
char wifiStaSSID[33] = "";
char wifiStaPassword[65] = "";
bool staConnected = false;
unsigned long lastNTPRetry = 0;
bool ntpSynced = false;

// State variables
uint8_t effectIndex = 0;
uint8_t paletteIndex = 0;
uint8_t brightness = DEFAULT_BRIGHTNESS;
uint8_t speed = 20;
bool autoCycle = true;
uint8_t currentMode = MODE_AMBIENT;
unsigned long lastChange = 0;
unsigned long lastPaletteChange = 0;

// IMU data
float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;

// Shake detection state
unsigned long shakeTimestamps[SHAKE_COUNT];
uint8_t shakeIndex = 0;
unsigned long lastModeChange = 0;
bool wasShaking = false;  // Track if we were above threshold last frame

// Shuffle bags for random-without-repeats cycling
// Use 16 (NUM_AMBIENT_EFFECTS) as it's the larger of the two
uint8_t effectShuffleBag[16];
uint8_t effectShufflePos = 0;
uint8_t effectShuffleSize = 0;

uint8_t paletteShuffleBag[NUM_PALETTES];
uint8_t paletteShufflePos = 0;

// Current palette
CRGBPalette16 currentPalette;

// IMU power profile tracking (used when POWER_SAVE_ENABLED)
#if defined(POWER_SAVE_ENABLED)
enum IMUProfile { IMU_FULL, IMU_LOW_POWER };
IMUProfile currentIMUProfile = IMU_FULL;
#endif

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
  // Bot mode has no effect shuffling
  if (currentMode == MODE_BOT) { effectShuffleSize = 1; effectShufflePos = 0; return; }
  effectShuffleSize = (currentMode == MODE_MOTION) ? NUM_MOTION_EFFECTS : NUM_AMBIENT_EFFECTS;
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

void setup() {
  Serial.begin(115200);
  delay(100);

  // Determine whether to enable WiFi
  #if defined(AUTO_WIFI_USB_DETECT)
    // Wait for USB host to enumerate, then check for SOF frames
    delay(USB_DETECT_DELAY_MS);
    wifiEnabled = usb_serial_jtag_ll_txfifo_writable();
    DBGLN(wifiEnabled ? "USB detected - WiFi ON" : "Battery mode - WiFi OFF");
  #else
    wifiEnabled = true;  // LCD target: always enable WiFi
  #endif

  if (wifiEnabled) {
    // Load saved WiFi STA credentials from NVS (fallback to config.h defaults)
    preferences.begin("vizpow", true);  // read-only
    String savedSSID = preferences.getString("sta_ssid", WIFI_STA_SSID);
    String savedPass = preferences.getString("sta_pass", WIFI_STA_PASSWORD);
    preferences.end();
    savedSSID.toCharArray(wifiStaSSID, sizeof(wifiStaSSID));
    savedPass.toCharArray(wifiStaPassword, sizeof(wifiStaPassword));

    // AP+STA: keep web server AP and connect to home network for NTP
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    bool apStarted = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);
    #if defined(WIFI_TX_POWER)
      WiFi.setTxPower(WIFI_TX_POWER);
    #endif
    DBG("AP Started: ");
    DBGLN(apStarted ? "YES" : "NO");
    DBG("AP SSID: ");
    DBGLN(WIFI_SSID);
    DBG("AP IP: ");
    DBGLN(WiFi.softAPIP());

    // Connect to home network for NTP
    if (strlen(wifiStaSSID) > 0) {
      WiFi.begin(wifiStaSSID, wifiStaPassword);
      DBG("Connecting to ");
      DBG(wifiStaSSID);
      int tries = 0;
      while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(300);
        DBG(".");
        tries++;
      }
      if (WiFi.status() == WL_CONNECTED) {
        staConnected = true;
        DBG("\nConnected! IP: ");
        DBGLN(WiFi.localIP());
        configTime(NTP_GMT_OFFSET, NTP_DAYLIGHT_OFFSET, NTP_SERVER);
        DBGLN("NTP time sync started");

        // Wait briefly for NTP to actually sync
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 3000)) {
          ntpSynced = true;
          DBGLN("NTP synced!");
        } else {
          DBGLN("NTP pending (will retry in loop)");
        }
      } else {
        DBGLN("\nHome network connection failed - will retry in background");
      }
    } else {
      DBGLN("No WiFi STA SSID configured");
    }

    setupWebServer();
    DBGLN("Web server started");
  } else {
    WiFi.mode(WIFI_OFF);
  }

  // Initialize LEDs (always needed for the leds[] buffer)
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  #if defined(MAX_LED_POWER_MA)
    FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_LED_POWER_MA);
  #endif

  // Initialize LCD if enabled
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
    initLCD();
  #endif

  // Run intro animation
  introAnimation();

  // Initialize IMU
  Wire.begin(I2C_SDA, I2C_SCL);

  if (imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
    imu.configAccelerometer(
      SensorQMI8658::ACC_RANGE_4G,
      SensorQMI8658::ACC_ODR_250Hz,
      SensorQMI8658::LPF_MODE_0
    );
    imu.configGyroscope(
      SensorQMI8658::GYR_RANGE_512DPS,
      SensorQMI8658::GYR_ODR_896_8Hz,
      SensorQMI8658::LPF_MODE_0
    );
    imu.enableAccelerometer();
    imu.enableGyroscope();
    DBGLN("IMU initialized");
  } else {
    DBGLN("IMU initialization failed");
  }

  // Initialize touch controller (shares I2C bus with IMU)
  #if defined(TOUCH_ENABLED)
    initTouch();
  #endif

  // Set initial palette
  currentPalette = palettes[0];

  // Initialize shake detection
  for (uint8_t i = 0; i < SHAKE_COUNT; i++) {
    shakeTimestamps[i] = 0;
  }

  // Initialize shuffle bags
  resetEffectShuffle();
  resetPaletteShuffle();
}

// Switch IMU between full (motion mode) and low-power (ambient/emoji)
#if defined(POWER_SAVE_ENABLED)
void updateIMUForMode() {
  IMUProfile target = (currentMode == MODE_MOTION) ? IMU_FULL : IMU_LOW_POWER;
  if (target == currentIMUProfile) return;

  if (target == IMU_FULL) {
    imu.enableGyroscope();
  } else {
    imu.disableGyroscope();
  }
  currentIMUProfile = target;
}
#endif

void readIMU() {
  if (imu.getDataReady()) {
    imu.getAccelerometer(accelX, accelY, accelZ);
    #if defined(POWER_SAVE_ENABLED)
      if (currentIMUProfile == IMU_FULL) {
        imu.getGyroscope(gyroX, gyroY, gyroZ);
      } else {
        gyroX = gyroY = gyroZ = 0;
      }
    #else
      imu.getGyroscope(gyroX, gyroY, gyroZ);
    #endif
  }
}

// Check for shake gesture to change mode
void checkModeShake() {
  unsigned long now = millis();

  // Skip if in cooldown period
  if (now - lastModeChange < SHAKE_COOLDOWN_MS) {
    return;
  }

  // Calculate acceleration magnitude (subtract 1g for gravity)
  float magnitude = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  bool isShaking = magnitude > SHAKE_THRESHOLD;

  // Detect shake event (transition from not-shaking to shaking)
  if (isShaking && !wasShaking) {
    // Record this shake timestamp
    shakeTimestamps[shakeIndex] = now;
    shakeIndex = (shakeIndex + 1) % SHAKE_COUNT;

    // Count shakes within the window
    uint8_t validShakes = 0;
    for (uint8_t i = 0; i < SHAKE_COUNT; i++) {
      if (now - shakeTimestamps[i] < SHAKE_WINDOW_MS) {
        validShakes++;
      }
    }

    // If enough shakes, change mode
    if (validShakes >= SHAKE_COUNT) {
      // Cycle to next mode: MOTION -> AMBIENT -> EMOJI -> BOT -> MOTION
      #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
        uint8_t nextMode = (currentMode + 1) % NUM_MODES;
      #else
        // Non-LCD targets skip Bot Mode
        uint8_t nextMode = (currentMode + 1) % 3;
      #endif

      // Handle mode-specific entry logic
      if (nextMode == MODE_EMOJI && emojiQueueCount == 0) {
        addRandomEmojis(RANDOM_EMOJI_COUNT);
      }

      #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
        // Exit bot mode if leaving it
        if (currentMode == MODE_BOT) {
          exitBotMode();
        }
        // Enter bot mode if entering it
        if (nextMode == MODE_BOT) {
          enterBotMode();
        }
      #endif

      currentMode = nextMode;
      effectIndex = 0;
      lastChange = now;
      lastModeChange = now;
      resetEffectShuffle();  // Reshuffle for the new mode's effect count
      FastLED.clear();

      // Clear shake timestamps to prevent immediate re-trigger
      for (uint8_t i = 0; i < SHAKE_COUNT; i++) {
        shakeTimestamps[i] = 0;
      }

      // Brief flash to indicate mode change
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(50, 50, 50);
      }
      showDisplay();
      delay(100);
      FastLED.clear();
    }
  }

  wasShaking = isShaking;
}

void loop() {
  if (wifiEnabled) {
    server.handleClient();

    // Background WiFi STA reconnect + NTP retry
    if (!staConnected && strlen(wifiStaSSID) > 0 && WiFi.status() != WL_CONNECTED) {
      unsigned long now = millis();
      if (now - lastNTPRetry > 30000) {  // Retry every 30s
        lastNTPRetry = now;
        WiFi.begin(wifiStaSSID, wifiStaPassword);
      }
    }
    if (!staConnected && WiFi.status() == WL_CONNECTED) {
      staConnected = true;
      configTime(NTP_GMT_OFFSET, NTP_DAYLIGHT_OFFSET, NTP_SERVER);
      DBGLN("WiFi STA reconnected + NTP started");
    }
    if (staConnected && !ntpSynced) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 0)) {
        ntpSynced = true;
        DBGLN("NTP synced!");
      }
    }
  }
  readIMU();
  #if defined(POWER_SAVE_ENABLED)
    updateIMUForMode();
  #endif
  checkModeShake();  // Check for shake gesture to change mode

  // Bot mode: single shake reaction (separate from mode-change shake)
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
  if (currentMode == MODE_BOT && botMode.initialized) {
    float mag = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
    if (mag > SHAKE_THRESHOLD && !botMode.shakeReacting) {
      botMode.onShake();
    }
    // Also register any motion as interaction for idle timer
    if (mag > 1.3f) {
      botMode.registerInteraction();
    }
  }
  #endif

  // Handle touch gestures
  #if defined(TOUCH_ENABLED)
    handleTouch();
  #endif

  // Only do effect/palette cycling for Motion and Ambient modes
  if (currentMode != MODE_EMOJI && currentMode != MODE_BOT) {
    int maxEffects = (currentMode == MODE_MOTION) ? NUM_MOTION_EFFECTS : NUM_AMBIENT_EFFECTS;

    // Auto cycle effects (ambient effects get double time)
    unsigned long cycleTime = (currentMode == MODE_MOTION) ? 10000 : 20000;
    if (autoCycle && millis() - lastChange > cycleTime) {
      lastChange = millis();
      effectIndex = nextShuffledEffect();
      FastLED.clear();
    }

    // Auto cycle palettes
    if (autoCycle && millis() - lastPaletteChange > 5000) {
      lastPaletteChange = millis();
      paletteIndex = nextShuffledPalette();
      currentPalette = palettes[paletteIndex];
    }
  }

  // Run current effect based on mode
  switch (currentMode) {
    case MODE_MOTION:
      runMotionEffect(effectIndex);
      break;
    case MODE_AMBIENT:
      runAmbientEffect(effectIndex);
      break;
    case MODE_EMOJI:
      runEmojiEffect();
      break;
    #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
    case MODE_BOT:
      runBotMode();
      break;
    #endif
  }

  // Bot mode handles its own LCD rendering, skip normal display pipeline
  if (currentMode != MODE_BOT) {
    showDisplay();
  }

  // Frame timing: power-save uses adaptive delays with FPS caps,
  // full-power just uses the speed setting directly
  #if defined(POWER_SAVE_ENABLED)
    int frameDelay;
    switch (currentMode) {
      case MODE_EMOJI:
        frameDelay = emojiFading ? FRAME_DELAY_EMOJI_FADING : FRAME_DELAY_EMOJI_STATIC;
        break;
      case MODE_AMBIENT:
        frameDelay = max((int)speed, FRAME_DELAY_AMBIENT_MIN);
        break;
      default:  // MODE_MOTION
        frameDelay = speed;
        break;
    }

    // Chunk long delays into 30ms segments so shake detection stays responsive
    while (frameDelay > 30) {
      delay(30);
      frameDelay -= 30;
      if (wifiEnabled) server.handleClient();
      readIMU();
      checkModeShake();
    }
    delay(frameDelay);
  #else
    // Bot mode uses its own frame timing for smooth ~30fps
    if (currentMode == MODE_BOT) {
      delay(BOT_FRAME_DELAY_MS);
    } else {
      delay(speed);
    }
  #endif
}
