#ifndef BOOT_SEQUENCE_H
#define BOOT_SEQUENCE_H

#include <Arduino.h>
#include <Wire.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"
#include "system_status.h"

// Global instance — defined here, extern'd via system_status.h
SystemStatus sysStatus = {false, false, false, false, false, false, false, IPAddress(0,0,0,0), 0, 0};

// Only compile boot sequence for LCD targets
#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

#include <Arduino_GFX_Library.h>

// ============================================================================
// Boot Display Constants
// ============================================================================

#define BOOT_TEXT_SIZE     2
#define BOOT_LINE_HEIGHT   22
#define BOOT_LEFT_MARGIN   10
#define BOOT_TOP_MARGIN    30
#define BOOT_STATUS_X      200   // Right-aligned status column

// Colors (RGB565)
#define BOOT_COLOR_BG      0x0000  // Black
#define BOOT_COLOR_TITLE   0x07FF  // Cyan
#define BOOT_COLOR_LABEL   0xC618  // Light gray
#define BOOT_COLOR_DETAIL  0x7BEF  // Mid gray
#define BOOT_COLOR_OK      0x07E0  // Green
#define BOOT_COLOR_FAIL    0xF800  // Red
#define BOOT_COLOR_WARN    0xFFE0  // Yellow
#define BOOT_COLOR_READY   0x07FF  // Cyan

// ============================================================================
// Boot Display Helpers
// ============================================================================

// External GFX pointer (initialized in display_lcd.h before boot runs)
extern Arduino_GFX *gfx;

static uint8_t bootStageIndex = 0;
static const uint8_t BOOT_TOTAL_STAGES = 7;

// Draw the boot header
void bootDrawHeader() {
  gfx->setTextSize(BOOT_TEXT_SIZE);
  gfx->setCursor(BOOT_LEFT_MARGIN, 6);
  gfx->setTextColor(BOOT_COLOR_TITLE);
  gfx->print("vizBot");

  gfx->setTextSize(1);
  gfx->setCursor(LCD_WIDTH - 42, 10);
  gfx->setTextColor(BOOT_COLOR_DETAIL);
  gfx->print("boot");
}

// Draw a stage label: "[1/7] LCD"
void bootDrawStage(const char* label) {
  bootStageIndex++;
  int16_t y = BOOT_TOP_MARGIN + (bootStageIndex - 1) * BOOT_LINE_HEIGHT;

  gfx->setTextSize(BOOT_TEXT_SIZE);
  gfx->setCursor(BOOT_LEFT_MARGIN, y);
  gfx->setTextColor(BOOT_COLOR_LABEL);

  // Stage number
  gfx->print("[");
  gfx->print(bootStageIndex);
  gfx->print("/");
  gfx->print(BOOT_TOTAL_STAGES);
  gfx->print("] ");
  gfx->print(label);
}

// Draw result for the current stage
void bootDrawResult(bool success, const char* detail = nullptr) {
  int16_t y = BOOT_TOP_MARGIN + (bootStageIndex - 1) * BOOT_LINE_HEIGHT;

  gfx->setTextSize(BOOT_TEXT_SIZE);
  gfx->setCursor(BOOT_STATUS_X, y);
  gfx->setTextColor(success ? BOOT_COLOR_OK : BOOT_COLOR_FAIL);
  gfx->print(success ? "OK" : "FAIL");

  if (!success) sysStatus.failCount++;

  // Optional detail line below in smaller text
  if (detail != nullptr) {
    gfx->setTextSize(1);
    gfx->setCursor(BOOT_LEFT_MARGIN + 20, y + 14);
    gfx->setTextColor(BOOT_COLOR_DETAIL);
    gfx->print(detail);
  }
}

// Draw final boot summary at bottom of screen
void bootDrawSummary() {
  int16_t y = BOOT_TOP_MARGIN + BOOT_TOTAL_STAGES * BOOT_LINE_HEIGHT + 10;

  gfx->setTextSize(BOOT_TEXT_SIZE);
  gfx->setCursor(BOOT_LEFT_MARGIN, y);

  if (sysStatus.failCount == 0) {
    gfx->setTextColor(BOOT_COLOR_READY);
    gfx->print("All systems go.");
  } else {
    gfx->setTextColor(BOOT_COLOR_WARN);
    gfx->print(sysStatus.failCount);
    gfx->print(" failed - degraded");
  }

  // Show boot time
  gfx->setTextSize(1);
  gfx->setCursor(BOOT_LEFT_MARGIN, y + 22);
  gfx->setTextColor(BOOT_COLOR_DETAIL);
  gfx->print("Boot: ");
  gfx->print(sysStatus.bootTimeMs);
  gfx->print("ms");

  // Show IP if WiFi is up
  if (sysStatus.wifiReady) {
    gfx->setCursor(BOOT_LEFT_MARGIN, y + 34);
    gfx->setTextColor(BOOT_COLOR_DETAIL);
    gfx->print("IP: ");
    gfx->print(sysStatus.apIP);
  }
}

// ============================================================================
// Boot Stage Functions
// ============================================================================
// Each returns true/false. The boot sequence calls them in order.
// LCD must be initialized BEFORE this runs (chicken-and-egg: we need LCD to
// show boot status, so initLCD() is called first, then we draw the boot screen).

// External references for hardware init
extern CRGB leds[];
extern SensorQMI8658 imu;
extern WebServer server;

