#ifndef KALEIDO_SCENE_H
#define KALEIDO_SCENE_H

// ============================================================================
// Kaleido scene (experimental) — the ESP-Mosaico kaleidoscope engine as a
// background behind the face. Built only with -DKALEIDO_ENABLED.
// ============================================================================
// The rule for this scene is that it must not slow vizBot down:
//   - No buffer is held while another scene is showing. Every allocation goes
//     through the memory owner (mem.h, tag "kaleido") and lands in PSRAM
//     (about 1 MB while it shows). The two tasks are made on first use and
//     then kept, parked: their stacks are PSRAM too, so the only internal RAM
//     Kaleido ever holds is its two static task control blocks (under 1 KB).
//   - All drawing happens on core 0. The draw task runs at priority 1, the
//     same as wifi_srv (idle priority was tried first: the idle task sleeps a
//     whole tick whenever its turn comes, which left Kaleido 8 frames a
//     second). The texture worker runs at idle priority, taking turns with
//     IDLE0 and the cloud task. At priority 1 it filled every gap the draw
//     task left; at 80 MHz (low power mode) one texture is seconds of CPU, so
//     IDLE0 never ran and the task watchdog reset the pendant (2026-11-20).
//   - It gives way. The cloud task runs at idle priority, below Kaleido, so
//     without this a sync would barely move:
//       firmware upload or update: it draws nothing; the loop keeps the last
//         picture.
//       HTTPS open, or a cloud step running longer than KS_CLOUD_GRACE_MS: no
//         new source textures, and drawing takes at most a sixth of core 0
//         (the picture keeps moving, more slowly).
//     The 1.69 has no voice and no album art, so nothing else needs covering.
//   - The render loop (core 1) never draws and never waits: each frame it
//     enlarges the newest finished picture into the canvas. If core 0 is
//     busy, Kaleido's own frame rate drops and vizBot's does not.
//   - The picture is drawn at two-thirds size (160x188 on the 1.69) and
//     enlarged 1.5x.
//     Full size was measured first: both cores share one 32 KB data cache and
//     one PSRAM bus, and with vizBot's own canvas traffic a full-size frame
//     took 38 ms of core 0 (14 fps). Half size ran at 29 fps but looked soft.
//   - Start-up and tear-down run on the worker, off the render loop.
//   - /debug/mem lists what it holds; GET /state shows "busy" while it gives way.
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
#include <Update.h>
#include "config.h"
#include "mem.h"

extern "C" {
#include "ko_engine.h"
#include "ko_look.h"
#include "ko_organic.h"
}

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
#define KS_TASK_PRIORITY    1       // draw task: same as wifi_srv
#define KS_WORKER_PRIORITY tskIDLE_PRIORITY   // textures: never keeps IDLE0 (the watchdog) out
// Task stacks, in PSRAM. Measured peak use on the 1.69 across look changes:
// worker 1.6 KB, draw 1.0 KB.
#define KS_WORKER_STACK    4096
#define KS_DRAW_STACK      3072
#define KS_FADE_MIN_US     60000u  // cross-fade length follows the texture rate
#define KS_FADE_MAX_US     1000000u   // a slow texture (1 a second) still fades the whole way
#define KS_IDLE_PAUSE_MS   250     // no frame taken for this long: make no textures
#define KS_IDLE_STOP_MS    30000   // no frame taken for this long: free everything (a page or
                                   // the pendant's info screens come and go without a rebuild)
#define KS_RETRY_MS        5000    // wait before retrying a failed start
#define KS_BUSY_POLL_MS    50      // while stopped for an upload: how often to look again
#define KS_EASE_RATIO      5       // while easing: sleep this many times the last draw.
                                   // Measured on a 1.69 cloud sync: 3.45 s without Kaleido,
                                   // 5.1 s at 2, 4.05 s at 5 (Kaleido ~14 fps meanwhile)
