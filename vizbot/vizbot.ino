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
#include "SensorQMI8658.hpp"

#include "config.h"
#include "palettes.h"
#include "effects_ambient.h"
#include "display_lcd.h"
#include "bot_mode.h"
#include "web_server.h"
#if defined(TOUCH_ENABLED)
#include "touch_control.h"
#endif

// Global objects
CRGB leds[NUM_LEDS];
SensorQMI8658 imu;
WebServer server(80);
bool wifiEnabled = false;

// State variables
uint8_t effectIndex = 0;
uint8_t paletteIndex = 0;
uint8_t brightness = DEFAULT_BRIGHTNESS;
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
  if (imu.getDataReady()) {
    imu.getAccelerometer(accelX, accelY, accelZ);
    imu.getGyroscope(gyroX, gyroY, gyroZ);
  }
}

// Start or restart the WiFi AP hotspot
void startWifiAP() {
  WiFi.disconnect(true);   // Clear any stale connection state
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);    // Keep radio awake
  delay(200);
  bool apStarted = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);
  DBGLN("--- WiFi AP Setup ---");
  DBG("softAP returned: ");
  DBGLN(apStarted ? "YES" : "NO");
  DBG("AP SSID: ");
  DBGLN(WIFI_SSID);
  DBG("AP IP: ");
  DBGLN(WiFi.softAPIP());
  DBG("AP MAC: ");
  DBGLN(WiFi.softAPmacAddress());
  delay(500);
  if (!wifiEnabled) {
    setupWebServer();
    DBGLN("Web server started");
  }
  wifiEnabled = true;
}

// Stop the WiFi AP hotspot
void stopWifiAP() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiEnabled = false;
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
  delay(500);  // Give ESP32-S3 time to stabilize before init
  DBGLN("\n=== vizBot starting ===");

  // Initialize LEDs (needed for the leds[] buffer used by ambient effects)
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);

  // Initialize LCD
  initLCD();

  // Run intro animation
  introAnimation();

  // Start WiFi AP hotspot (after hardware init so radio has time)
  startWifiAP();

  // Initialize IMU (for shake/motion detection)
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

  // Initialize shuffle bags (ambient effects cycle as bot background)
  resetEffectShuffle();
  resetPaletteShuffle();

  // Enter bot mode
  enterBotMode();
}

void loop() {
  if (wifiEnabled) {
    server.handleClient();
  }

  readIMU();

  // Shake reaction for bot
  if (botMode.initialized) {
    float mag = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
    if (mag > SHAKE_THRESHOLD && !botMode.shakeReacting) {
      botMode.onShake();
    }
    if (mag > 1.3f) {
      botMode.registerInteraction();
    }
  }

  // Handle touch gestures
  #if defined(TOUCH_ENABLED)
    handleTouch();
  #endif

  // Auto-cycle ambient effects and palettes (for bot background overlay)
  if (autoCycle && millis() - lastChange > 20000) {
    lastChange = millis();
    effectIndex = nextShuffledEffect();
  }
  if (autoCycle && millis() - lastPaletteChange > 5000) {
    lastPaletteChange = millis();
    paletteIndex = nextShuffledPalette();
    currentPalette = palettes[paletteIndex];
  }

  // Run bot mode (handles its own LCD rendering)
  runBotMode();

  delay(BOT_FRAME_DELAY_MS);
}
