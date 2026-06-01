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
uint8_t currentMode = MODE_AMBIENT;
unsigned long lastChange = 0;
unsigned long lastPaletteChange = 0;

// IMU data
float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;

// Shake detection state (short vs long shake)
unsigned long shakeStartMs = 0;        // when the current shake burst began (0 = idle)
uint16_t      shakeAboveFrames = 0;    // above-sustain polls this burst
uint8_t       shakeGapFrames = 0;      // consecutive below-sustain polls (burst-end detect)
bool          longShakeFired = false;  // long shake already handled this burst
bool          peakShakeSeen = false;   // a real peak (> SHAKE_THRESHOLD) occurred this burst
unsigned long lastShakeActionMs = 0;   // debounce between shake actions
bool          firstShakePending = true;// next Ambient short shake toggles the kaleidoscope

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
    WiFi.mode(WIFI_AP);
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

  // Shake detection state is initialized at declaration.

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
// Quick LED wipe across the matrix to signal a mode change (long shake).
void modeChangeSweep() {
  FastLED.clear();
  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      leds[XY(x, y)] = CRGB(40, 40, 70);
    }
    showDisplay();
    delay(22);
  }
  FastLED.clear();
  showDisplay();
}

// Enter a mode fresh: randomize what shows, kaleido off, arm the kaleido toggle.
void enterModeRandom(uint8_t mode) {
  currentMode = mode;
  resetEffectShuffle();              // size the shuffle bag for the new mode
  kaleidoscopeMode = KSCOPE_OFF;
  firstShakePending = true;          // first short shake on the new pattern toggles kaleido
  if (mode == MODE_EMOJI) {
    emojiQueueCount = 0;
    addRandomEmojis(RANDOM_EMOJI_COUNT);   // random icons
  } else {
    effectIndex = nextShuffledEffect();    // random starting effect
  }
  lastChange = millis();
}

// Advance to the next item within the current mode (short-shake browse).
void advanceWithinMode() {
  if (currentMode == MODE_EMOJI) {
    emojiQueueCount = 0;
    addRandomEmojis(RANDOM_EMOJI_COUNT);   // fresh random emoji set
  } else {
    int count = (currentMode == MODE_MOTION) ? NUM_MOTION_EFFECTS : NUM_AMBIENT_EFFECTS;
    effectIndex = (effectIndex + 1) % count;
  }
  lastChange = millis();
  FastLED.clear();
}

// Short shake: first short shake on an Ambient pattern toggles the kaleidoscope;
// otherwise advance to the next item (the new item arrives plain, kaleido armed).
void doShortShake() {
  if (currentMode == MODE_AMBIENT && firstShakePending) {
    kaleidoscopeMode = (kaleidoscopeMode == KSCOPE_OFF) ? KSCOPE_6SLICE : KSCOPE_OFF;
    firstShakePending = false;
  } else {
    advanceWithinMode();
    kaleidoscopeMode = KSCOPE_OFF;
    firstShakePending = true;        // re-arm the kaleido toggle for the new item
  }
}

// Long shake: change to the next top-level mode with a quick LED sweep.
void doLongShake() {
  modeChangeSweep();
  enterModeRandom((currentMode + 1) % NUM_MODES);
}

// Poll the IMU magnitude and classify shakes as short (quick burst) or
// long (sustained >= LONG_SHAKE_MS). Called several times per frame.
void checkModeShake() {
  unsigned long now = millis();
  float mag = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  bool active = mag > SHAKE_SUSTAIN_THRESHOLD;

  if (active) {
    if (shakeStartMs == 0) {          // a new burst begins
      shakeStartMs       = now;
      shakeAboveFrames   = 0;
      shakeGapFrames     = 0;
      longShakeFired     = false;
      peakShakeSeen      = false;
    }
    if (shakeAboveFrames < 0xFFFF) shakeAboveFrames++;
    shakeGapFrames = 0;
    if (mag > SHAKE_THRESHOLD) peakShakeSeen = true;

    // Long shake: held past the duration threshold (fires once per burst).
    if (!longShakeFired && peakShakeSeen &&
        (now - shakeStartMs) >= LONG_SHAKE_MS &&
        (now - lastShakeActionMs) >= SHAKE_ACTION_COOLDOWN_MS) {
      longShakeFired    = true;
      lastShakeActionMs = now;
      doLongShake();
    }
  } else if (shakeStartMs != 0) {     // maybe the end of a burst
    shakeGapFrames++;
    if (shakeGapFrames > SHAKE_GAP_TOLERANCE) {
      // Burst ended. A real, short burst (not already a long shake) = short shake.
      if (!longShakeFired && peakShakeSeen &&
          shakeAboveFrames >= SHORT_SHAKE_MIN_FRAMES &&
          (now - lastShakeActionMs) >= SHAKE_ACTION_COOLDOWN_MS) {
        lastShakeActionMs = now;
        doShortShake();
      }
      shakeStartMs     = 0;
      shakeAboveFrames = 0;
      shakeGapFrames   = 0;
      longShakeFired   = false;
      peakShakeSeen    = false;
    }
  }
}

void loop() {
  if (wifiEnabled) {
    server.handleClient();
  }
  readIMU();
  #if defined(POWER_SAVE_ENABLED)
    updateIMUForMode();
  #endif
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
    delay(speed);
  #endif
}
