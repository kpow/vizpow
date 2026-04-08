#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// Board Selection — defined by PlatformIO build_flags, do not uncomment here
// ============================================================================
// #define BOARD_ESP32S3_MATRIX       // Waveshare ESP32-S3-Matrix (8x8 LED)
// #define BOARD_ESP32S3_LCD_169      // Waveshare ESP32-S3-Touch-LCD-1.69
// #define BOARD_ESP32S3_LCD_13       // Waveshare ESP32-S3-LCD-1.3 (no touch, battery)
// #define BOARD_M5CORES3             // M5Stack Core S3

// ============================================================================
// Auto-derive TARGET from BOARD
// ============================================================================
#if defined(BOARD_ESP32S3_MATRIX)
  #define TARGET_LED
#elif defined(BOARD_ESP32S3_LCD_169) || defined(BOARD_ESP32S3_LCD_13)
  #define TARGET_LCD
#elif defined(BOARD_M5CORES3)
  #define TARGET_CORES3
#else
  #error "Select a board: BOARD_ESP32S3_MATRIX, BOARD_ESP32S3_LCD_169, BOARD_ESP32S3_LCD_13, or BOARD_M5CORES3"
#endif

// ============================================================================
// Target-level configuration (shared by all boards of same target)
// ============================================================================
#if defined(TARGET_LED)
  #define DISPLAY_LED_ONLY
  // Power-saving profile for battery-powered LED matrix
  #define POWER_SAVE_ENABLED
  #define AUTO_WIFI_USB_DETECT       // WiFi ON when USB host detected, OFF on battery
  #define USB_DETECT_DELAY_MS 1500   // Time to wait for USB host enumeration at boot
  #define DEFAULT_BRIGHTNESS 10
  #define MAX_LED_POWER_MA 200       // FastLED auto-scales to this limit
  #define WIFI_TX_POWER WIFI_POWER_8_5dBm  // Reduced TX - phone is nearby
  #define FRAME_DELAY_EMOJI_STATIC 100     // 10 FPS - static image
  #define FRAME_DELAY_EMOJI_FADING 50      // 20 FPS - smooth crossfade
  #define FRAME_DELAY_AMBIENT_MIN 40       // 25 FPS cap for ambient effects
  #define INTRO_DURATION_MS 1000
  #define INTRO_FADE_RATE 40
  #define INTRO_SPARKLE_BRIGHTNESS 180
  // No LCD — GfxDevice is a stub (declared but never used)
  typedef void GfxDevice;
#elif defined(TARGET_LCD)
  #define DISPLAY_LCD_ONLY
  #define HIRES_ENABLED  // Hi-res ambient effects (PSRAM provides heap headroom)
  // Full power profile for USB-powered LCD board
  #define WIFI_TX_POWER WIFI_POWER_19_5dBm  // Full TX — USB powered, needs range
  #define DEFAULT_BRIGHTNESS 15
  #define INTRO_DURATION_MS 2000
  #define INTRO_FADE_RATE 20
  #define INTRO_SPARKLE_BRIGHTNESS 255
  // LovyanGFX — DisplayProxy wraps LGFX + LGFX_Sprite for unified API.
  // Same pattern as TARGET_CORES3 — both targets use beginCanvas()/flushCanvas().
  struct DisplayProxy;
  typedef DisplayProxy GfxDevice;
#elif defined(TARGET_CORES3)
  #define DISPLAY_LCD_ONLY
  #define HIRES_ENABLED  // Hi-res ambient for bot background overlay
  #define TOUCH_ENABLED
  // Full power profile for USB-powered Core S3
  #define WIFI_TX_POWER WIFI_POWER_19_5dBm  // Full TX — USB powered, needs range
  #define DEFAULT_BRIGHTNESS 15
  #define INTRO_DURATION_MS 2000
  #define INTRO_FADE_RATE 20
  #define INTRO_SPARKLE_BRIGHTNESS 255
  // M5Unified uses LovyanGFX internally — DisplayProxy wraps it for unified API.
  // Forward-declared here so headers can use 'extern GfxDevice *gfx;'
  struct DisplayProxy;
  typedef DisplayProxy GfxDevice;
