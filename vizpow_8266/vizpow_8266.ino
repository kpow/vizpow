/*
 * vizPow — ESP8266 Controller
 *
 * A WiFi-controlled LED matrix for 8x8 WS2812B grids.
 * Port of the ESP32 version, stripped of IMU/touch/LCD hardware.
 *
 * Features:
 * - 13 ambient effects (pure FastLED)
 * - 41 emoji sprites
 * - 15 color palettes
 * - WiFi AP web interface for control
 * - Adjustable brightness and speed
 *
 * Hardware:
 * - ESP8266EX module (4MB flash)
 * - 8x8 WS2812B LED Matrix (GPIO2)
 *
 * License: MIT
 */

#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include "config.h"
#include "palettes.h"
#include "effects_ambient.h"
#include "effects_emoji.h"
#include "web_server.h"

// Global objects
CRGB leds[NUM_LEDS];
ESP8266WebServer server(80);

// State variables
uint8_t effectIndex = 0;
uint8_t paletteIndex = 0;
uint8_t brightness = 15;
uint8_t speed = 20;
bool autoCycle = true;
uint8_t currentMode = MODE_AMBIENT;
unsigned long lastChange = 0;
unsigned long lastPaletteChange = 0;

// Shuffle bags for random-without-repeats cycling
uint8_t effectShuffleBag[NUM_AMBIENT_EFFECTS];
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

// Sparkle intro animation at startup
void introAnimation() {
  unsigned long startTime = millis();
  while (millis() - startTime < 2000) {
    fadeToBlackBy(leds, NUM_LEDS, 20);
    int pos = random16(NUM_LEDS);
    leds[pos] = CHSV(random8(), 255, 255);
    FastLED.show();
    delay(20);
  }
  FastLED.clear();
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Start WiFi AP
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

  // Initialize LEDs
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);

  // Run intro animation
  introAnimation();

  // Set initial palette
  currentPalette = palettes[0];

  // Initialize shuffle bags
  resetEffectShuffle();
  resetPaletteShuffle();
}

void loop() {
  server.handleClient();

  // Only do effect/palette cycling for Ambient mode
  if (currentMode == MODE_AMBIENT) {
    // Auto cycle effects (ambient effects get 20s each)
    if (autoCycle && millis() - lastChange > 20000) {
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
    case MODE_AMBIENT:
      runAmbientEffect(effectIndex);
      break;
    case MODE_EMOJI:
      runEmojiEffect();
      break;
  }

  FastLED.show();
  delay(speed);
}
