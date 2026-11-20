#ifndef KALEIDO_SCENE_H
#define KALEIDO_SCENE_H

// ============================================================================
// Kaleido scene (experimental) — the ESP-Mosaico kaleidoscope engine as a
// background behind the face. Built only with -DKALEIDO_ENABLED.
// ============================================================================
// The rule for this scene is that it must not slow vizBot down:
//   - Nothing is allocated and no task exists while another scene is showing.
//   - Every buffer is PSRAM (about 1 MB). Internal RAM is two task stacks
//     (7 KB) plus a few small allocations, and only while the scene runs.
//   - All drawing happens on core 0 in two tasks at priority 1, the same as
//     wifi_srv and cloud, so those keep their share of the core (the WiFi
//     stack itself is far above). Idle priority was tried first: the idle
//     task sleeps a whole tick whenever its turn comes, which left Kaleido
//     8 frames a second.
//   - The render loop (core 1) never draws and never waits: each frame it
//     enlarges the newest finished picture into the canvas. If core 0 is
//     busy, Kaleido's own frame rate drops and vizBot's does not.
//   - The picture is drawn at two-thirds size (160x188 on the 1.69) and
//     enlarged 1.5x.
//     Full size was measured first: both cores share one 32 KB data cache and
//     one PSRAM bus, and with vizBot's own canvas traffic a full-size frame
//     took 38 ms of core 0 (14 fps). Half size ran at 29 fps but looked soft.
//   - Start-up and tear-down run on the worker, off the render loop.
//
// Tasks:
//   kaleido      source textures (1 to 2 a second), fold tables on a look
//                change, start-up and tear-down.
//   kaleido_draw one full frame per loop frame: cross-fades between source
//                textures (as the Mosaico does), then draws the kaleidoscope.
// ============================================================================

#ifdef KALEIDO_ENABLED

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "config.h"

extern "C" {
#include "ko_engine.h"
#include "ko_look.h"
#include "ko_organic.h"
}

#define BG_STYLE_KALEIDO   5
// Drawn size; shown at 1.5x (every 2 drawn pixels become 3 on the panel).
// Each side is two-thirds of the panel rounded up to an even number, so the
// enlarged picture can be a little bigger than the panel; the extra is cropped
// evenly. 240x280 -> 160x188, 240x240 -> 160x160, 320x240 -> 214x160.
#define KS_W               ((((LCD_WIDTH * 2 + 2) / 3) + 1) & ~1)
#define KS_H               ((((LCD_HEIGHT * 2 + 2) / 3) + 1) & ~1)
#define KS_OUT_W           (KS_W * 3 / 2)
#define KS_OUT_H           (KS_H * 3 / 2)
#define KS_CROP_X          ((KS_OUT_W - LCD_WIDTH) / 2)
#define KS_CROP_Y          ((KS_OUT_H - LCD_HEIGHT) / 2)
static_assert(KS_OUT_W >= LCD_WIDTH && KS_OUT_H >= LCD_HEIGHT, "Kaleido picture must cover the panel");
#define KS_ORGANIC_N       128     // organic source size (64 is 3x cheaper but looked fuzzy)
#define KS_LOOKS           8       // mirror looks (the two drum looks are not ported yet)
#define KS_LOOK_SECONDS    30      // auto-tour: seconds per look
#define KS_MOTION          0.75f   // "all motion at 75% speed", as on the Mosaico
#define KS_TUNNEL_SPEED    0.10f   // tunnel depth units per animation second
#define KS_TEX_REST_MS     30      // pause after each source texture, so core 0 gets idle time
#define KS_TASK_PRIORITY    1       // same as wifi_srv and cloud
// Task stacks (internal RAM, only while the scene runs). Measured peak use on
// the 1.69 across look changes: worker 1.6 KB, draw 1.0 KB.
#define KS_WORKER_STACK    4096
#define KS_DRAW_STACK      3072
#define KS_FADE_MIN_US     60000u  // cross-fade length follows the texture rate
#define KS_FADE_MAX_US     1000000u   // a slow texture (1 a second) still fades the whole way
#define KS_IDLE_STOP_MS    3000    // no frame taken for this long: free everything
#define KS_RETRY_MS        5000    // wait before retrying a failed start
#define KS_SPREAD_MASK     0x07E0F81Fu
#define KS_TEXELS          ((size_t)KO_SRC_N * KO_SRC_N)
#define KS_FRAME_BYTES     ((size_t)KS_W * KS_H * 2)

