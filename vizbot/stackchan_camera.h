#ifndef STACKCHAN_CAMERA_H
#define STACKCHAN_CAMERA_H

#ifdef BOARD_HAS_STACKCHAN_BASE

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_camera.h>
#include <img_converters.h>
#include "config.h"
#include "system_status.h"

// ============================================================================
// GC0308 camera (CoreS3) — photos for vizbrain's `look` tool, and a cheap
// motion check that notices when someone arrives at the desk.
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
// An "arrival" is motion after SC_CAM_QUIET_MS without any.
// ============================================================================

#define SC_CAM_GRID_W        32
#define SC_CAM_GRID_H        24
#define SC_CAM_MOTION_MS     500      // motion check period
#define SC_CAM_CELL_DELTA    22       // luma change that counts a cell as changed
#define SC_CAM_MIN_CELLS     45       // >= ~6% of cells = motion (idle noise peaks ~22)
#define SC_CAM_GLOBAL_CELLS  420      // >= ~55% of cells = head moved / lights changed
#define SC_CAM_QUIET_MS      (5UL * 60UL * 1000UL)   // quiet this long, then motion = arrival

extern volatile uint32_t scHeadBusyUntilMs;   // stackchan_base.h

static SemaphoreHandle_t scCamLock = nullptr;

struct ScCamMotion {
  uint8_t  prev[SC_CAM_GRID_W * SC_CAM_GRID_H];
  bool     havePrev = false;
  uint32_t lastMotionMs = 0;
  uint32_t lastChangedCells = 0;
  uint32_t frames = 0;
  uint32_t arrivals = 0;
  volatile bool arrivalPending = false;   // consumed by voice_client.h
  volatile uint32_t arrivalQuietMin = 0;
  volatile bool paused = false;           // set while a voice turn runs
};
static ScCamMotion scCamMotion;

inline bool scInitCamera() {
  camera_config_t c = {};
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.pin_xclk = -1;
  c.pin_sccb_sda = 12;
  c.pin_sccb_scl = 11;
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
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  c.sccb_i2c_port = -1;

  M5.In_I2C.release();
  esp_err_t err = esp_camera_init(&c);
  M5.In_I2C.begin();    // take the internal bus back for touch, IMU, PMIC, head sensors

  sysStatus.scCameraReady = (err == ESP_OK);
  if (!sysStatus.scCameraReady) {
    Serial.printf("  Camera: init failed (%s)\n", esp_err_to_name(err));
    return false;
  }
  scCamLock = xSemaphoreCreateMutex();
  scCamMotion.lastMotionMs = millis();   // assume someone's here at boot: no instant "arrival"
  Serial.println("  Camera: GC0308 QVGA ready");
  return true;
}

// JPEG of a fresh frame. The caller frees *out with free(). quality 1-100.
inline bool scCameraJpeg(uint8_t** out, size_t* len, uint8_t quality = 80) {
  if (!sysStatus.scCameraReady || !scCamLock) return false;
  if (xSemaphoreTake(scCamLock, pdMS_TO_TICKS(2000)) != pdTRUE) return false;
  // GRAB_LATEST keeps the newest frame, but drop one so the shot is from now.
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) { esp_camera_fb_return(fb); fb = esp_camera_fb_get(); }
  bool ok = false;
  if (fb) {
    ok = frame2jpg(fb, quality, out, len);
    esp_camera_fb_return(fb);
  }
  xSemaphoreGive(scCamLock);
  return ok;
}

// One motion check. Returns true when this check counts as an arrival.
inline bool scCameraMotionTick() {
  if (!sysStatus.scCameraReady || scCamMotion.paused) return false;
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
    uint32_t changed = 0;
    for (int i = 0; i < SC_CAM_GRID_W * SC_CAM_GRID_H; i++) {
      if (abs((int)grid[i] - (int)scCamMotion.prev[i]) > SC_CAM_CELL_DELTA) changed++;
    }
    scCamMotion.lastChangedCells = changed;
    if (changed >= SC_CAM_MIN_CELLS && changed < SC_CAM_GLOBAL_CELLS) {
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
