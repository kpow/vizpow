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
#include "web_server.h"

// Global objects
CRGB leds[NUM_LEDS];
SensorQMI8658 imu;
WebServer server(80);

// State variables
uint8_t effectIndex = 0;
uint8_t paletteIndex = 0;
uint8_t brightness = 15;
uint8_t speed = 20;
bool autoCycle = false;
bool motionMode = true;
unsigned long lastChange = 0;
unsigned long lastPaletteChange = 0;

// IMU data
float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;

// Current palette
CRGBPalette16 currentPalette;

// Color test at startup
void colorTest() {
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
  delay(500);
  
  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(500);
  
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(500);
  
  FastLED.clear();
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  
  // Initialize LEDs
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  
  // Run color test
  colorTest();
  
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
  
  // Set initial palette
  currentPalette = palettes[0];
  
  // Start WiFi AP
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Access Point Started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  // Setup web server
  setupWebServer();
  Serial.println("Web server started");
}

void readIMU() {
  if (imu.getDataReady()) {
    imu.getAccelerometer(accelX, accelY, accelZ);
    imu.getGyroscope(gyroX, gyroY, gyroZ);
  }
}

void loop() {
  server.handleClient();
  readIMU();
  
  int maxEffects = motionMode ? NUM_MOTION_EFFECTS : NUM_AMBIENT_EFFECTS;
  
  // Auto cycle effects
  if (autoCycle && millis() - lastChange > 10000) {
    lastChange = millis();
    effectIndex = (effectIndex + 1) % maxEffects;
    FastLED.clear();
  }
  
  // Auto cycle palettes
  if (autoCycle && millis() - lastPaletteChange > 5000) {
    lastPaletteChange = millis();
    paletteIndex = (paletteIndex + 1) % NUM_PALETTES;
    currentPalette = palettes[paletteIndex];
  }

  // Run current effect
  if (motionMode) {
    runMotionEffect(effectIndex);
  } else {
    runAmbientEffect(effectIndex);
  }

  FastLED.show();
  delay(speed);
}