// Panel grade. The looks were tuned on the Mosaico's AMOLED, where lifted
// shadows still read as deep colour. On this IPS panel they read as grey, so
// the source is made more vivid, its black level pulled down and its mid-tones
// darkened before it is drawn.
static float ksVivid = 1.35f;      // saturation multiplier on top of the look's own
static float ksBlack = 0.08f;      // input level that becomes black
static float ksGamma = 1.25f;      // >1 darkens mid-tones
static float ksSharp = 0.75f;      // edge boost on the source image; 0 = off

enum : uint8_t { KS_OFF = 0, KS_STARTING, KS_RUNNING, KS_STOPPING };

struct KaleidoScene {
  volatile uint8_t state;
  SemaphoreHandle_t lock;          // loop: while copying a frame. worker: while freeing.
  StaticSemaphore_t lockBuf;

  // Allocated by the worker on start, freed by it on stop
  ko_engine_t *engine[2];
  uint32_t *texShow, *texPrev, *texNext, *texBack;   // spread textures
  uint16_t *tex565;
  uint8_t *rgb, *rgbSmall, *rgbSharp;
  ko_organic_t *organic;
  uint16_t *frame[2];              // finished pictures, KS_W x KS_H, plain RGB565
  TaskHandle_t drawTask, workerTask;
  uint8_t engineLook[2];
  uint8_t lut5[32], lut6[64];
  float lutBlack, lutGamma;

  // worker -> draw task
  volatile bool texPending, enginePending;
  volatile uint8_t backEngineLook;
  // draw task -> loop
  volatile bool frameReady, haveFrame, drawParked;
  volatile uint8_t frontFrame;
  // loop -> tasks
  volatile uint32_t lastFrameMs;
  volatile uint8_t lookReq;

  // Animation (draw task only, read by the worker for the next texture)
  ko_look_t look;
  volatile uint8_t lookIndex;
  uint8_t frontEngine;
  float angle;
  volatile float t, hueT;
  double travel;
  uint32_t lastUs, lookStartMs;
  bool fading;
  uint32_t fadeStartUs, fadeUs, lastTexUs;

  uint32_t failedMs;

  // Stats for GET /state
  volatile uint32_t frames, drawUsSum, drawUsMax, textures, texUsSum, copies, copyUsSum;
};
static KaleidoScene ks = {};

static void *ksPsram(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM); }

// ---- Source textures (worker) ---------------------------------------------
// Bilinear n x n -> KO_SRC_N^2 RGB888 upscale (same maths as the Mosaico worker).
static void ksUpscale(const uint8_t *src, int n, uint8_t *dst) {
  const int N = KO_SRC_N;
  for (int y = 0; y < N; ++y) {
    const int fy = ((y * n) << 8) / N - 128;
    const int y0 = fy < 0 ? 0 : fy >> 8;
    const int y1 = y0 + 1 < n ? y0 + 1 : y0;
    const int wy = fy < 0 ? 0 : fy & 255;
    for (int x = 0; x < N; ++x) {
      const int fx = ((x * n) << 8) / N - 128;
      const int x0 = fx < 0 ? 0 : fx >> 8;
      const int x1 = x0 + 1 < n ? x0 + 1 : x0;
      const int wx = fx < 0 ? 0 : fx & 255;
      const uint8_t *a = src + ((size_t)y0 * n + x0) * 3, *b = src + ((size_t)y0 * n + x1) * 3;
      const uint8_t *c = src + ((size_t)y1 * n + x0) * 3, *d = src + ((size_t)y1 * n + x1) * 3;
      uint8_t *o = dst + ((size_t)y * N + x) * 3;
      for (int ch = 0; ch < 3; ++ch) {
        const int top = a[ch] * (256 - wx) + b[ch] * wx;
        const int bot = c[ch] * (256 - wx) + d[ch] * wx;
        o[ch] = (uint8_t)((top * (256 - wy) + bot * wy) >> 16);
      }
    }
  }
}

