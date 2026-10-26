// Firmware side of the parity check: compiles vizbot/bot_eye_model.h (with the freshly
// generated bot_eye_params.h) on the Mac and renders a fixed set of frames into raw RGB565.
// parity/lab.ts renders the same frames, in the same order, from eyelab/shared and the face
// files; parity/parity.ts diffs the two dumps. Keep the frame list in step with lab.ts.
//
//   c++ -std=c++17 -O1 -I <build> -I parity/stub -I vizbot parity/test.cpp -o <build>/fw
//   <build>/fw <out.bin>
//
// <build> holds copies of bot_eye_model.h, bot_eye_params.h and bot_faces.h, so the model's
// #include "config.h" finds the stub, not the real firmware config.
#include <Arduino.h>
#include <cstdio>
#include "config.h"
unsigned long g_millis = 0;
struct GfxDevice {
  uint16_t fb[LCD_WIDTH * LCD_HEIGHT];
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) {
    if (x < 0 || x >= LCD_WIDTH) return;
    for (int32_t j = y; j < y + h; j++) if (j >= 0 && j < LCD_HEIGHT) fb[j * LCD_WIDTH + x] = (uint16_t)c;
  }
  void clear(uint16_t c) { for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) fb[i] = c; }
};
GfxDevice dev;
GfxDevice *gfx = &dev;
#define BOT_COLOR_BG 0x0000
#define BOT_COLOR_PUPIL 0x0000
#include "bot_eye_model.h"

static FILE *out;
static void frame(int cx, int cy, bool ambient, unsigned long now, float k = 1.0f) {
  dev.clear(ambient ? 0x1234 : 0);
  botEyes.render(cx, cy, k, 0xFFFF, ambient, now);
  fwrite(dev.fb, 2, LCD_WIDTH * LCD_HEIGHT, out);
}
int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: fw <out.bin>\n"); return 2; }
  out = fopen(argv[1], "wb");
  if (!out) { perror(argv[1]); return 1; }
  const int N = BOT_NUM_EXPRESSIONS;
  botEyes.init(0);
  botEyes.idle = false;
  // every face at rest, plain and ambient
  for (int i = 0; i < N; i++) {
    botEyes.setExpr(i, 0, true);
    frame(120, 118, false, 0);
    frame(120, 118, true, 0);
  }
  // transitions, idle off (deterministic), 50 ms frames
  static const int pairs[][2] = { {0, 6}, {16, 20}, {0, 7}, {2, 1}, {4, 3}, {0, 24}, {12, 9}, {13, 18}, {0, 21} };
  for (auto &p : pairs) {
    unsigned long now = 100000;
    botEyes.init(now);
    botEyes.idle = false;
    botEyes.setExpr(p[0], now, true);
    for (int f = 0; f < 4; f++) { now += 50; botEyes.step(now); }
    botEyes.setExpr(p[1], now, false);
    for (int f = 0; f < 24; f++) { now += 50; botEyes.step(now); frame(120, 118, false, now); }
  }
  // sleep
  { unsigned long now = 100000; botEyes.init(now); botEyes.idle = false; botEyes.setExpr(1, now, true);
    botEyes.sleeping = true; for (int f = 0; f < 30; f++) { now += 50; botEyes.step(now); frame(120, 118, false, now); } }
  // pendant: neutral to happy
  { unsigned long now = 100000; botEyes.init(now); botEyes.idle = false; botEyes.pendant = true; botEyes.setExpr(0, now, true);
    for (int f = 0; f < 2; f++) { now += 50; botEyes.step(now); }
    botEyes.setExpr(1, now, false); for (int f = 0; f < 20; f++) { now += 50; botEyes.step(now); frame(120, 118, false, now); } }
  // look pad
  { unsigned long now = 100000; botEyes.init(now); botEyes.idle = false; botEyes.setExpr(0, now, true);
    botEyes.padX = 0.8f; botEyes.padY = -0.5f; for (int f = 0; f < 20; f++) { now += 50; botEyes.step(now); frame(120, 118, false, now); } }
  // chip scale
  { botEyes.init(0); botEyes.idle = false;
    for (int i = 0; i < N; i++) { botEyes.setExpr(i, 0, true); frame(120, 118, false, 0, 0.25f); frame(120, 118, true, 0, 0.36f); } }
  // glitch bands, fixed
  { unsigned long now = 100000; botEyes.init(now); botEyes.idle = false; botEyes.setExpr(23, now, true);
    static const float B[2][3][3] = { {{0.1f, 0.2f, 0.15f}, {0.4f, 0.12f, -0.18f}, {0.45f, 0.25f, 0.11f}}, {{0.6f, 0.15f, -0.2f}, {0.05f, 0.1f, 0.13f}, {0.3f, 0.28f, -0.12f}} };
    for (int e = 0; e < 2; e++) { for (int j = 0; j < 3; j++) { botEyes.band[e][j].y0 = B[e][j][0]; botEyes.band[e][j].h = B[e][j][1]; botEyes.band[e][j].dx = B[e][j][2]; } botEyes.bandSet[e] = true; }
    frame(120, 118, false, now); frame(120, 118, true, now);
    botEyes.padX = 1; botEyes.padY = 0.6f; for (int f = 0; f < 10; f++) { now += 50; botEyes.gNext = now; botEyes.bandNext[0] = botEyes.bandNext[1] = now + 100000; botEyes.step(now); }
    frame(120, 118, false, now); }
  fclose(out);
  return 0;
}