#endif

// Manual override: uncomment to enable both displays (if hardware supports)
// #define DISPLAY_DUAL

// ============================================================================
// Hardware Configuration - Board Specific
// ============================================================================

#if defined(BOARD_ESP32S3_MATRIX)
  // Waveshare ESP32-S3-Matrix board pins
  #define DATA_PIN 14
  #define I2C_SDA 11
  #define I2C_SCL 12

#elif defined(BOARD_ESP32S3_LCD_169)
  // Waveshare ESP32-S3-Touch-LCD-1.69 board pins
  #define DATA_PIN 14              // External LED matrix data pin (if used)
  #define I2C_SDA 11               // IMU/Touch I2C SDA
  #define I2C_SCL 10               // IMU/Touch I2C SCL

  // LCD pins (ST7789V2) - corrected from TFT_eSPI working config
  #define LCD_SCK 6
  #define LCD_MOSI 7
  #define LCD_CS 5
  #define LCD_DC 4
  #define LCD_RST 8
  #define LCD_BL 15

  // LCD dimensions
  #define LCD_WIDTH 240
  #define LCD_HEIGHT 280
  #define LCD_OFFSET_Y 20          // ST7789V2 row offset for 280px panel height

  // Speech bubble position (below face on taller 280px display)
  #define OVERLAY_BUBBLE_Y 210

  // Touch controller (CST816T) - shares I2C bus with IMU
  #define TOUCH_I2C_ADDR 0x15
  #define TOUCH_ENABLED

#elif defined(BOARD_ESP32S3_LCD_13)
  // Waveshare ESP32-S3-LCD-1.3 board pins (no touch, battery powered)
  #define DATA_PIN 14              // External LED matrix data pin (if used)
  #define I2C_SDA 47               // IMU I2C SDA
  #define I2C_SCL 48               // IMU I2C SCL

  // LCD pins (ST7789VW)
  #define LCD_SCK 40
  #define LCD_MOSI 41
  #define LCD_CS 39
  #define LCD_DC 38
  #define LCD_RST 42
  #define LCD_BL 20

  // LCD dimensions (square 1.3" display)
  #define LCD_WIDTH 240
  #define LCD_HEIGHT 240
  #define LCD_OFFSET_Y 0           // No row offset for 240x240 panel

  // No touch controller on this board

#elif defined(BOARD_M5CORES3)
  // M5Stack Core S3 - LCD, touch, and IMU managed by M5Unified
  #define DATA_PIN   38            // Onboard single RGB LED (compute buffer placeholder)
  #define I2C_SDA    12            // Internal I2C bus SDA
  #define I2C_SCL    11            // Internal I2C bus SCL
  // Note: LCD reset/backlight are via AW9523 I2C expander (M5Unified handles this)
  // Note: Touch IRQ is GPIO21 (handled by M5Unified internally)
  #define TOUCH_I2C_ADDR 0x38     // FT6336 capacitive touch (informational; M5Unified drives it)

  // LCD dimensions (landscape 320x240)
  #define LCD_WIDTH 320
  #define LCD_HEIGHT 240

  // Face center for landscape layout — horizontally centered, slightly above
  // vertical center to leave room for mouth and speech bubbles below
  #define BOT_FACE_CX 160
  #define BOT_FACE_CY 105

#else
  #error "Unknown board — add pin definitions for your board above"
#endif

// Default LCD_OFFSET_Y for boards that don't define it (e.g. Core S3 uses M5Unified)
#ifndef LCD_OFFSET_Y
  #define LCD_OFFSET_Y 0
#endif

// ============================================================================
// Firmware Identity (used for OTA validation + cloud reporting)
// ============================================================================
#define FIRMWARE_VERSION "2.2.0"

#if defined(BOARD_ESP32S3_MATRIX)
  #define BOARD_TYPE "esp32s3-matrix"
#elif defined(BOARD_ESP32S3_LCD_169)
  #define BOARD_TYPE "esp32s3-lcd169"