// Edge boost (unsharp mask) on the KO_SRC_N^2 RGB888 source: each pixel moves
// away from the average of its four neighbours. Edges clamp, as the engine
// samples the texture mirrored.
static void ksSharpen(const uint8_t *src, uint8_t *dst, float amount) {
  const int N = KO_SRC_N, k = (int)(amount * 64.0f);
  for (int y = 0; y < N; ++y) {
    const uint8_t *row = src + (size_t)y * N * 3;
    const uint8_t *up = y > 0 ? row - N * 3 : row, *down = y + 1 < N ? row + N * 3 : row;
    uint8_t *out = dst + (size_t)y * N * 3;
    for (int x = 0; x < N; ++x) {
      const int l = x > 0 ? (x - 1) * 3 : 0, r = x + 1 < N ? (x + 1) * 3 : x * 3, c = x * 3;
      for (int ch = 0; ch < 3; ++ch) {
        const int v = row[c + ch];
        const int edge = 4 * v - row[l + ch] - row[r + ch] - up[c + ch] - down[c + ch];
        const int o = v + ((edge * k) >> 8);
        out[c + ch] = (uint8_t)(o < 0 ? 0 : (o > 255 ? 255 : o));
      }
    }
  }
}

static void ksBuildPanelLut() {
  ks.lutBlack = ksBlack;
  ks.lutGamma = ksGamma;
  for (int i = 0; i < 64; ++i) {
    float v = ((float)i / 63.0f - ksBlack) / (1.0f - ksBlack);
    v = v <= 0.0f ? 0.0f : powf(v, ksGamma);
    ks.lut6[i] = (uint8_t)(v * 63.0f + 0.5f);
    if (i < 32) {
      float u = ((float)i / 31.0f - ksBlack) / (1.0f - ksBlack);
      u = u <= 0.0f ? 0.0f : powf(u, ksGamma);
      ks.lut5[i] = (uint8_t)(u * 31.0f + 0.5f);
    }
  }
}

// One source texture for the current look and time, into ks.texBack.
static void ksMakeTexture() {
  const ko_look_t look = *ko_look_preset(ks.lookIndex);
  const ko_organic_variant_t variant = (ko_organic_variant_t)(ks.lookIndex % KO_ORGANIC_COUNT);
  if (KS_ORGANIC_N == KO_SRC_N) {
    ko_organic_render(ks.organic, variant, ks.t, ks.rgb);
  } else {
    ko_organic_render(ks.organic, variant, ks.t, ks.rgbSmall);
    ksUpscale(ks.rgbSmall, KS_ORGANIC_N, ks.rgb);
  }
  const uint8_t *graded = ks.rgb;
  if (ksSharp > 0.0f) {
    ksSharpen(ks.rgb, ks.rgbSharp, ksSharp);
    graded = ks.rgbSharp;
  }
  ko_grade(graded, ks.tex565, KO_SRC_N, look.hue + ks.hueT, look.saturation * ksVivid, look.contrast, look.invert);
  if (ks.lutBlack != ksBlack || ks.lutGamma != ksGamma) ksBuildPanelLut();
  uint32_t *out = ks.texBack;
  for (size_t i = 0; i < KS_TEXELS; ++i) {
    const uint16_t c = ks.tex565[i];
    out[i] = ((uint32_t)ks.lut5[c >> 11] << 11) | ((uint32_t)ks.lut6[(c >> 5) & 63] << 21) | ks.lut5[c & 31];
  }
}

static void ksBuildFold(uint8_t slot, uint8_t lookIndex) {
  const ko_look_t *look = ko_look_preset(lookIndex);
  const ko_geometry_t geo = {look->mode, look->segments, look->scale};
  ko_engine_set_geometry(ks.engine[slot], &geo);
  ks.engineLook[slot] = lookIndex;
}

// ---- Drawing (draw task, core 0) ------------------------------------------
static void ksApplyLook(uint8_t index) {
  ks.lookIndex = index;
  ks.look = *ko_look_preset(index);
  ks.travel = 0.0;
  ks.hueT = 0.0f;
  ks.lookStartMs = millis();
}

