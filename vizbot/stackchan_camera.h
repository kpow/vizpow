#ifndef STACKCHAN_CAMERA_H
#define STACKCHAN_CAMERA_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <Preferences.h>
#include "soc/gdma_struct.h"
#include "soc/lcd_cam_struct.h"
#include "soc/system_struct.h"
#include "config.h"
#include "system_status.h"

// ============================================================================
// GC0308 camera (CoreS3) — photos for vizbrain's `look` tool, on demand.
// ============================================================================
// Pins and settings are M5's own (m5stack/M5CoreS3 src/utility/GC0308.cpp).
// The sensor's SCCB control bus IS the CoreS3 internal I2C (SDA 12 / SCL 11),
// which M5Unified, the touch panel, the IMU and the Stackchan peripherals all
// use. M5's recipe releases that bus, lets esp_camera_init configure the
// sensor, and the bus is then taken back. After init the camera only needs its
// parallel (DVP) pins, so SCCB is never touched again.
//
// Frames are QVGA RGB565 in PSRAM. Photos are JPEG-encoded on demand.
//
// Motion: every SC_CAM_MOTION_MS the frame is reduced to a 32x24 luma grid and
// compared with the previous one. A change confined to part of the view is
// motion; a change across most of the view is the head turning or the lights
// changing, and is ignored. Frames taken while the head is moving are skipped
// (scHeadBusyUntilMs, set by every head move in stackchan_base.h).
// An "arrival" is sustained motion after SC_CAM_QUIET_MS without any. vizbrain
// then looks (a photo) and only greets if a person is actually there.
// ============================================================================

#define SC_CAM_GRID_W        32
#define SC_CAM_GRID_H        24
#define SC_CAM_MOTION_MS     500      // motion check period
#define SC_CAM_CELL_DELTA    22       // luma change that counts a cell as changed
#define SC_CAM_MIN_CELLS     45       // >= ~6% of cells = motion (idle noise peaks ~22)
#define SC_CAM_GLOBAL_CELLS  420      // >= ~55% of cells = head moved / lights changed
#define SC_CAM_QUIET_MS      (3UL * 60UL * 1000UL)   // quiet this long, then motion = arrival
#define SC_CAM_SUSTAIN       2        // motion in this many of the last 3 checks (~1 s)
#define SC_CAM_BUSY_LEVEL    120      // cells this active are flickering lights/screens: ignored

extern volatile uint32_t scHeadBusyUntilMs;   // stackchan_base.h

static SemaphoreHandle_t scCamLock = nullptr;

struct ScCamMotion {
  uint8_t  prev[SC_CAM_GRID_W * SC_CAM_GRID_H];
  uint8_t  activity[SC_CAM_GRID_W * SC_CAM_GRID_H];   // how often each cell changes (decays)
  uint8_t  recent = 0;                                // bit history of motion checks
  bool     havePrev = false;
  uint32_t maskedCells = 0;
  uint32_t lastMotionMs = 0;
  uint32_t lastChangedCells = 0;
  uint32_t frames = 0;
  uint32_t arrivals = 0;
  volatile bool arrivalPending = false;   // consumed by voice_client.h
  volatile uint32_t arrivalQuietMin = 0;
  volatile bool paused = false;           // set while a voice turn runs
};
static ScCamMotion scCamMotion;

extern bool i2cAcquire(uint32_t timeoutMs);   // task_manager.h
extern void i2cRelease();
extern void i2cSetLongHold(bool on);

static bool scCamDetected = false;           // sensor answered at boot
static volatile bool scCamRunning = false;   // driver up (only during a photo)

#define SC_CAM_SETTLE_FRAMES 8               // frames dropped after start so exposure settles

// Saved switch (NVS "camOn", default on). Off = no photos, camera never started.
// /bot/camera/enable sets it; applies at once.
inline bool scCameraEnabledPref() {
  Preferences p;
  if (!p.begin("vizbot", true)) return true;
  bool on = p.getBool("camOn", true);
  p.end();
  return on;
}

