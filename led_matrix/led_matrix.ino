/*
 * ESP32-S3 LED Matrix Controller
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
#include "SensorQMI8658.hpp"

#include "config.h"
#include "palettes.h"
#include "effects_motion.h"
#include "effects_ambient.h"
#include "effects_emoji.h"
#include "web_server.h"
#include "display_lcd.h"
#if defined(TOUCH_ENABLED)
#include "touch_control.h"
#endif

// Global objects
CRGB leds[NUM_LEDS];
SensorQMI8658 imu;
WebServer server(80);

// State variables
uint8_t effectIndex = 0;
uint8_t paletteIndex = 0;
uint8_t brightness = 15;
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
  while (millis() - startTime < 2000) {  // Run for 2 seconds
    fadeToBlackBy(leds, NUM_LEDS, 20);
    int pos = random16(NUM_LEDS);
    leds[pos] = CHSV(random8(), 255, 255);  // Random rainbow colors
    showDisplay();
    delay(20);
  }
  FastLED.clear();
  showDisplay();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Start WiFi AP FIRST (before other peripherals)
  WiFi.mode(WIFI_AP);
  delay(100);
  bool apStarted = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);
  Serial.print("AP Started: ");
  Serial.println(apStarted ? "YES" : "NO");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  // Setup web server
  setupWebServer();
  Serial.println("Web server started");

  // Initialize LEDs (always needed for the leds[] buffer)
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);

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
    Serial.println("IMU initialized");
  } else {
    Serial.println("IMU initialization failed");
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

void readIMU() {
  if (imu.getDataReady()) {
    imu.getAccelerometer(accelX, accelY, accelZ);
    imu.getGyroscope(gyroX, gyroY, gyroZ);
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
      // Cycle to next mode: MOTION -> AMBIENT -> EMOJI -> MOTION
      uint8_t nextMode = (currentMode + 1) % 3;

      // If entering emoji mode, ensure there's a sequence
      if (nextMode == MODE_EMOJI && emojiQueueCount == 0) {
        addRandomEmojis(RANDOM_EMOJI_COUNT);
      }

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
  server.handleClient();
  readIMU();
  checkModeShake();  // Check for shake gesture to change mode

  // Handle touch gestures
  #if defined(TOUCH_ENABLED)
    handleTouch();
  #endif

  // Only do effect/palette cycling for Motion and Ambient modes
  if (currentMode != MODE_EMOJI) {
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
  }

  showDisplay();
  delay(speed);
}
