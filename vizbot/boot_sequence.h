#ifndef BOOT_SEQUENCE_H
#define BOOT_SEQUENCE_H

#include <Arduino.h>
#include <Wire.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "layout.h"
#include "system_status.h"
#ifdef CLOUD_ENABLED
#include "content_cache.h"
#include "cloud_client.h"
#endif
#ifdef BOARD_HAS_STACKCHAN_BASE
#include "stackchan_base.h"
#endif

// Global instance — defined here, extern'd via system_status.h.
// Zero-initialized: all bool fields false, numerics 0, IPAddress 0.0.0.0.
// Use {} (not a positional list) so adding SystemStatus fields stays safe.
SystemStatus sysStatus = {};

// Only compile boot sequence for LCD targets
#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)

#ifdef TARGET_CORES3
#include <M5Unified.h>
#include "bot_sounds.h"
#include "proximity_light.h"
#endif

// ============================================================================
// Boot Display Constants
// ============================================================================

#define BOOT_TEXT_SIZE     2
// BOOT_LINE_HEIGHT, BOOT_LEFT_MARGIN, BOOT_TOP_MARGIN, BOOT_STATUS_X from layout.h

// Colors (RGB565)
#define BOOT_COLOR_BG      0x0000  // Black
#define BOOT_COLOR_TITLE   0x07FF  // Cyan
#define BOOT_COLOR_LABEL   0xC618  // Light gray
#define BOOT_COLOR_DETAIL  0x7BEF  // Mid gray
#define BOOT_COLOR_OK      0x07E0  // Green
#define BOOT_COLOR_FAIL    0xF800  // Red
#define BOOT_COLOR_WARN    0xFFE0  // Yellow
#define BOOT_COLOR_READY   0x07FF  // Cyan
#define BOOT_COLOR_DEFER   0x7BEF  // Mid gray — neutral, not alarming

// ============================================================================
// Boot Display Helpers
// ============================================================================

// External GFX pointer (initialized in display_lcd.h before boot runs)
extern GfxDevice *gfx;

// Global stage counter (for [n/total] labels across all columns)
static uint8_t bootStageIndex = 0;

// Total stage count (compile-time, includes StackChan stages when applicable)
#if defined(BOARD_HAS_STACKCHAN_BASE)
static const uint8_t BOOT_TOTAL_STAGES = 21;  // 12 core + 9 StackChan
#elif defined(TARGET_CORES3) && defined(CLOUD_ENABLED)
static const uint8_t BOOT_TOTAL_STAGES = 12;
#elif defined(TARGET_CORES3)
static const uint8_t BOOT_TOTAL_STAGES = 10;
#elif defined(CLOUD_ENABLED)
static const uint8_t BOOT_TOTAL_STAGES = 11;
#else
static const uint8_t BOOT_TOTAL_STAGES = 9;
#endif

// Column-aware drawing state. On non-StackChan builds these equal the old
// constants and produce pixel-identical output. StackChan builds override
// them at the top of runBootSequence() for a compact two-column layout.
static int16_t bootLabelX     = BOOT_LEFT_MARGIN;
static int16_t bootStatusX    = BOOT_STATUS_X;
static uint8_t bootTextSz     = BOOT_TEXT_SIZE;
static uint8_t bootLineH      = BOOT_LINE_HEIGHT;
static bool    bootShowDetail  = true;
static uint8_t bootRowInCol    = 0;   // row within current column
static uint8_t bootMaxRow      = 0;   // max rows seen in any column

// Draw the boot header
void bootDrawHeader() {
  gfx->setTextSize(BOOT_TEXT_SIZE);
  gfx->setCursor(BOOT_LEFT_MARGIN, 6);
  gfx->setTextColor(BOOT_COLOR_TITLE);
  gfx->print("vizBot");

  #ifdef BOARD_HAS_STACKCHAN_BASE
  gfx->setTextSize(1);
  gfx->setCursor(LCD_WIDTH - 56, 2);
  gfx->setTextColor(BOOT_COLOR_TITLE);
  gfx->print("StackChan");
  gfx->setCursor(LCD_WIDTH - 26, 12);
  gfx->setTextColor(BOOT_COLOR_DETAIL);
  gfx->print("boot");
  #else
  gfx->setTextSize(1);
  gfx->setCursor(LCD_WIDTH - 42, 10);
  gfx->setTextColor(BOOT_COLOR_DETAIL);
  gfx->print("boot");
  #endif
}