#elif defined(BOARD_ESP32S3_LCD_13)
  #define BOARD_TYPE "esp32s3-lcd13"
#elif defined(BOARD_M5CORES3)
  #define BOARD_TYPE "m5cores3"
#endif

// GitHub repo for OTA update checks
#define OTA_GITHUB_REPO "kpow/vizpow"

// ============================================================================
// Common Configuration
// ============================================================================
#define NUM_LEDS 64
#define MATRIX_WIDTH 8
#define MATRIX_HEIGHT 8

// WiFi AP configuration
// Note: WIFI_SSID_BASE and MDNS_HOSTNAME_BASE are base strings.
// initDeviceID() appends a 4-hex MAC suffix at runtime (e.g. "vizBot-A3F2")
// so each device on the same network has a unique SSID and mDNS hostname.
#define WIFI_SSID_BASE     "vizBot"
#define WIFI_PASSWORD      "12345678"
#define MDNS_HOSTNAME_BASE "vizbot"   // Full runtime name: vizbot-xxxx.local

// Display mode
#define MODE_BOT 0

// Ambient effect count (used for bot background overlay)
#define NUM_AMBIENT_EFFECTS 11
#define NUM_PALETTES 15
#define MAX_SAY_LEN 96  // Max characters for speech text (LCD + WLED)

// Shake detection threshold (for bot reactions)
#define SHAKE_THRESHOLD 2.0      // Acceleration magnitude to count as a shake (g)

// WiFi provisioning
#define WIFI_STA_CONNECT_TIMEOUT_MS 10000  // Max wait for STA connection
#define WIFI_AP_LINGER_MS 30000            // Keep AP alive after STA connects (user needs time to switch)
#define WIFI_NVS_NAMESPACE "vizwifi"       // NVS namespace for WiFi credentials

// Info Mode — sustained shake to toggle weather/info view
#define SUSTAINED_SHAKE_DURATION_MS 500    // How long to shake to trigger info mode
#define SHAKE_SUSTAIN_THRESHOLD     1.2f   // Accel magnitude for sustained shake detection
#define WEATHER_LAT_DEFAULT         "37.54"
#define WEATHER_LON_DEFAULT         "-77.43"

// ============================================================================
// MIDI Synthesizer (SAM2695 via Grove Port C)
// ============================================================================
#ifdef TARGET_CORES3
  #define MIDI_SYNTH_ENABLED
  #define MIDI_TX_PIN  17   // Grove Port C — data to SAM2695 RXD
  #define MIDI_RX_PIN  18   // Grove Port C — data from SAM2695 TXD (unused)
  #define MIDI_BAUD    31250
#endif

// ============================================================================
// Cloud Integration (vizCloud)
// ============================================================================
// Cloud integration — pinned CA cert (no esp_crt_bundle dependency)
#if defined(TARGET_CORES3) || defined(TARGET_LCD)
  #define CLOUD_ENABLED
#endif
#ifdef CLOUD_ENABLED
  #define CLOUD_SERVER_URL       "https://vizcloud-raxo5.ondigitalocean.app"
  #define CLOUD_BOT_SECRET       "349baac1c179460b0ea78ca572bcc7a1187bcab891b71b47c28dce5dae5c5103"
  #define CLOUD_POLL_DEFAULT     60
  #define CLOUD_CONNECT_TIMEOUT  10000
  #define CLOUD_RESPONSE_TIMEOUT 15000
  #define CLOUD_BOOT_TIMEOUT     5000
  #define CLOUD_NVS_NAMESPACE    "vizcloud"
#endif

// Debug serial output (comment out to save ~700 bytes of flash)
#define DEBUG_SERIAL
#ifdef DEBUG_SERIAL
  #define DBG(...) Serial.print(__VA_ARGS__)
  #define DBGLN(...) Serial.println(__VA_ARGS__)
#else
  #define DBG(...)
  #define DBGLN(...)
#endif

// XY mapping - trying NO serpentine (straight rows)
inline uint16_t XY(uint8_t x, uint8_t y) {
  return y * MATRIX_WIDTH + x;
}

#endif
