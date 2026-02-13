#ifndef EFFECTS_MOTION_H
#define EFFECTS_MOTION_H

#include <FastLED.h>
#include "config.h"

// External references to globals defined in main sketch
extern CRGB leds[];
extern CRGBPalette16 currentPalette;
extern float accelX, accelY, accelZ;
extern float gyroX, gyroY, gyroZ;

// Motion sensitivity settings - adjust these to tune responsiveness
#define ACCEL_SENSITIVITY 2.5    // Multiplier for accelerometer effects (higher = more responsive)
#define GYRO_SENSITIVITY 3.0     // Multiplier for gyroscope effects (higher = more responsive)
#define SHAKE_THRESHOLD_LOW 1.2  // Lower threshold for shake detection (was 1.5)
#define SHAKE_THRESHOLD_HIGH 1.8 // Higher threshold for big shakes (was 2.5)

void tiltBall() {
  static float ballX = 3.5, ballY = 3.5;

  // More responsive: larger range and faster interpolation
  // Swapped X/Y axes to match device orientation
  float targetX = 3.5 + accelY * 5.0 * ACCEL_SENSITIVITY;
  float targetY = 3.5 + accelX * 5.0 * ACCEL_SENSITIVITY;

  ballX += (targetX - ballX) * 0.5;  // Faster response (was 0.3)
  ballY += (targetY - ballY) * 0.5;
  
  ballX = constrain(ballX, 0, 7);
  ballY = constrain(ballY, 0, 7);
  
  fadeToBlackBy(leds, NUM_LEDS, 100);
  
  int ix = (int)ballX;
  int iy = (int)ballY;
  
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      int nx = ix + dx;
      int ny = iy + dy;
      if (nx >= 0 && nx < MATRIX_WIDTH && ny >= 0 && ny < MATRIX_HEIGHT) {
        float dist = sqrt((ballX - nx) * (ballX - nx) + (ballY - ny) * (ballY - ny));
        uint8_t bright = 255 - constrain(dist * 150, 0, 255);
        leds[XY(nx, ny)] = ColorFromPalette(currentPalette, millis() / 20, bright);
      }
    }
  }
}

void motionPlasma() {
  static uint16_t t = 0;
  float motion = sqrt(gyroX * gyroX + gyroY * gyroY + gyroZ * gyroZ);
  t += 1 + (motion / 15 * GYRO_SENSITIVITY);  // Much faster response (was /50)
  
  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      uint8_t value = sin8(x * 32 + t) + sin8(y * 32 + t) + sin8((x + y) * 16 + t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, value);
    }
  }
}

void shakeSparkle() {
  float shake = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  fadeToBlackBy(leds, NUM_LEDS, 15);  // Slower fade for longer trails

  if (shake > SHAKE_THRESHOLD_LOW) {
    // More sparks from less movement
    int numSparks = constrain((shake - 1) * 25 * ACCEL_SENSITIVITY, 1, 40);
    for (int i = 0; i < numSparks; i++) {
      int pos = random16(NUM_LEDS);
      leds[pos] = ColorFromPalette(currentPalette, random8(), 255);
    }
  }
}

void tiltWave() {
  static uint8_t t = 0;
  t++;
  
  float angle = atan2(accelY, accelX);
  
  FastLED.clear();
  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      float projected = x * cos(angle) + y * sin(angle);
      uint8_t bright = sin8(projected * 40 + t * 3);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, projected * 20 + t, bright);
    }
  }
}

void tiltRipple() {
  static uint8_t t = 0;
  t++;

  // Larger center movement from tilt (was 2)
  float cx = 3.5 + accelX * 4.0 * ACCEL_SENSITIVITY;
  float cy = 3.5 - accelY * 4.0 * ACCEL_SENSITIVITY;
  
  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      float dx = x - cx;
      float dy = y - cy;
      float dist = sqrt(dx * dx + dy * dy);
      uint8_t bright = sin8((dist * 50) - t * 4);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, dist * 30 + t, bright);
    }
  }
}

void gyroSwirl() {
  static uint16_t t = 0;
  t += 2 + abs(gyroZ) / 30 * GYRO_SENSITIVITY;  // Much faster swirl (was /100)
  
  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      float dx = x - 3.5;
      float dy = y - 3.5;
      float angle = atan2(dy, dx);
      float dist = sqrt(dx * dx + dy * dy);
      uint8_t hue = (angle * 40) + (dist * 20) + t;
      leds[XY(x, y)] = ColorFromPalette(currentPalette, hue);
    }
  }
}

void shakeExplode() {
  static uint8_t explodeFrame = 255;
  static uint8_t explodeHue = 0;

  float shake = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);

  // Lower threshold for explosion trigger (was 2.5)
  if (shake > SHAKE_THRESHOLD_HIGH && explodeFrame >= 50) {
    explodeFrame = 0;
    explodeHue = random8();
  }
  
  if (explodeFrame < 50) {
    float radius = explodeFrame / 5.0;
    FastLED.clear();
    
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
        float dx = x - 3.5;
        float dy = y - 3.5;
        float dist = sqrt(dx * dx + dy * dy);
        if (abs(dist - radius) < 1.5) {
          uint8_t bright = 255 - abs(dist - radius) * 170;
          leds[XY(x, y)] = ColorFromPalette(currentPalette, explodeHue + dist * 20, bright);
        }
      }
    }
    explodeFrame++;
  } else {
    fadeToBlackBy(leds, NUM_LEDS, 30);
  }
}

// Run motion effect by index
void runMotionEffect(uint8_t index) {
  switch (index) {
    case 0: tiltBall(); break;
    case 1: motionPlasma(); break;
    case 2: shakeSparkle(); break;
    case 3: tiltWave(); break;
    case 4: tiltRipple(); break;
    case 5: gyroSwirl(); break;
    case 6: shakeExplode(); break;
  }
}

#endif