// Draw a stage label: "[1/21] LCD"
void bootDrawStage(const char* label) {
  bootStageIndex++;
  bootRowInCol++;
  int16_t y = BOOT_TOP_MARGIN + (bootRowInCol - 1) * bootLineH;

  gfx->setTextSize(bootTextSz);
  gfx->setCursor(bootLabelX, y);
  gfx->setTextColor(BOOT_COLOR_LABEL);

  gfx->print("[");
  gfx->print(bootStageIndex);
  gfx->print("/");
  gfx->print(BOOT_TOTAL_STAGES);
  gfx->print("] ");
  gfx->print(label);
}

// Draw result for the current stage
void bootDrawResult(bool success, const char* detail = nullptr) {
  int16_t y = BOOT_TOP_MARGIN + (bootRowInCol - 1) * bootLineH;

  gfx->setTextSize(bootTextSz);
  gfx->setCursor(bootStatusX, y);
  gfx->setTextColor(success ? BOOT_COLOR_OK : BOOT_COLOR_FAIL);
  gfx->print(success ? "OK" : "FAIL");

  if (!success) sysStatus.failCount++;

  if (bootShowDetail && detail != nullptr) {
    gfx->setTextSize(1);
    gfx->setCursor(bootLabelX + 20, y + 14);
    gfx->setTextColor(BOOT_COLOR_DETAIL);
    gfx->print(detail);
  }
}

// Draw deferred/stub status — neutral color, does NOT increment failCount
void bootDrawDeferred() {
  int16_t y = BOOT_TOP_MARGIN + (bootRowInCol - 1) * bootLineH;

  gfx->setTextSize(bootTextSz);
  gfx->setCursor(bootStatusX, y);
  gfx->setTextColor(BOOT_COLOR_DEFER);
  gfx->print("DEFER");
}

// Switch to the right column (StackChan stages)
#ifdef BOARD_HAS_STACKCHAN_BASE
void bootBeginRightColumn() {
  if (bootRowInCol > bootMaxRow) bootMaxRow = bootRowInCol;
  bootLabelX  = 164;
  bootStatusX = 286;
  bootRowInCol = 0;
}
#endif

// Draw final boot summary at bottom of screen
void bootDrawSummary() {
  if (bootRowInCol > bootMaxRow) bootMaxRow = bootRowInCol;
  int16_t y = BOOT_TOP_MARGIN + bootMaxRow * bootLineH + 10;

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

  // Show IP — prefer STA IP if connected, otherwise AP IP
  if (sysStatus.staConnected) {
    gfx->setCursor(BOOT_LEFT_MARGIN, y + 34);
    gfx->setTextColor(BOOT_COLOR_OK);
    gfx->print("STA: ");
    gfx->print(sysStatus.staIP);
  } else if (sysStatus.wifiReady) {
    gfx->setCursor(BOOT_LEFT_MARGIN, y + 34);
    gfx->setTextColor(BOOT_COLOR_DETAIL);
    gfx->print("AP: ");
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
#ifndef TARGET_CORES3
extern SensorQMI8658 imu;
#endif
extern WebServer server;
extern DNSServer dnsServer;

// Forward declarations for functions defined elsewhere
extern void initLCD();
extern void setupWebServer();
extern void startDNS();
extern bool startMDNS();
extern bool initTouch();
extern bool bootAttemptSTA();

// Stage 1: LCD — already initialized before boot screen starts
bool bootStageLCD() {
  #ifdef TARGET_CORES3
  bool ok = true;
  #else
  bool ok = (gfx != nullptr);
  #endif
  sysStatus.lcdReady = ok;
  return ok;
}

// Stage 2: LEDs
bool bootStageLEDs() {
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

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
  #ifdef TARGET_CORES3
  sysStatus.i2cReady = true;
  return true;
  #else
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);

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
  #endif
}

// Stage 4: IMU
bool bootStageIMU() {
  if (!sysStatus.i2cReady) {
    sysStatus.imuReady = false;
    return false;
  }

  #ifdef TARGET_CORES3
  float ax = 0, ay = 0, az = 0;
  M5.Imu.getAccel(&ax, &ay, &az);
  bool ok = (ax != 0.0f || ay != 0.0f || az != 0.0f);
  #else
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
  #endif

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

// Stage 6: WiFi AP (preserves STA if already connected)
bool bootStageWiFi() {
  if (sysStatus.staConnected) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }
  delay(100);

  bool ok = WiFi.softAP(apSSID, WIFI_PASSWORD, 1, false, 4);
  if (ok) {
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_TX_POWER);

    uint8_t retries = 0;
    while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0) && retries < 20) {
      delay(100);
      retries++;
    }
    sysStatus.apIP = WiFi.softAPIP();

    if (sysStatus.apIP == IPAddress(0, 0, 0, 0)) {
      ok = false;
    }

    DBG("WiFi AP IP: ");
    DBGLN(sysStatus.apIP);
    DBG("WiFi AP MAC: ");
    DBGLN(WiFi.softAPmacAddress());
    DBG("TX Power: ");
    DBG(WiFi.getTxPower());
    DBG("dBm*4, retries: ");
    DBGLN(retries);
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