#define KS_CLOUD_GRACE_MS  100     // a cloud step longer than this is real work, not a poll
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
static float ksSharp = 0.75f;
static uint8_t ksEase = KS_EASE_RATIO;      // edge boost on the source image; 0 = off

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
  volatile bool stopReq;           // loop -> worker: another scene is showing
  volatile uint8_t busy;           // KS_BUSY_*: why it is giving way right now

  // Stats for GET /state
  volatile uint32_t frames, drawUsSum, drawUsMax, textures, texUsSum, copies, copyUsSum;
};
static KaleidoScene ks = {};

static void *ksPsram(size_t bytes) { return memAlloc(MEM_KALEIDO, bytes, MEM_BIG_ONLY); }
static void ksRelease(void *p) { memFree(MEM_KALEIDO, p); }

// The engine's own allocations (ko_port.h), PSRAM only.
extern "C" void *ks_mem_calloc(size_t n, size_t size) { return memCalloc(MEM_KALEIDO, n, size, MEM_BIG_ONLY); }
extern "C" void ks_mem_free(void *ptr) { memFree(MEM_KALEIDO, ptr); }

// ---- Giving way -----------------------------------------------------------
// Kaleido runs on core 0 above the cloud task (idle priority) and shares the
// PSRAM bus with the TLS buffers (see the notes at the top).
inline bool otaUploadActive();               // ota_update.h
// Set by the cloud task (task_manager.h) around each cloud step; 0 between.
volatile uint32_t ksCloudStepMs = 0;
// Ordered by how far it gives way: below KS_BUSY_UPLOAD it eases, at it stops.
enum : uint8_t { KS_BUSY_NONE = 0, KS_BUSY_CLOUD, KS_BUSY_HTTPS, KS_BUSY_UPLOAD };
#ifdef KALEIDO_SERIAL_STATS
static volatile uint8_t ksFakeBusy = KS_BUSY_NONE;   // dev keys 'h' (HTTPS) and 'u' (upload)
#endif
static uint8_t ksBusyReason() {
#ifdef KALEIDO_SERIAL_STATS
  if (ksFakeBusy) return ksFakeBusy;
#endif
  if (otaUploadActive() || Update.isRunning()) return KS_BUSY_UPLOAD;
  if (memHeld(MEM_TLS) > 0) return KS_BUSY_HTTPS;
  const uint32_t step = ksCloudStepMs;
  if (step && millis() - step > KS_CLOUD_GRACE_MS) return KS_BUSY_CLOUD;
  return KS_BUSY_NONE;
}

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
    // The worker frees the buffers once it sees drawParked. Both tasks are on
    // core 0, so with the scheduler held it sees the state and the flag change
    // together: never "parked" while this task is about to draw.
    vTaskSuspendAll();
    const bool run = ks.state == KS_RUNNING;
    ks.drawParked = !run;
    xTaskResumeAll();
    if (!run) {                                // parked between runs; the task is kept
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (ks.frameReady) {                       // the loop has not taken the last frame yet
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
      continue;
    }
    const uint8_t busy = ksBusyReason();
    ks.busy = busy;
    if (busy >= KS_BUSY_UPLOAD) {              // stopped: the loop keeps the last picture
      vTaskDelay(pdMS_TO_TICKS(KS_BUSY_POLL_MS));
      continue;
    }
    // Drawing goes before source textures: the worker is at idle priority.
    const uint32_t t0 = micros();
    ksDrawFrame();
    ks.frameReady = true;
    if (busy) {                                // easing: at most 1/(1+ksEase) of core 0
      const uint32_t ms = (micros() - t0) * ksEase / 1000;
      vTaskDelay(ms > 0 ? pdMS_TO_TICKS(ms) : 1);
    } else {
      vTaskDelay(1);                           // a turn for the other core 0 tasks every frame
    }
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
  if (KS_ORGANIC_N != KO_SRC_N) ks.rgbSmall = (uint8_t *)ksPsram((size_t)KS_ORGANIC_N * KS_ORGANIC_N * 3);
  ks.rgbSharp = (uint8_t *)ksPsram(KS_TEXELS * 3);
  ks.frame[0] = (uint16_t *)ksPsram(KS_FRAME_BYTES);
  ks.frame[1] = (uint16_t *)ksPsram(KS_FRAME_BYTES);
  ks.organic = ko_organic_create(KS_ORGANIC_N);
  return ks.engine[0] && ks.engine[1] && ks.texShow && ks.texPrev && ks.texNext && ks.texBack &&
         ks.tex565 && ks.rgb && (ks.rgbSmall || KS_ORGANIC_N == KO_SRC_N) && ks.rgbSharp && ks.frame[0] && ks.frame[1] && ks.organic;
}

