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

#define FIRMWARE_VERSION "0.7.11.4"

// ---- Hardware pins (ESP32-C3 Super Mini) -----------------------------------
// NOTE: GPIO8/GPIO9 are C3 strapping pins. The onboard WS2812 NeoPixel is
// hardwired to GPIO8, so we keep the OLED off GPIO8 and drive the NeoPixel
// there. I2C SDA moved to GPIO10 (adjacent to SCL on GPIO9) — re-plug that one
// DuPont wire from the GPIO8 header to GPIO10. GPIO9 is BOOT; idling high is fine.
#define TOUCH_PIN   3      // pushbutton to GND (was GPIO7)
#define OLED_SDA    10     // I2C data  (moved off GPIO8 to free the NeoPixel)
#define OLED_SCL    9      // I2C clock

// Input on GPIO3 is a momentary pushbutton wired GPIO3 -> button -> GND,
// using the internal pull-up: idle reads HIGH, pressed reads LOW (active-LOW).
// (Was 1 for the active-HIGH TTP223 capacitive pad, which false-triggered.)
#define TOUCH_ACTIVE_HIGH 0

// Set to 1 to log raw button pin state + gesture events over serial (debugging).
#define TOUCH_DEBUG 0

// Set to 1 to log WiFi RSSI to serial every 3s (antenna testing). The rssi
// value stays available in /state regardless.
#define WIFI_RSSI_LOG 0

// 1 = WiFi on (web control + time/weather); 0 = run fully offline.
#define WIFI_ENABLED 1

// ---- OLED driver -----------------------------------------------------------
// Pick the controller in your panel. 1.3" 4-pin modules are most often SH1106;
// 0.96" are usually SSD1306. If the image is shifted ~2px or garbled, flip this.
#define OLED_DRIVER_SH1106   1   // set to 0 to use SSD1306 instead

// Hardware I2C on OLED_SDA / OLED_SCL (U8g2 passes the pins to Wire.begin).
// Bit-banged software I2C took ~520 ms per frame (2 fps); hardware I2C at
// 400 kHz sends the 1 KB frame in ~25 ms. 1 = back to software I2C.
#define OLED_USE_SW_I2C      0
#define OLED_I2C_HZ          400000

#define OLED_WIDTH   128
#define OLED_HEIGHT  64
// The vizbot eye model clips to the screen with these names
#define LCD_WIDTH    OLED_WIDTH
#define LCD_HEIGHT   OLED_HEIGHT

// ---- Face placement (OLED pixels) ------------------------------------------
// The eye model is designed for a 240 px wide face (eye R 48). renderMiniFace()
// draws it centred on (FACE_ORIGIN_X, FACE_ORIGIN_Y) at FACE_SCALE, the way
// vizbot's renderBotFaceAt() shrinks the face; the mouth gets a scaled copy.
// Tallest faces span about -66 (brows) .. +84 (mouth + curve) design px, so
// 0.41 fits all 25 into 64 rows with a pixel to spare (0.45 clipped mouths).
#define FACE_SCALE       0.41f          // eye R 48 -> ~20 px, face ~100 px wide
#define FACE_ORIGIN_X    64             // screen x of the face centre
#define FACE_ORIGIN_Y    28             // screen y of the face centre (between the eyes)

// ---- Face motion (face_motion.h) -------------------------------------------
// Whole-face rotation: tilts, wobbles, pendulum swings, drifts, spins, flips.
// Rare by default; level (off / rare / often) is saved in NVS from the web page.
#define MOTION_RARE_MIN_S    120       // rare: a move every 2 ...
#define MOTION_RARE_MAX_S    360       // ... to 6 minutes
#define MOTION_OFTEN_MIN_S   10        // often: every 10 ...
#define MOTION_OFTEN_MAX_S   30        // ... to 30 s
#define MOTION_FIT_A         54.0f     // face half-width kept on screen while turned (px)
#define MOTION_FIT_B         32.0f     // face half-height
#define MOTION_DIZZY_MS      2500      // dizzy face after a double spin

