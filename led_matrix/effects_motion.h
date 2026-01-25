#ifndef EFFECTS_MOTION_H
#define EFFECTS_MOTION_H

#include <FastLED.h>
#include "config.h"

// External references to globals defined in main sketch
extern CRGB leds[];
extern CRGBPalette16 currentPalette;
extern float accelX, accelY, accelZ;
extern float gyroX, gyroY, gyroZ;

void tiltBall() {
  static float ballX = 3.5, ballY = 3.5;
  
  float targetX = 3.5 + accelX * 3.5;
  float targetY = 3.5 - accelY * 3.5;
  
  ballX += (targetX - ballX) * 0.3;
  ballY += (targetY - ballY) * 0.3;
  
  ballX = constrain(ballX, 0, 7);
  ballY = constrain(ballY, 0, 7);
  
  fadeToBlackBy(leds, NUM_LEDS, 100);
  
  int ix = (int)ballX;
  int iy = (int)ballY;
  
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      int nx = ix + dx;
      int ny = iy + dy;
      if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
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
  t += 1 + (motion / 50);
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t value = sin8(x * 32 + t) + sin8(y * 32 + t) + sin8((x + y) * 16 + t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, value);
    }
  }
}

void shakeSparkle() {
  float shake = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  fadeToBlackBy(leds, NUM_LEDS, 20);
  
  if (shake > 1.5) {
    int numSparks = constrain((shake - 1) * 10, 1, 20);
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
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      float projected = x * cos(angle) + y * sin(angle);
      uint8_t bright = sin8(projected * 40 + t * 3);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, projected * 20 + t, bright);
    }
  }
}

void spinTrails() {
  static float angle = 0;
  angle += gyroZ / 500.0;
  
  fadeToBlackBy(leds, NUM_LEDS, 30);
  
  for (float r = 0; r < 4; r += 0.5) {
    int x = 3.5 + cos(angle + r) * r;
    int y = 3.5 + sin(angle + r) * r;
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
      leds[XY(x, y)] = ColorFromPalette(currentPalette, r * 50 + millis() / 20);
    }
  }
}

void gravityPixels() {
  static float px[8], py[8], vx[8], vy[8];
  static bool init = false;
  
  if (!init) {
    for (int i = 0; i < 8; i++) {
      px[i] = random8(WIDTH);
      py[i] = random8(HEIGHT);
      vx[i] = 0;
      vy[i] = 0;
    }
    init = true;
  }
  
  FastLED.clear();
  
  for (int i = 0; i < 8; i++) {
    vx[i] += accelX * 0.2;
    vy[i] -= accelY * 0.2;
    vx[i] *= 0.95;
    vy[i] *= 0.95;
    
    px[i] += vx[i];
    py[i] += vy[i];
    
    if (px[i] < 0) { px[i] = 0; vx[i] *= -0.5; }
    if (px[i] > 7) { px[i] = 7; vx[i] *= -0.5; }
    if (py[i] < 0) { py[i] = 0; vy[i] *= -0.5; }
    if (py[i] > 7) { py[i] = 7; vy[i] *= -0.5; }
    
    leds[XY((int)px[i], (int)py[i])] = ColorFromPalette(currentPalette, i * 30 + millis() / 20);
  }
}

void motionNoise() {
  static uint16_t t = 0;
  t += 3;
  
  int offsetX = accelX * 50;
  int offsetY = accelY * 50;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t n = inoise8((x * 50) + offsetX, (y * 50) + offsetY, t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, n);
    }
  }
}

void tiltRipple() {
  static uint8_t t = 0;
  t++;
  
  float cx = 3.5 + accelX * 2;
  float cy = 3.5 - accelY * 2;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
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
  t += 2 + abs(gyroZ) / 100;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
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
  
  if (shake > 2.5 && explodeFrame >= 50) {
    explodeFrame = 0;
    explodeHue = random8();
  }
  
  if (explodeFrame < 50) {
    float radius = explodeFrame / 5.0;
    FastLED.clear();
    
    for (uint8_t x = 0; x < WIDTH; x++) {
      for (uint8_t y = 0; y < HEIGHT; y++) {
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

void tiltFire() {
  static uint8_t heat[64];
  
  int hotSpot = constrain(3.5 + accelX * 2, 0, 7);
  
  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8(heat[i], random8(0, 20));
  }
  
  for (int x = 0; x < WIDTH; x++) {
    int intensity = 255 - abs(x - hotSpot) * 50;
    if (random8() < 180 && intensity > 0) {
      heat[XY(x, HEIGHT - 1)] = qadd8(heat[XY(x, HEIGHT - 1)], random8(intensity / 2, intensity));
    }
  }
  
  for (int y = 0; y < HEIGHT - 1; y++) {
    for (int x = 0; x < WIDTH; x++) {
      heat[XY(x, y)] = (heat[XY(x, y)] + heat[XY(x, y + 1)] + heat[XY(x, y + 1)]) / 3;
    }
  }
  
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(currentPalette, heat[i]);
  }
}

void motionRainbow() {
  static int16_t hueOffset = 0;
  
  hueOffset += gyroZ / 100;
  float spd = 1 + sqrt(accelX * accelX + accelY * accelY) * 2;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t hue = hueOffset + (x * 8) + (y * 8);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, hue);
    }
  }
  
  hueOffset += spd;
}

// Run motion effect by index
void runMotionEffect(uint8_t index) {
  switch (index) {
    case 0: tiltBall(); break;
    case 1: motionPlasma(); break;
    case 2: shakeSparkle(); break;
    case 3: tiltWave(); break;
    case 4: spinTrails(); break;
    case 5: gravityPixels(); break;
    case 6: motionNoise(); break;
    case 7: tiltRipple(); break;
    case 8: gyroSwirl(); break;
    case 9: shakeExplode(); break;
    case 10: tiltFire(); break;
    case 11: motionRainbow(); break;
  }
}

#endif