// Blend the shown texture from the previous one toward the newest.
static void ksCrossFade(uint32_t nowUs) {
  if (ks.texPending) {
    uint32_t *finished = ks.texBack;
    ks.texBack = ks.texNext;       // the worker's next scratch buffer
    ks.texNext = finished;
    memcpy(ks.texPrev, ks.texShow, KS_TEXELS * 4);   // start from what is on screen now
    const uint32_t gap = nowUs - ks.lastTexUs;
    ks.lastTexUs = nowUs;
    ks.fadeUs = gap < KS_FADE_MIN_US ? KS_FADE_MIN_US : (gap > KS_FADE_MAX_US ? KS_FADE_MAX_US : gap);
    ks.fadeStartUs = nowUs;
    ks.fading = true;
    ks.texPending = false;         // the worker may start on the next one
  }
  if (!ks.fading) return;
  const uint32_t elapsed = nowUs - ks.fadeStartUs;
  if (elapsed >= ks.fadeUs) {
    memcpy(ks.texShow, ks.texNext, KS_TEXELS * 4);
    ks.fading = false;
    return;
  }
  const uint32_t w = elapsed * 32u / ks.fadeUs;      // 0..31
  const uint32_t *a = ks.texPrev, *b = ks.texNext;
  uint32_t *o = ks.texShow;
  for (size_t i = 0; i < KS_TEXELS; ++i) {
    o[i] = ((a[i] * (32u - w) + b[i] * w) >> 5) & KS_SPREAD_MASK;
  }
}

static void ksDrawFrame() {
  const uint32_t nowUs = micros();
  if (ks.enginePending) {
    ks.frontEngine ^= 1;
    ksApplyLook(ks.engineLook[ks.frontEngine]);
    ks.enginePending = false;
  }
  ksCrossFade(nowUs);

  float dt = ks.lastUs ? (nowUs - ks.lastUs) * 1e-6f : 0.0f;
  if (dt > 0.25f) dt = 0.25f;                  // after a pause, carry on rather than jump
  ks.lastUs = nowUs;
  const float adt = dt * ks.look.speed * ks.look.pace * KS_MOTION;
  ks.t += adt;
  ks.angle += ks.look.spin * adt;
  if (ks.angle > 6.2831853f) ks.angle -= 6.2831853f;
  if (ks.angle < -6.2831853f) ks.angle += 6.2831853f;
  ks.hueT += ks.look.hue_drift * adt;
  if (ks.look.mode == KO_MODE_TUNNEL) ks.travel += (double)(KS_TUNNEL_SPEED * adt);

  // Auto-tour: ask for the next look; it appears when its fold table is built
  if (!ks.enginePending && ks.lookReq == ks.lookIndex && millis() - ks.lookStartMs > KS_LOOK_SECONDS * 1000UL) {
    ks.lookReq = (ks.lookIndex + 1) % KS_LOOKS;
  }

  const ko_view_t view = {ks.angle, ks.look.zoom, 0.0f, 0.0f, 1, ks.travel};
  ko_engine_render_spread(ks.engine[ks.frontEngine], ks.texShow, &view, ks.frame[ks.frontFrame ^ 1],
                          KS_W, 0, 0, KS_W, KS_H);
  const uint32_t us = micros() - nowUs;
  ks.frames = ks.frames + 1;
  ks.drawUsSum += us;
  if (us > ks.drawUsMax) ks.drawUsMax = us;
}