inline void scCameraApplyEnabled(bool on) {
  sysStatus.scCameraReady = scCamDetected && on;
}

static camera_config_t scCameraConfig() {
  camera_config_t c = {};
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.pin_xclk = -1;
  c.pin_sccb_sda = -1;   // -1 = reuse the bus already on sccb_i2c_port (SDA 12 / SCL 11)
  c.pin_sccb_scl = -1;
  c.pin_d7 = 47;
  c.pin_d6 = 48;
  c.pin_d5 = 16;
  c.pin_d4 = 15;
  c.pin_d3 = 42;
  c.pin_d2 = 41;
  c.pin_d1 = 40;
  c.pin_d0 = 39;
  c.pin_vsync = 46;
  c.pin_href = 38;
  c.pin_pclk = 45;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size = FRAMESIZE_QVGA;
  c.jpeg_quality = 0;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  c.sccb_i2c_port = 1;   // M5's internal bus (I2C_NUM_1 on the S3)
  return c;
}

// GDMA peripheral id of LCD_CAM in a channel's peri_sel register (63 = none).
#define SC_GDMA_PERI_LCD_CAM 5
#define SC_GDMA_PERI_NONE    63

// esp32-camera connects its RX channel to LCD_CAM by writing peri_sel directly
// (it never calls gdma_connect), and ll_cam_deinit() frees the channel without
// clearing it. The channel is then still wired to the camera. The next start
// gets whichever channel is free; when that is a different one (the mic's I2S
// channel is free during a voice turn), two channels select LCD_CAM, the old
// one takes the data and the new one never completes a buffer:
// "cam_hal: FB-SIZE: 0 != 153600" and no frame. Disconnect after every stop.
static void scCameraReleaseDma() {
  for (int i = 0; i < SOC_GDMA_PAIRS_PER_GROUP_MAX; i++) {
    if (GDMA.channel[i].in.peri_sel.sel == SC_GDMA_PERI_LCD_CAM) {
      GDMA.channel[i].in.peri_sel.sel = SC_GDMA_PERI_NONE;
    }
  }
}

// Start the driver; the sensor is set up over M5's I2C port 1 bus.
static esp_err_t scCameraStart() {
  if (scCamRunning) return ESP_OK;
  camera_config_t c = scCameraConfig();
  scCameraReleaseDma();    // a failed init frees its channel the same leaky way
  // PSRAM DMA mode would save the 32 KB internal bounce buffer, but this
  // driver hands over PSRAM-mode frames unchecked and they come out as shifted
  // noise (tried with and without GDMA burst). Default mode checks each frame.
  esp_camera_set_psram_mode(false);
  i2cSetLongHold(true);    // other I2C users wait for the sensor setup, not barge in
  bool held = i2cAcquire(1000);
  esp_err_t err = esp_camera_init(&c);
  if (held) i2cRelease();
  i2cSetLongHold(false);
  scCamRunning = (err == ESP_OK);
  return err;
}

// Stop the driver and free its buffers. The camera only leaves the shared
// bus; M5 keeps it.
static void scCameraStop() {
  if (!scCamRunning) return;
  i2cSetLongHold(true);
  bool held = i2cAcquire(1000);
  esp_camera_deinit();
  scCameraReleaseDma();
  scCamRunning = false;
  if (held) i2cRelease();
  i2cSetLongHold(false);
}

inline bool scInitCamera() {
  scCamLock = xSemaphoreCreateMutex();
  esp_err_t err = scCameraStart();
  scCamDetected = (err == ESP_OK);
  scCameraStop();
  scCameraApplyEnabled(scCameraEnabledPref());
  scCamMotion.lastMotionMs = millis();   // assume someone's here at boot: no instant "arrival"
  if (!scCamDetected) {
    Serial.printf("  Camera: init failed (%s)\n", esp_err_to_name(err));
    return false;
  }
  Serial.printf("  Camera: GC0308 QVGA, on demand%s\n", sysStatus.scCameraReady ? "" : " (disabled, camOn=0)");
  return true;
}