// ---- Ferro mode (ferro_blob.h) ---------------------------------------------
// Black ferrofluid drop on a white screen, floating free. Sizes from the
// motion spec §1-2 (spikes ~1.45x party's share of r so medium reads).
#define FERRO_R_M            15.0f     // body radius, px (S 12, L 18)
#define FERRO_SPIKE_M        10.0f     // tall spike, medium pattern (px)
#define FERRO_SPIKE_B        13.5f     // tall spike, big pattern
#define FERRO_CONTRAST_K     0.55f     // panel brightness in ferro vs the user's value
#define FERRO_CONTRAST_MIN   40
#define FERRO_CONTRAST_MAX   140
#define FERRO_VISIT_MS       60000     // auto: how long a visit lasts
#define FERRO_VISIT_GAP_MIN_S 480      // auto: face time between visits, 8 ...
#define FERRO_VISIT_GAP_MAX_S 900      // ... to 15 min

// ---- Behavior --------------------------------------------------------------
#define FRAME_INTERVAL_MS      40       // ~25 FPS render cap
#define SAY_DEFAULT_MS         4000     // how long "say" text stays up
#define STARTLE_MS             900      // surprised reaction when touched

#define DEFAULT_CONTRAST       180      // OLED contrast / "brightness" (0-255)
#define SNOW_DEFAULT_ON        1        // little falling-snow ambiance by default

// ---- Inside lights (strip_leds.h) ------------------------------------------
// 12x WS2812B glowing through the translucent body, in three runs of 4 wired
// in this order: right side, left side, bottom by the pants (right and left
// as you face him). The onboard WS2812 (hardwired to GPIO8) is left dark: the
// strip needs both RMT channels.
#define LED_STRIP_PIN        4         // strip DIN (free, not a strapping pin)
#define LED_RUN              4         // LEDs per run
#define LED_STRIP_COUNT      (3 * LED_RUN)
// Which end of each run is wired first (check with /debug/leds?id=runs)
#define LED_RIGHT_TOP_FIRST  0         // 1 = the right run starts at the top
#define LED_LEFT_TOP_FIRST   1         // 1 = the left run starts at the top
#define LED_BOTTOM_LEFT_FIRST 1        // 1 = the bottom run starts at your left
#define LED_MAX_MA           450       // FastLED power cap so USB never browns out
#define LED_DEFAULT_BRI      160       // 0-255, saved per bot in NVS once changed
#define LED_FRAME_MS         20        // 50 Hz light updates
#define LED_MOOD_CYCLE_MS    10000     // mood: swap to a companion motion every 10 s
#define LED_SPARK_MS         3000      // rainbow burst laid over any mode

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
#define INK_WINDY_MPH       15      // Ink Time shows "wind" at or above this (no WMO code for it)
#define INK_ENTER_MS        1400    // face -> ink: eyes shut, the black contracts into the drop, it splits out
#define INK_EXIT_MS         1400    // ink -> face: gather into the drop, swell over the screen, eyes open
#define INK_FLIP_MS         1200    // clock <-> weather through the drop
#define CLOCK_24H       0           // 1 = 24-hour clock, 0 = 12-hour
#define INFO_AUTO_SWITCH_MS 30000   // auto-flip time<->weather in info mode (0 = off)

// ---- WiFi / web ------------------------------------------------------------
#define WIFI_AP_BASE     "vizMini"      // AP SSID becomes vizMini-XXXX
#define MDNS_HOSTNAME    "vizmini"      // reachable at http://vizmini.local
#define BOOT_AP_NOTE_MS  30000          // after the intro, the hotspot name stays in a bubble this long
#define INTRO_IP_HOLD_MS 2600           // intro: how long his address shows before he swells into the face
#define INTRO_AP_HOLD_MS 8000           // ... or the hotspot name (people have to read it)
#define STA_CONNECT_TIMEOUT_MS  20000   // give up joining a network after 20 s (a join takes ~5-8 s)
#define NVS_NAMESPACE    "vizmini"      // Preferences namespace for creds

#endif // CONFIG_H