static void ksDrawTask(void *) {
  for (;;) {
    if (ks.state == KS_STOPPING) {             // park: the worker deletes this task
      ks.drawParked = true;
      for (;;) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    if (ks.frameReady) {                       // the loop has not taken the last frame yet
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
      continue;
    }
    // Drawing goes before source textures: the worker drops to idle priority
    // for the length of a frame. Sharing the core evenly made every frame
    // miss its loop frame (15 fps); this way both fit (29 fps, 2 textures/s).
    vTaskPrioritySet(ks.workerTask, tskIDLE_PRIORITY);
    ksDrawFrame();
    vTaskPrioritySet(ks.workerTask, KS_TASK_PRIORITY);
    ks.frameReady = true;
    vTaskDelay(1);                             // a turn for the other core 0 tasks every frame
  }
}

// ---- Worker: textures, fold tables, start-up and tear-down ----------------
static bool ksAllocate() {
  ks.engine[0] = ko_engine_create_sized(KS_W, KS_H);
  ks.engine[1] = ko_engine_create_sized(KS_W, KS_H);
  ks.texShow = (uint32_t *)ksPsram(KS_TEXELS * 4);
  ks.texPrev = (uint32_t *)ksPsram(KS_TEXELS * 4);
  ks.texNext = (uint32_t *)ksPsram(KS_TEXELS * 4);
  ks.texBack = (uint32_t *)ksPsram(KS_TEXELS * 4);
  ks.tex565 = (uint16_t *)ksPsram(KS_TEXELS * 2);
  ks.rgb = (uint8_t *)ksPsram(KS_TEXELS * 3);
  ks.rgbSmall = (uint8_t *)ksPsram((size_t)KS_ORGANIC_N * KS_ORGANIC_N * 3);
  ks.rgbSharp = (uint8_t *)ksPsram(KS_TEXELS * 3);
  ks.frame[0] = (uint16_t *)ksPsram(KS_FRAME_BYTES);
  ks.frame[1] = (uint16_t *)ksPsram(KS_FRAME_BYTES);
  ks.organic = ko_organic_create(KS_ORGANIC_N);
  return ks.engine[0] && ks.engine[1] && ks.texShow && ks.texPrev && ks.texNext && ks.texBack &&
         ks.tex565 && ks.rgb && ks.rgbSmall && ks.rgbSharp && ks.frame[0] && ks.frame[1] && ks.organic;
}

static void ksFree() {
  if (ks.drawTask) vTaskDelete(ks.drawTask);   // parked, or never started
  ks.drawTask = nullptr;
  for (int i = 0; i < 2; ++i) {
    ko_engine_destroy(ks.engine[i]);
    ks.engine[i] = nullptr;
    heap_caps_free(ks.frame[i]);
    ks.frame[i] = nullptr;
  }
  heap_caps_free(ks.texShow);
  heap_caps_free(ks.texPrev);
  heap_caps_free(ks.texNext);
  heap_caps_free(ks.texBack);
  heap_caps_free(ks.tex565);
  heap_caps_free(ks.rgb);
  heap_caps_free(ks.rgbSmall);
  heap_caps_free(ks.rgbSharp);
  ks.texShow = ks.texPrev = ks.texNext = ks.texBack = nullptr;
  ks.tex565 = nullptr;
  ks.rgb = ks.rgbSmall = ks.rgbSharp = nullptr;
  if (ks.organic) ko_organic_destroy(ks.organic);
  ks.organic = nullptr;
}

static void ksWorkerTask(void *) {
  ks.workerTask = xTaskGetCurrentTaskHandle();
  bool ok = ksAllocate();
  if (ok) {
    ks.lutBlack = -1.0f;                       // forces the panel table to be built
    ks.frontEngine = 0;
    ksBuildFold(0, ks.lookReq);
    ksApplyLook(ks.lookReq);
    ksMakeTexture();
    memcpy(ks.texShow, ks.texBack, KS_TEXELS * 4);
    memcpy(ks.texNext, ks.texBack, KS_TEXELS * 4);
    ks.fading = false;
    ks.texPending = false;
    ks.enginePending = false;
    ks.frameReady = false;
    ks.haveFrame = false;
    ks.drawParked = false;
    ks.frontFrame = 0;
    ks.lastUs = 0;
    ks.lastTexUs = micros();
    ok = xTaskCreatePinnedToCore(ksDrawTask, "kaleido_draw", KS_DRAW_STACK, nullptr, KS_TASK_PRIORITY, &ks.drawTask, 0) == pdPASS;
  }
  if (!ok) {
    ksFree();
    ks.failedMs = millis() | 1;
    ks.state = KS_OFF;
    vTaskDelete(nullptr);
  }
  ks.lastFrameMs = millis();
  ks.state = KS_RUNNING;

  for (;;) {
    // Nobody is taking frames (another scene, a full-screen page, another mode): let go.
    if (ks.state == KS_RUNNING && millis() - ks.lastFrameMs > KS_IDLE_STOP_MS) {
      xSemaphoreTake(ks.lock, portMAX_DELAY);
      if (millis() - ks.lastFrameMs > KS_IDLE_STOP_MS) ks.state = KS_STOPPING;
      xSemaphoreGive(ks.lock);
    }
    if (ks.state == KS_STOPPING) {
      while (!ks.drawParked) {                 // let it finish the frame it is on
        xTaskNotifyGive(ks.drawTask);
        vTaskDelay(1);
      }
      xSemaphoreTake(ks.lock, portMAX_DELAY);  // never while the loop is copying a frame
      ksFree();
      ks.state = KS_OFF;
      xSemaphoreGive(ks.lock);
      vTaskDelete(nullptr);
    }
    if (!ks.enginePending && ks.lookReq != ks.engineLook[ks.frontEngine]) {
      ksBuildFold(ks.frontEngine ^ 1, ks.lookReq);   // about 0.3 s; the old look keeps running
      ks.enginePending = true;
    }
    if (!ks.texPending) {
      const uint32_t t0 = micros();
      ksMakeTexture();
      ks.texUsSum += micros() - t0;
      ks.textures = ks.textures + 1;
      ks.texPending = true;
      vTaskDelay(pdMS_TO_TICKS(KS_TEX_REST_MS));
    } else {
      vTaskDelay(pdMS_TO_TICKS(4));
    }
  }
}

// ---- Render-loop API (core 1) ---------------------------------------------
// Exact average of two RGB565 pixels, no carry between channels.
#define KS_AVG(p, q) (uint16_t)(((p) & (q)) + ((((p) ^ (q)) & 0xF7DEu) >> 1))

// One drawn row -> KS_OUT_W panel pixels: each pair a, b becomes a, average, b.
__attribute__((optimize("O2")))
static void ksExpandPairs(const uint16_t *row, uint16_t *out) {
  for (int x = 0; x < KS_W; x += 2) {
    const uint16_t a = row[x], b = row[x + 1];
    out[0] = __builtin_bswap16(a);
    out[1] = __builtin_bswap16(KS_AVG(a, b));
    out[2] = __builtin_bswap16(b);
    out += 3;
  }
}

// One drawn row -> one canvas row, cropped to the panel width when it is wider.
__attribute__((optimize("O2")))
static void ksExpandRow(const uint16_t *row, uint16_t *out) {
#if KS_OUT_W == LCD_WIDTH
  ksExpandPairs(row, out);
#else
  uint16_t line[KS_OUT_W];
  ksExpandPairs(row, line);
  memcpy(out, line + KS_CROP_X, (size_t)LCD_WIDTH * 2);
#endif
}

// KS_W x KS_H plain RGB565 -> the canvas at 1.5x. Rows work like columns: each
// pair of drawn rows becomes row, average, row. Rows off the panel are skipped.
__attribute__((optimize("O2")))
static void ksEnlarge(const uint16_t *src, uint16_t *dst) {
  uint16_t mid[KS_W];
  for (int pair = 0; pair < KS_H / 2; ++pair) {
    const uint16_t *a = src + (size_t)(2 * pair) * KS_W, *b = a + KS_W;
    const int row = 3 * pair - KS_CROP_Y;
    if (row >= 0 && row < LCD_HEIGHT) ksExpandRow(a, dst + (size_t)row * LCD_WIDTH);
    if (row + 1 >= 0 && row + 1 < LCD_HEIGHT) {
      for (int x = 0; x < KS_W; ++x) mid[x] = KS_AVG(a[x], b[x]);
      ksExpandRow(mid, dst + (size_t)(row + 1) * LCD_WIDTH);
    }
    if (row + 2 < LCD_HEIGHT) ksExpandRow(b, dst + (size_t)(row + 2) * LCD_WIDTH);
  }
}

// Put the newest Kaleido picture into the canvas (LCD_WIDTH x LCD_HEIGHT,
// byte-swapped RGB565). Never draws and never waits for one. Returns false
// while there is no picture yet: the caller clears to black.
inline bool kaleidoSceneRender(uint16_t *px, uint32_t nowMs) {
  if (!px) return false;
  if (ks.state == KS_OFF) {
    if (ks.failedMs && nowMs - ks.failedMs < KS_RETRY_MS) return false;
    if (!ks.lock) ks.lock = xSemaphoreCreateMutexStatic(&ks.lockBuf);
    ks.failedMs = 0;
    ks.lookReq = ks.lookIndex % KS_LOOKS;
    ks.state = KS_STARTING;
    if (xTaskCreatePinnedToCore(ksWorkerTask, "kaleido", KS_WORKER_STACK, nullptr, KS_TASK_PRIORITY, nullptr, 0) != pdPASS) {
      ks.failedMs = nowMs | 1;
      ks.state = KS_OFF;
    }
    return false;
  }
  if (ks.state != KS_RUNNING) return false;

  xSemaphoreTake(ks.lock, portMAX_DELAY);      // only ever contended by a tear-down
  if (ks.state != KS_RUNNING) {
    xSemaphoreGive(ks.lock);
    return false;
  }
  ks.lastFrameMs = nowMs;
  if (ks.frameReady) {                         // take it and let the draw task start the next
    ks.frontFrame ^= 1;
    ks.haveFrame = true;
    ks.frameReady = false;
    xTaskNotifyGive(ks.drawTask);
  }
  const bool have = ks.haveFrame;
  if (have) {
    const uint32_t t0 = micros();
    ksEnlarge(ks.frame[ks.frontFrame], px);
    ks.copyUsSum += micros() - t0;
    ks.copies = ks.copies + 1;
  }
  xSemaphoreGive(ks.lock);
  return have;
}

// Another scene is showing: free everything (the worker does it, off the loop).
inline void kaleidoSceneRelease() {
  if (ks.state == KS_RUNNING) ks.state = KS_STOPPING;
}

// For GET /state. Reading starts a fresh window.
inline String kaleidoSceneJson() {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  const float secs = lastMs ? (now - lastMs) / 1000.0f : 0.0f;
  const uint32_t frames = ks.frames, textures = ks.textures, copies = ks.copies;
  String json = String("{\"state\":") + String(ks.state) +
                ",\"look\":\"" + ko_look_name(ks.lookIndex) + "\"" +
                ",\"fps\":" + String(secs > 0.0f ? frames / secs : 0.0f, 1) +
                ",\"drawAvgUs\":" + String(frames ? ks.drawUsSum / frames : 0) +
                ",\"drawMaxUs\":" + String(ks.drawUsMax) +
                ",\"texPerSec\":" + String(secs > 0.0f ? textures / secs : 0.0f, 1) +
                ",\"texAvgUs\":" + String(textures ? ks.texUsSum / textures : 0) +
                ",\"copyAvgUs\":" + String(copies ? ks.copyUsSum / copies : 0) + "}";
  lastMs = now;
  ks.frames = 0;
  ks.drawUsSum = 0;
  ks.drawUsMax = 0;
  ks.textures = 0;
  ks.texUsSum = 0;
  ks.copies = 0;
  ks.copyUsSum = 0;
  return json;
}

#ifdef KALEIDO_SERIAL_STATS
// Bench aid ([env:lcd-169-kaleido-dev] only): keys over USB serial, and loop
// and heap figures every 5 s.
//   k Kaleido   a ambient   b black   n next look   p party mode on/off
//   v/V vivid -/+   c/C black level -/+   g/G gamma -/+   s/S sharpen -/+
void setBotBackgroundStyle(uint8_t style);
uint8_t getBotBackgroundStyle();
inline void kaleidoDevSerial() {
  static uint32_t lastMs = 0;
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'k') setBotBackgroundStyle(BG_STYLE_KALEIDO);
    else if (c == 'a') setBotBackgroundStyle(4);
    else if (c == 'b') setBotBackgroundStyle(0);
    else if (c == 'n') ks.lookReq = (ks.lookIndex + 1) % KS_LOOKS;
    else if (c == 'p') partyMode = !partyMode;
    else if (c == 'v') ksVivid -= 0.1f;
    else if (c == 'V') ksVivid += 0.1f;
    else if (c == 'c') ksBlack -= 0.02f;
    else if (c == 'C') ksBlack += 0.02f;
    else if (c == 'g') ksGamma -= 0.05f;
    else if (c == 'G') ksGamma += 0.05f;
    else if (c == 's') ksSharp -= 0.25f;
    else if (c == 'S') ksSharp += 0.25f;
    if (ksSharp < 0.0f) ksSharp = 0.0f;
    if (ksBlack < 0.0f) ksBlack = 0.0f;
  }
  const uint32_t now = millis();
  if (now - lastMs < 5000) return;
  lastMs = now;
  Serial.printf("KSTAT bg=%u party=%u/%u stackFree=%u/%u heap=%u heapMin=%u heapBlock=%u psramFree=%u vivid=%.2f black=%.2f gamma=%.2f sharp=%.2f loop=%s kaleido=%s\n",
                (unsigned)getBotBackgroundStyle(), (unsigned)partyMode, (unsigned)partyBlob.active,
                (unsigned)(ks.state == KS_RUNNING ? uxTaskGetStackHighWaterMark(ks.workerTask) : 0),
                (unsigned)(ks.state == KS_RUNNING ? uxTaskGetStackHighWaterMark(ks.drawTask) : 0),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                ksVivid, ksBlack, ksGamma, ksSharp,
                loopStatsJson().c_str(), kaleidoSceneJson().c_str());
}
#endif // KALEIDO_SERIAL_STATS

#endif // KALEIDO_ENABLED
#endif // KALEIDO_SCENE_H