// Stage 8: DNS + mDNS (captive portal)
bool bootStageDNS() {
  if (!sysStatus.wifiReady) {
    sysStatus.dnsReady = false;
    sysStatus.mdnsReady = false;
    return false;
  }

  startDNS();
  sysStatus.dnsReady = true;
  sysStatus.mdnsReady = startMDNS();
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
  bootRowInCol = 0;
  bootMaxRow = 0;
  sysStatus.failCount = 0;

  // StackChan builds: compact two-column layout (text size 1, no detail sub-lines).
  // Left column = core stages, right column = StackChan stages.
  #ifdef BOARD_HAS_STACKCHAN_BASE
  bootTextSz     = 1;
  bootLineH      = 13;
  bootShowDetail = false;
  bootLabelX     = 2;
  bootStatusX    = 120;
  #endif

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
  #ifdef TARGET_CORES3
  bootDrawResult(ok, ok ? "RGB LED GPIO38" : nullptr);
  #else
  bootDrawResult(ok, ok ? "64 WS2812B GPIO14" : nullptr);
  #endif
  delay(80);

  // --- Stage 3: I2C Bus ---
  bootDrawStage("I2C Bus");
  ok = bootStageI2C();
  #ifdef TARGET_CORES3
  bootDrawResult(ok, ok ? "SDA:12 SCL:11" : "No devices found");
  #else
  bootDrawResult(ok, ok ? "SDA:11 SCL:10" : "No devices found");
  #endif
  delay(80);

  // --- Stage 4: IMU ---
  bootDrawStage("IMU");
  ok = bootStageIMU();
  #ifdef TARGET_CORES3
  bootDrawResult(ok, ok ? "BMI270 M5Unified" : "Sensor missing");
  #else
  bootDrawResult(ok, ok ? "QMI8658 @ 0x6B" : "Sensor missing");
  #endif
  delay(80);

  // --- Stage 5: Touch ---
  bootDrawStage("Touch");
  ok = bootStageTouch();
  #if defined(TOUCH_ENABLED)
  #ifdef TARGET_CORES3
  bootDrawResult(ok, ok ? "Cap. M5Unified" : "No response");
  #else
  bootDrawResult(ok, ok ? "CST816 @ 0x15" : "No response");
  #endif
  #else
  bootDrawResult(false, "Disabled in config");
  #endif
  delay(80);

  // --- Stage 6 (Core S3 only): Sensors — Speaker, Mic, Proximity/Light ---
  #ifdef TARGET_CORES3
  {
    bootDrawStage("Sensors");
    #ifdef MIDI_SYNTH_ENABLED
    midiSynth.init();
    sysStatus.midiReady = midiSynth.ready;
    #endif
    botSounds.init();
    sysStatus.speakerReady = true;
    audioSpectrum.init();
    sysStatus.micReady = true;
    proxLight.init();
    sysStatus.proxLightReady = proxLight.initialized;
    char detail[48];
    snprintf(detail, sizeof(detail), "%s+Mic%s",
      botSounds.useMidi ? "MIDI" : "Spkr",
      proxLight.initialized ? "+Prox" : "");
    bootDrawResult(true, detail);
    delay(80);
  }
  #endif

  // --- WiFi STA ---
  bootDrawStage("WiFi STA");
  ok = bootAttemptSTA();
  if (ok) {
    char ipStr[20];
    snprintf(ipStr, sizeof(ipStr), "%s", sysStatus.staIP.toString().c_str());
    bootDrawResult(true, ipStr);
  } else {
    bootDrawResult(false, "Connect failed");
  }
  delay(80);

  // --- WiFi AP ---
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

  // --- Web Server ---
  bootDrawStage("Web Srv");
  ok = bootStageWebServer();
  bootDrawResult(ok, ok ? "Port 80" : "No WiFi");
  delay(80);

  // --- DNS + mDNS (Captive Portal) ---
  bootDrawStage("Portal");
  ok = bootStageDNS();
  if (ok) {
    char mdnsLabel[28];
    if (sysStatus.mdnsReady) {
      snprintf(mdnsLabel, sizeof(mdnsLabel), "DNS + %s.local", mdnsHostname);
      bootDrawResult(true, mdnsLabel);
    } else {
      bootDrawResult(true, "DNS only");
    }
  } else {
    bootDrawResult(false, "No WiFi");
  }
  delay(80);

#ifdef CLOUD_ENABLED
  // --- LittleFS ---
  bootDrawStage("Storage");
  ok = initContentCache();
  sysStatus.littlefsReady = ok;
  bootDrawResult(ok, ok ? "LittleFS 128K" : "Format failed");
  delay(80);

  // --- Cloud ---
  bootDrawStage("Cloud");
  if (sysStatus.littlefsReady) {
    initCloudClient();
    sysStatus.cloudRegistered = cloudMeta.registered;
    if (cloudMeta.registered) {
      bootDrawResult(true, "Cached ID");
    } else if (sysStatus.staConnected) {
      bootDrawResult(true, "Will register");
    } else {
      bootDrawResult(true, "No WiFi yet");
    }
  } else {
    bootDrawResult(false, "No storage");
  }
  delay(80);
#endif

  // ========================================================================
  // StackChan stages — right column, all stubs reporting DEFER
  // ========================================================================
#ifdef BOARD_HAS_STACKCHAN_BASE
  bootBeginRightColumn();

  // Right-column header
  gfx->setTextSize(1);
  gfx->setCursor(bootLabelX, BOOT_TOP_MARGIN - 10);
  gfx->setTextColor(BOOT_COLOR_TITLE);
  gfx->print("StackChan");

  // --- IO Expander (PY32L020) ---
  bootDrawStage("IO Exp");
  ok = scInitIoExpander();
  bootDrawResult(ok);
  delay(40);

  // --- VM_EN servo power rail ---
  bootDrawStage("VM_EN");
  ok = scInitVmEn();
  bootDrawResult(ok);
  delay(40);

  // --- SCS0009 Servo X (yaw) ---
  bootDrawStage("Servo X");
  ok = scInitServoX();
  bootDrawResult(ok);
  delay(40);

  // --- SCS0009 Servo Y (pitch) ---
  bootDrawStage("Servo Y");
  ok = scInitServoY();
  bootDrawResult(ok);
  delay(40);

  // --- WS2812C base LED ring ---
  bootDrawStage("Base LED");
  ok = scInitBaseLeds();
  bootDrawResult(ok);
  delay(40);

  // --- Si12T head touch ---
  bootDrawStage("Touch SC");
  ok = scInitHeadTouch();
  bootDrawResult(ok);
  delay(40);

  // --- INA226 battery monitor ---
  bootDrawStage("Battery");
  ok = scInitBatteryMon();
  bootDrawResult(ok);
  delay(40);

  // --- GC0308 camera (Phase 4 stub) ---
  bootDrawStage("Camera");
  scInitCamera();
  bootDrawDeferred();
  delay(40);

  // --- LittleFS photo storage (Phase 4 stub) ---
  bootDrawStage("Photo FS");
  scInitPhotoFs();
  bootDrawDeferred();
  delay(40);
#endif

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

  // If STA connected at boot, seed the provisioning state machine so the
  // AP linger timer starts.  After WIFI_AP_LINGER_MS the AP will shut down
  // and the device switches to STA-only — no manual reconnection needed.
  if (sysStatus.staConnected && sysStatus.wifiReady) {
    wifiProv.connectedAtMs = millis();
    wifiProv.state = PROV_CONNECTED;
    DBGLN("STA connected at boot — AP linger timer started");
  }

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