static void ksFree() {
  for (int i = 0; i < 2; ++i) {
    ko_engine_destroy(ks.engine[i]);
    ks.engine[i] = nullptr;
    ksRelease(ks.frame[i]);
    ks.frame[i] = nullptr;
  }
  ksRelease(ks.texShow);
  ksRelease(ks.texPrev);
  ksRelease(ks.texNext);
  ksRelease(ks.texBack);
  ksRelease(ks.tex565);
  ksRelease(ks.rgb);
  ksRelease(ks.rgbSmall);
  ksRelease(ks.rgbSharp);
  ks.texShow = ks.texPrev = ks.texNext = ks.texBack = nullptr;
  ks.tex565 = nullptr;
  ks.rgb = ks.rgbSmall = ks.rgbSharp = nullptr;
  if (ks.organic) ko_organic_destroy(ks.organic);
  ks.organic = nullptr;
}

// One showing of the scene: allocate, run until stopped, free everything.
static void ksRun() {
  if (!ksAllocate()) {
    ksFree();
    ks.failedMs = millis() | 1;
    ks.state = KS_OFF;
    return;
  }
  {
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
    ks.frontFrame = 0;
    ks.lastUs = 0;
    ks.lastTexUs = micros();
    ks.busy = KS_BUSY_NONE;
  }
  ks.lastFrameMs = millis();
  ks.state = KS_RUNNING;
  xTaskNotifyGive(ks.drawTask);

  for (;;) {
    // Asked to stop, or nobody has taken a frame for a while (a full-screen
    // page, another mode): let go. Only this task moves RUNNING -> STOPPING.
    if (ks.state == KS_RUNNING && (ks.stopReq || millis() - ks.lastFrameMs > KS_IDLE_STOP_MS)) {
      xSemaphoreTake(ks.lock, portMAX_DELAY);
      ks.state = KS_STOPPING;
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
      return;
    }
    // Giving way, or nobody is looking: no textures, no fold tables.
    if (ksBusyReason() || millis() - ks.lastFrameMs > KS_IDLE_PAUSE_MS) {
      vTaskDelay(pdMS_TO_TICKS(KS_BUSY_POLL_MS));
      continue;
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

static void ksWorkerTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // a start from the loop
    if (ks.state == KS_STARTING) ksRun();
  }
}

// First showing only: make both tasks, stacks in PSRAM (the framework allows
// it: CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY). Neither task touches flash,
// so a PSRAM stack is safe.
// FreeRTOS requires control blocks in internal RAM: a heap one could fall back
// to PSRAM when internal RAM runs out, so they are static.
static StaticTask_t ksWorkerTcb, ksDrawTcb;
static bool ksCreateTasks() {
  StackType_t *ws = (StackType_t *)ksPsram(KS_WORKER_STACK);
  StackType_t *ds = (StackType_t *)ksPsram(KS_DRAW_STACK);
  if (!ws || !ds) {
    ksRelease(ws);
    ksRelease(ds);
    return false;
  }
  ks.drawTask = xTaskCreateStaticPinnedToCore(ksDrawTask, "kaleido_draw", KS_DRAW_STACK, nullptr,
                                              KS_TASK_PRIORITY, ds, &ksDrawTcb, 0);
  ks.workerTask = xTaskCreateStaticPinnedToCore(ksWorkerTask, "kaleido", KS_WORKER_STACK, nullptr,
                                                KS_WORKER_PRIORITY, ws, &ksWorkerTcb, 0);
  return ks.drawTask && ks.workerTask;
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
    if (!ks.workerTask && !ksCreateTasks()) {
      ks.failedMs = nowMs | 1;
      return false;
    }
    ks.failedMs = 0;
    ks.stopReq = false;
    ks.lastFrameMs = nowMs;
    ks.lookReq = ks.lookIndex % KS_LOOKS;
    ks.state = KS_STARTING;
    xTaskNotifyGive(ks.workerTask);
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
  if (ks.state != KS_OFF) ks.stopReq = true;   // the worker acts on it
}

// For GET /state. Reading starts a fresh window.
inline String kaleidoSceneJson() {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  const float secs = lastMs ? (now - lastMs) / 1000.0f : 0.0f;
  const uint32_t frames = ks.frames, textures = ks.textures, copies = ks.copies;
  String json = String("{\"state\":") + String(ks.state) +
                ",\"busy\":" + String(ks.state == KS_RUNNING ? ks.busy : 0) +
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
//   h  pretend HTTPS is open (Kaleido eases) on/off
//   u  pretend a firmware upload runs (Kaleido stops) on/off
//   e/E ease ratio -/+ (sleep after each frame while easing, in draw times)
//   L  low power (pendant) mode on/off: saved, then the bot restarts
void setBotBackgroundStyle(uint8_t style);
uint8_t getBotBackgroundStyle();
#include <Preferences.h>
inline void kaleidoDevSerial() {
  static uint32_t lastMs = 0;
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'k') setBotBackgroundStyle(BG_STYLE_KALEIDO);
    else if (c == 'a') setBotBackgroundStyle(4);
    else if (c == 'b') setBotBackgroundStyle(0);
    else if (c == 'n') ks.lookReq = (ks.lookIndex + 1) % KS_LOOKS;
    else if (c == 'p') partyMode = !partyMode;
    else if (c == 'e' && ksEase > 1) ksEase--;
    else if (c == 'E' && ksEase < 20) ksEase++;
    else if (c == 'h') ksFakeBusy = ksFakeBusy ? KS_BUSY_NONE : KS_BUSY_HTTPS;
    else if (c == 'u') ksFakeBusy = ksFakeBusy ? KS_BUSY_NONE : KS_BUSY_UPLOAD;
    else if (c == 'L') {
      Preferences prefs;
      if (prefs.begin("vizbot", false)) {
        prefs.putBool("lowPwr", !prefs.getBool("lowPwr", false));
        prefs.end();
      }
      ESP.restart();
    }
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
  // Each HTTPS connection's length (cloud sync, cloud update), to compare
  // syncs with Kaleido showing and without.
  static uint32_t httpsStart = 0, httpsMinHeap = 0;
  if (memHeld(MEM_TLS) > 0) {
    if (!httpsStart) { httpsStart = now | 1; httpsMinHeap = UINT32_MAX; }
    const uint32_t h = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (h < httpsMinHeap) httpsMinHeap = h;
  } else if (httpsStart) {
    Serial.printf("KHTTPS ms=%u bg=%u kfps-state=%u busy=%u minHeap=%u\n", (unsigned)(now - httpsStart),
                  (unsigned)getBotBackgroundStyle(), (unsigned)ks.state, (unsigned)ks.busy, (unsigned)httpsMinHeap);
    httpsStart = 0;
  }
  if (now - lastMs < 5000) return;
  lastMs = now;
  Serial.printf("KSTAT ease=%u mhz=%u bg=%u party=%u/%u tls=%u kmem=%u stackFree=%u/%u heap=%u heapMin=%u heapBlock=%u psramFree=%u vivid=%.2f black=%.2f gamma=%.2f sharp=%.2f loop=%s kaleido=%s\n",
                (unsigned)ksEase, (unsigned)getCpuFrequencyMhz(), (unsigned)getBotBackgroundStyle(), (unsigned)partyMode, (unsigned)partyBlob.active,
                (unsigned)memHeld(MEM_TLS), (unsigned)memHeld(MEM_KALEIDO),
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