// ---- DMA diagnostics (GET /bot/camera/diag) -------------------------------------
// Register snapshot of all 5 GDMA channel pairs + LCD_CAM, taken at points in the
// last photo. peri_sel: 0 SPI2, 1 SPI3, 2 UHCI0, 3 I2S0, 4 I2S1, 5 LCD_CAM, 6 AES,
// 7 SHA, 8 ADC, 9 RMT, 63 none.
static char scCamDiag[3072];
static size_t scCamDiagLen = 0;

static void scCamDiagf(const char* fmt, ...) {
  if (scCamDiagLen >= sizeof(scCamDiag) - 1) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(scCamDiag + scCamDiagLen, sizeof(scCamDiag) - scCamDiagLen, fmt, ap);
  va_end(ap);
  if (n > 0) scCamDiagLen = min(sizeof(scCamDiag) - 1, scCamDiagLen + (size_t)n);
}

static void scCamDiagSnap(const char* tag) {
  scCamDiagf("[%s t=%lu core=%d] dmaClk=%d dmaRst=%d camCtrl1=%08lx lcInt ena=%08lx raw=%08lx\n",
             tag, (unsigned long)millis(), xPortGetCoreID(),
             (int)SYSTEM.perip_clk_en1.dma_clk_en, (int)SYSTEM.perip_rst_en1.dma_rst,
             (unsigned long)LCD_CAM.cam_ctrl1.val, (unsigned long)LCD_CAM.lc_dma_int_ena.val,
             (unsigned long)LCD_CAM.lc_dma_int_raw.val);
  for (int i = 0; i < 5; i++) {
    scCamDiagf("  ch%d in:sel=%lu conf0=%08lx ena=%08lx raw=%08lx link=%08lx state=%08lx | out:sel=%lu\n", i,
               (unsigned long)GDMA.channel[i].in.peri_sel.val, (unsigned long)GDMA.channel[i].in.conf0.val,
               (unsigned long)GDMA.channel[i].in.int_ena.val, (unsigned long)GDMA.channel[i].in.int_raw.val,
               (unsigned long)GDMA.channel[i].in.link.val, (unsigned long)GDMA.channel[i].in.state.val,
               (unsigned long)GDMA.channel[i].out.peri_sel.val);
  }
}

