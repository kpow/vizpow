#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// vizMini — pared-down vizbot for ESP32-C3 Super Mini + 1.3" mono OLED
// ============================================================================
// Target: ESP32-C3 Super Mini
//   - 1.3" monochrome I2C OLED (SH1106 or SSD1306, 128x64) on GPIO8/9
//   - TTP223-style capacitive touch (digital out) on GPIO7
//   - WiFi STA (joins your network) with AP fallback + web control page
//
// This firmware deliberately reuses the *real* vizbot face engine
// (tween.h, bot_faces.h, bot_eyes.h) unchanged. A small GfxDevice adapter
// (gfx_mono.h) maps the renderer's drawing primitives onto a 1-bit U8G2
// buffer with a scale+translate transform, so the bot keeps its personality
// on a tiny one-color screen.
// ============================================================================

#define FIRMWARE_VERSION "0.5.0"

// ---- Hardware pins (ESP32-C3 Super Mini) -----------------------------------
// NOTE: GPIO8/GPIO9 are C3 strapping pins. The onboard WS2812 NeoPixel is
// hardwired to GPIO8, so we keep the OLED off GPIO8 and drive the NeoPixel
// there. I2C SDA moved to GPIO10 (adjacent to SCL on GPIO9) — re-plug that one
// DuPont wire from the GPIO8 header to GPIO10. GPIO9 is BOOT; idling high is fine.
#define TOUCH_PIN   7      // TTP223 digital output (HIGH while touched)
#define OLED_SDA    10     // I2C data  (moved off GPIO8 to free the NeoPixel)
#define OLED_SCL    9      // I2C clock

// TTP223 modules are typically active-HIGH. Set to 0 if yours is active-LOW.
#define TOUCH_ACTIVE_HIGH 1

// Set to 1 to log raw GPIO7 state + gesture events over serial (debugging).
#define TOUCH_DEBUG 0

// Set to 1 to log WiFi RSSI to serial every 3s (antenna testing). The rssi
// value stays available in /state regardless.
#define WIFI_RSSI_LOG 0

// ---- OLED driver -----------------------------------------------------------
// Pick the controller in your panel. 1.3" 4-pin modules are most often SH1106;
// 0.96" are usually SSD1306. If the image is shifted ~2px or garbled, flip this.
#define OLED_DRIVER_SH1106   1   // set to 0 to use SSD1306 instead

// Use bit-banged (software) I2C so the SDA/SCL pins are explicit and never
// depend on the board variant's default Wire pins. Set to 0 to use hardware
// I2C (faster) once you've confirmed Wire maps to GPIO8/9 on your board.
#define OLED_USE_SW_I2C      1

#define OLED_WIDTH   128
#define OLED_HEIGHT  64

// ---- Face transform (design space -> OLED) ---------------------------------
// The reused renderer draws in its native 240x280 "design space" centered on
// (BOT_FACE_CX, BOT_FACE_CY) = (120, 118) (defaults from bot_faces.h). The
// GfxDevice adapter maps every coordinate/radius through:
//     screen = ORIGIN + (design - DESIGN_CENTER) * FACE_SCALE
// Tune these three on real hardware to frame the face nicely.
#define FACE_DESIGN_CX   120.0f
#define FACE_DESIGN_CY   118.0f
#define FACE_SCALE       0.45f          // ~240px design -> ~108px drawn
#define FACE_ORIGIN_X    64             // screen x that design-cx maps to
#define FACE_ORIGIN_Y    30             // screen y that design-cy maps to

// ---- Behavior --------------------------------------------------------------
#define FRAME_INTERVAL_MS      40       // ~25 FPS render cap
#define IDLE_SLEEP_MS          120000   // auto-sleep after 2 min of no input
#define SAY_DEFAULT_MS         4000     // how long "say" text stays up
#define STARTLE_MS             900      // surprised reaction when touched

#define DEFAULT_CONTRAST       180      // OLED contrast / "brightness" (0-255)
#define SNOW_DEFAULT_ON        1        // little falling-snow ambiance by default

// ---- Onboard NeoPixel "spark" (WS2812 on GPIO8) ----------------------------
// Long rainbow bursts that glow through the yeti's skin, separated by short
// dark gaps — on more than it's off (~70% duty), but still clearly cycling.
#define NEOPIXEL_PIN       8           // onboard WS2812 data
#define SPARK_FIRST_MS     4000        // first burst ~4s after boot
#define SPARK_MIN_MS       8000        // dark gap between bursts: 8s ...
#define SPARK_MAX_MS       18000       // ... to 18s (shorter than the burst)
#define SPARK_DURATION_MS  30000       // each rainbow burst lasts ~30s (the "on")
#define SPARK_MAX_BRIGHT   1.0f        // full brightness (bleeds through the skin)
#define SPARK_CYCLE_MS     12000       // time for one full rainbow rotation
                                       // (higher = gentler, smoother color drift)

// ---- Touch gestures --------------------------------------------------------
#define DOUBLE_TAP_MS  450      // two touches within this window = double-tap

// ---- Time + Weather (info) mode --------------------------------------------
// Clock via NTP; weather via Open-Meteo (free, no API key, plain HTTP) — the
// same source the full vizbot uses. Set your location below.
#define TZ_POSIX        "EST5EDT,M3.2.0,M11.1.0"   // US Eastern (matches vizbot)
#define NTP_SERVER_1    "pool.ntp.org"
#define NTP_SERVER_2    "time.nist.gov"

#define WEATHER_LAT     "37.54"     // <-- your latitude
#define WEATHER_LON     "-77.43"    // <-- your longitude
#define WEATHER_UNIT_F  1           // 1 = Fahrenheit, 0 = Celsius
#define WEATHER_REFRESH_MS  600000  // re-fetch at most every 10 min
#define CLOCK_24H       0           // 1 = 24-hour clock, 0 = 12-hour
#define INFO_AUTO_SWITCH_MS 30000   // auto-flip time<->weather in info mode (0 = off)

// ---- WiFi / web ------------------------------------------------------------
#define WIFI_AP_BASE     "vizMini"      // AP SSID becomes vizMini-XXXX
#define MDNS_HOSTNAME    "vizmini"      // reachable at http://vizmini.local
#define STA_CONNECT_TIMEOUT_MS  10000   // give up joining a network after 10s
#define NVS_NAMESPACE    "vizmini"      // Preferences namespace for creds

#endif // CONFIG_H