// Forward declarations for functions defined elsewhere
extern void initLCD();
extern void setupWebServer();
extern bool initTouch();

// Stage 1: LCD — already initialized before boot screen starts
bool bootStageLCD() {
  // LCD was already initialized to show this boot screen.
  // Verify gfx pointer is valid.
  bool ok = (gfx != nullptr);
  sysStatus.lcdReady = ok;
  return ok;
}

// Stage 2: LEDs
bool bootStageLEDs() {
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  // Quick sanity: if addLeds didn't crash, we're good.
  // Flash a brief white pixel to confirm data line works.
  leds[0] = CRGB::White;
  FastLED.show();
  delay(50);
  leds[0] = CRGB::Black;
  FastLED.show();

  sysStatus.ledsReady = true;
  return true;
}

// Stage 3: I2C Bus
bool bootStageI2C() {
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);

  // Scan for any device on the bus to verify it's alive
  bool found = false;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      found = true;
      break;
    }
  }

  sysStatus.i2cReady = found;
  return found;
}

// Stage 4: IMU (QMI8658)
bool bootStageIMU() {
  if (!sysStatus.i2cReady) {
    sysStatus.imuReady = false;
    return false;
  }

  bool ok = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (ok) {
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
  }

  sysStatus.imuReady = ok;
  return ok;
}

// Stage 5: Touch Controller
bool bootStageTouch() {
  #if defined(TOUCH_ENABLED)
  if (!sysStatus.i2cReady) {
    sysStatus.touchReady = false;
    return false;
  }

  bool ok = initTouch();
  sysStatus.touchReady = ok;
  return ok;
  #else
  sysStatus.touchReady = false;
  return false;
  #endif
}

// Stage 6: WiFi AP
bool bootStageWiFi() {
  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.setSleep(false);

  bool ok = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, 4);
  if (ok) {
    delay(300);  // Let AP stabilize
    sysStatus.apIP = WiFi.softAPIP();
  }

  sysStatus.wifiReady = ok;
  return ok;
}

// Stage 7: Web Server
bool bootStageWebServer() {
  if (!sysStatus.wifiReady) {
    sysStatus.webServerReady = false;
    return false;
  }

  setupWebServer();
  sysStatus.webServerReady = true;
  return true;
}

// ============================================================================
// Run Full Boot Sequence
// ============================================================================
// Call this from setup() AFTER initLCD(). Draws each stage to the LCD
// and populates sysStatus.

void runBootSequence() {
  uint32_t bootStart = millis();
  bootStageIndex = 0;
  sysStatus.failCount = 0;

  // Clear screen and draw header
  gfx->fillScreen(BOOT_COLOR_BG);
  bootDrawHeader();

  // --- Stage 1: LCD ---
  bootDrawStage("LCD");
  bool ok = bootStageLCD();
  bootDrawResult(ok);
  delay(80);

  // --- Stage 2: LEDs ---
  bootDrawStage("LEDs");
  ok = bootStageLEDs();
  bootDrawResult(ok, ok ? "64 WS2812B GPIO14" : nullptr);
  delay(80);

  // --- Stage 3: I2C Bus ---
  bootDrawStage("I2C Bus");
  ok = bootStageI2C();
  bootDrawResult(ok, ok ? "SDA:11 SCL:10" : "No devices found");
  delay(80);

  // --- Stage 4: IMU ---
  bootDrawStage("IMU");
  ok = bootStageIMU();
  bootDrawResult(ok, ok ? "QMI8658 @ 0x6B" : "Sensor missing");
  delay(80);

  // --- Stage 5: Touch ---
  bootDrawStage("Touch");
  ok = bootStageTouch();
  #if defined(TOUCH_ENABLED)
  bootDrawResult(ok, ok ? "CST816 @ 0x15" : "No response");
  #else
  bootDrawResult(false, "Disabled in config");
  #endif
  delay(80);

  // --- Stage 6: WiFi AP ---
  bootDrawStage("WiFi AP");
  ok = bootStageWiFi();
  if (ok) {
    char ipStr[20];
    snprintf(ipStr, sizeof(ipStr), "%s", sysStatus.apIP.toString().c_str());
    bootDrawResult(true, ipStr);
  } else {
    bootDrawResult(false, "AP start failed");
  }
  delay(80);

  // --- Stage 7: Web Server ---
  bootDrawStage("Web Srv");
  ok = bootStageWebServer();
  bootDrawResult(ok, ok ? "Port 80" : "No WiFi");
  delay(80);

  // --- Summary ---
  sysStatus.bootTimeMs = millis() - bootStart;
  bootDrawSummary();

  // Hold boot screen so user can read it
  delay(2000);

  // Clear for normal operation
  gfx->fillScreen(BOOT_COLOR_BG);

  // Mark WiFi enabled globally if it came up
  extern bool wifiEnabled;
  wifiEnabled = sysStatus.wifiReady;

  DBGLN("=== Boot sequence complete ===");
  DBG("Boot time: ");
  DBG(sysStatus.bootTimeMs);
  DBGLN("ms");
  DBG("Failures: ");
  DBGLN(sysStatus.failCount);
}

#else

// Stub for non-LCD targets
inline void runBootSequence() {}

#endif // DISPLAY_LCD_ONLY || DISPLAY_DUAL

#endif // BOOT_SEQUENCE_H