// JPEG of a fresh frame: starts the camera, lets exposure settle, grabs one
// frame and stops it again (~1 s). The caller frees *out with free(). quality 1-100.
inline bool scCameraJpeg(uint8_t** out, size_t* len, uint8_t quality = 80) {
  if (!sysStatus.scCameraReady || !scCamLock) return false;
  if (xSemaphoreTake(scCamLock, pdMS_TO_TICKS(3000)) != pdTRUE) return false;
  bool ok = false;
  esp_err_t err = scCameraStart();
  if (err != ESP_OK) {
    Serial.printf("[Camera] start failed (%s), free heap %u, max block %u\n", esp_err_to_name(err),
                  (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  } else {
    scCamDiagLen = 0;
    scCamDiagf("heap=%u maxBlock=%u\n", (unsigned)ESP.getFreeHeap(),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    scCamDiagSnap("started");
    camera_fb_t* fb = nullptr;
    for (int i = 0; i <= SC_CAM_SETTLE_FRAMES; i++) {
      fb = esp_camera_fb_get();          // the driver waits up to 4 s for a frame
      if (!fb) { scCamDiagf("frame %d: none\n", i); scCamDiagSnap("no-frame"); break; }
      if (i == 0) scCamDiagSnap("frame0");
      if (i < SC_CAM_SETTLE_FRAMES) { esp_camera_fb_return(fb); fb = nullptr; }
    }
    if (fb) {
      ok = frame2jpg(fb, quality, out, len);
      esp_camera_fb_return(fb);
    }
    scCameraStop();
  }
  xSemaphoreGive(scCamLock);
  return ok;
}

// One motion check. Returns true when this check counts as an arrival.
inline bool scCameraMotionTick() {
  if (!scCamRunning || scCamMotion.paused) return false;
  uint32_t now = millis();
  if ((int32_t)(scHeadBusyUntilMs - now) > 0) {   // head moving: view is shifting
    scCamMotion.havePrev = false;
    return false;
  }
  if (xSemaphoreTake(scCamLock, pdMS_TO_TICKS(200)) != pdTRUE) return false;
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { xSemaphoreGive(scCamLock); return false; }

  // Reduce QVGA RGB565 to a 32x24 luma grid (10x10 pixel blocks, sampled 2x2).
  static uint8_t grid[SC_CAM_GRID_W * SC_CAM_GRID_H];
  const uint16_t* px = (const uint16_t*)fb->buf;
  const int W = fb->width, H = fb->height;
  const int bw = W / SC_CAM_GRID_W, bh = H / SC_CAM_GRID_H;
  for (int gy = 0; gy < SC_CAM_GRID_H; gy++) {
    for (int gx = 0; gx < SC_CAM_GRID_W; gx++) {
      uint32_t sum = 0;
      for (int sy = 0; sy < 2; sy++) {
        for (int sx = 0; sx < 2; sx++) {
          uint16_t v = px[(gy * bh + sy * bh / 2 + bh / 4) * W + gx * bw + sx * bw / 2 + bw / 4];
          v = (v >> 8) | (v << 8);                       // sensor bytes are big-endian
          uint8_t r = (v >> 11) << 3, g = ((v >> 5) & 0x3F) << 2, b = (v & 0x1F) << 3;
          sum += (r * 77 + g * 150 + b * 29) >> 8;
        }
      }
      grid[gy * SC_CAM_GRID_W + gx] = sum / 4;
    }
  }
  esp_camera_fb_return(fb);
  xSemaphoreGive(scCamLock);
  scCamMotion.frames++;

  bool arrival = false;
  if (scCamMotion.havePrev) {
    // Cells that change all the time (LED strips, screens, a TV) are learned
    // and ignored, so they can't keep the room from ever being "quiet".
    uint32_t changed = 0, all = 0, masked = 0;
    for (int i = 0; i < SC_CAM_GRID_W * SC_CAM_GRID_H; i++) {
      bool diff = abs((int)grid[i] - (int)scCamMotion.prev[i]) > SC_CAM_CELL_DELTA;
      uint8_t& a = scCamMotion.activity[i];
      bool busy = a >= SC_CAM_BUSY_LEVEL;
      if (busy) masked++;
      if (diff) { all++; if (!busy) changed++; }
      a = diff ? (uint8_t)min(255, a + 40) : (uint8_t)(a - (a >> 4) - (a ? 1 : 0));
    }
    scCamMotion.lastChangedCells = changed;
    scCamMotion.maskedCells = masked;
    bool moving = changed >= SC_CAM_MIN_CELLS && all < SC_CAM_GLOBAL_CELLS;
    scCamMotion.recent = (uint8_t)((scCamMotion.recent << 1) | (moving ? 1 : 0)) & 0x07;
    // Sustained (2 of the last 3 checks) so a single flick of light doesn't count.
    if (__builtin_popcount(scCamMotion.recent) >= SC_CAM_SUSTAIN) {
      uint32_t quiet = now - scCamMotion.lastMotionMs;
      if (quiet >= SC_CAM_QUIET_MS) {
        scCamMotion.arrivals++;
        scCamMotion.arrivalQuietMin = quiet / 60000;
        arrival = true;
      }
      scCamMotion.lastMotionMs = now;
    }
  }
  memcpy(scCamMotion.prev, grid, sizeof(grid));
  scCamMotion.havePrev = true;
  return arrival;
}

#endif // BOARD_HAS_STACKCHAN_BASE
#endif // STACKCHAN_CAMERA_H
