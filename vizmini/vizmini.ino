// ============================================================================
// vizMini — a pocket vizbot for the ESP32-C3 Super Mini + 1.3" mono OLED
// ============================================================================
// Single-core cooperative loop: the web server and the face renderer share one
// core, interleaved every iteration so web control stays responsive while the
// face animates at ~25 FPS.
//
// The face engine (tween.h / bot_faces.h / bot_eyes.h) is the *unmodified*
// vizbot renderer. gfx_mono.h adapts its six drawing primitives onto a 1-bit
// U8g2 buffer. See README.md.
// ============================================================================

#include "config.h"
#include "gfx_mono.h"          // defines GfxDevice + the u8g2 display object

// --- globals the reused renderer (bot_eyes.h) expects to find ---------------
GfxDevice  gfxDev;
GfxDevice *gfx = &gfxDev;       // bot_eyes.h draws through this
float      accelX = 0, accelY = 0, accelZ = 1.0f;  // no IMU on this board (unused)
uint8_t    botBackgroundStyle = 0;                 // 0 = no ambient stroke

#include "bot_eyes.h"          // renderBotFace(), blink + look-around helpers
#include "touch_input.h"
#include "weather.h"
#include "web_ui.h"
#include <time.h>

// Top-level interaction mode
enum AppMode : uint8_t { MODE_FACE = 0, MODE_INFO };
enum InfoView : uint8_t { VIEW_TIME = 0, VIEW_WEATHER };
AppMode  g_mode = MODE_FACE;
InfoView g_infoView = VIEW_TIME;
unsigned long g_lastPressMs = 0;       // for double-tap detection
unsigned long g_viewSwitchMs = 0;      // when the info view last changed (auto-flip timer)

// ============================================================================
// Runtime state
// ============================================================================
BotFaceState  face;
BotBlinkState blink;
BotLookAround look;
TouchInput    touch;

uint8_t  g_expr      = EXPR_NEUTRAL;
uint8_t  g_prevExpr  = EXPR_NEUTRAL;   // expression to restore after a startle
bool     g_snow      = SNOW_DEFAULT_ON;
uint8_t  g_contrast  = DEFAULT_CONTRAST;

char          g_sayText[49] = {0};
unsigned long g_sayUntil    = 0;
unsigned long g_startleUntil = 0;
unsigned long g_lastInputMs = 0;
unsigned long g_lastFrame   = 0;

// ---- snow particles --------------------------------------------------------
struct Flake { int16_t x; int16_t y; uint8_t spd; };
static const uint8_t SNOW_N = 14;
Flake snow[SNOW_N];

void snowInit() {
  for (uint8_t i = 0; i < SNOW_N; i++) {
    snow[i] = { (int16_t)random(0, OLED_WIDTH), (int16_t)random(0, OLED_HEIGHT),
                (uint8_t)random(1, 3) };
  }
}
void snowDrawAndStep() {
  for (uint8_t i = 0; i < SNOW_N; i++) {
    u8g2.setDrawColor(1);
    u8g2.drawPixel(snow[i].x, snow[i].y);
    snow[i].y += snow[i].spd;
    if ((millis() / 200 + i) % 3 == 0) snow[i].x += (i & 1) ? 1 : -1;
    if (snow[i].y >= OLED_HEIGHT) {
      snow[i].y = 0;
      snow[i].x = random(0, OLED_WIDTH);
    }
    if (snow[i].x < 0) snow[i].x = OLED_WIDTH - 1;
    if (snow[i].x >= OLED_WIDTH) snow[i].x = 0;
  }
}

// ============================================================================
// Onboard NeoPixel "spark" — occasional rainbow glow on GPIO8
// ============================================================================
bool          g_sparkActive = false;
unsigned long g_sparkStart  = 0;
unsigned long g_nextSparkAt = 0;

// h:0-360, s/v:0-1 -> r/g/b:0-1
void hsv2rgb(float h, float s, float v, float &r, float &g, float &b) {
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float rr, gg, bb;
  if      (h <  60) { rr = c; gg = x; bb = 0; }
  else if (h < 120) { rr = x; gg = c; bb = 0; }
  else if (h < 180) { rr = 0; gg = c; bb = x; }
  else if (h < 240) { rr = 0; gg = x; bb = c; }
  else if (h < 300) { rr = x; gg = 0; bb = c; }
  else              { rr = c; gg = 0; bb = x; }
  r = rr + m; g = gg + m; b = bb + m;
}

void sparkSchedule() { g_nextSparkAt = millis() + (unsigned long)random(SPARK_MIN_MS, SPARK_MAX_MS); }
void sparkFire()     { g_sparkActive = true; g_sparkStart = millis(); Serial.println("[spark] fire"); }

// Call every loop pass. Dark when idle; rainbow sweep with fade in/out when firing.
void sparkUpdate() {
  if (!g_sparkActive) {
    if (millis() >= g_nextSparkAt) sparkFire();
    return;
  }
  unsigned long el = millis() - g_sparkStart;
  if (el >= SPARK_DURATION_MS) {
    g_sparkActive = false;
    rgbLedWrite(NEOPIXEL_PIN, 0, 0, 0);   // back to dark
    sparkSchedule();
    return;
  }
  // continuous rainbow drift — slow cycle = smooth glide between colors
  float hue = fmodf((float)el / (float)SPARK_CYCLE_MS * 360.0f, 360.0f);
  // fixed short fade in/out so a long burst stays mostly full-bright
  const float fin = 800.0f, fout = 1200.0f;
  float env = 1.0f;
  if (el < fin) env = (float)el / fin;
  else if (el > (float)SPARK_DURATION_MS - fout) env = (float)(SPARK_DURATION_MS - el) / fout;
  float r, g, b;
  hsv2rgb(hue, 1.0f, 1.0f, r, g, b);
  float k = env * SPARK_MAX_BRIGHT * 255.0f;
  rgbLedWrite(NEOPIXEL_PIN, (uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
}

// ============================================================================
// Expression / sleep helpers
// ============================================================================
void applyExpression(uint8_t v) {
  if (v >= BOT_NUM_EXPRESSIONS) v = EXPR_NEUTRAL;
  g_expr = v;
  face.transitionTo(v);
}

void startle() {
  g_prevExpr = g_expr;
  face.transitionTo(EXPR_SURPRISED, 120);
  g_startleUntil = millis() + STARTLE_MS;
}

// ---- Time + Weather (info) mode -------------------------------------------
// View 1: full-screen clock
void drawTimeView() {
  struct tm ti;
  bool haveTime = getLocalTime(&ti, 5);

  u8g2.clearBuffer();
  u8g2.setDrawColor(1);

  char hhmm[8];
  if (haveTime) strftime(hhmm, sizeof(hhmm), CLOCK_24H ? "%H:%M" : "%I:%M", &ti);
  else          strcpy(hhmm, "--:--");
  if (!CLOCK_24H && hhmm[0] == '0') memmove(hhmm, hhmm + 1, strlen(hhmm)); // strip leading 0

  u8g2.setFont(u8g2_font_logisoso32_tn);                 // big clock
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(hhmm)) / 2, 40, hhmm);

  if (haveTime && !CLOCK_24H) {                          // AM/PM tag
    u8g2.setFont(u8g2_font_5x8_tf);
    u8g2.drawStr(OLED_WIDTH - 14, 10, ti.tm_hour < 12 ? "AM" : "PM");
  }

  char date[20];
  if (haveTime) strftime(date, sizeof(date), "%a %b %d", &ti);
  else          strcpy(date, webIsAP() ? "no network" : "syncing...");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(date)) / 2, 60, date);

  u8g2.sendBuffer();
}

// View 2: full-screen weather
void drawWeatherView() {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);

  if (!weather.valid) {
    u8g2.setFont(u8g2_font_6x12_tf);
    const char* msg = (WiFi.status() == WL_CONNECTED) ? "weather n/a" : "no network";
    u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(msg)) / 2, 36, msg);
    u8g2.sendBuffer();
    return;
  }

  // Big temperature on the left
  char temp[8];
  snprintf(temp, sizeof(temp), "%d", weather.tempF);
  u8g2.setFont(u8g2_font_logisoso24_tn);
  int tx = 6;
  u8g2.drawStr(tx, 34, temp);
  int tw = u8g2.getStrWidth(temp);
  u8g2.drawCircle(tx + tw + 5, 14, 2);                   // degree mark
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(tx + tw + 9, 22, WEATHER_UNIT_F ? "F" : "C");

  // Icon top-right
  drawWeatherIcon(106, 16, weather.code);

  // Condition centered
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(weather.condition)) / 2, 50, weather.condition);

  // Hi / Lo
  u8g2.setFont(u8g2_font_6x10_tf);
  char hilo[20];
  snprintf(hilo, sizeof(hilo), "H %d   L %d", weather.hi, weather.lo);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(hilo)) / 2, 62, hilo);

  u8g2.sendBuffer();
}

void renderInfo() {
  if (g_infoView == VIEW_WEATHER) drawWeatherView();
  else                            drawTimeView();
}

// Flip Time <-> Weather and restart the auto-switch timer.
void toggleInfoView() {
  g_infoView = (g_infoView == VIEW_TIME) ? VIEW_WEATHER : VIEW_TIME;
  g_viewSwitchMs = millis();
  if (g_infoView == VIEW_WEATHER) refreshWeatherIfStale();
}

void enterInfo() {
  g_mode = MODE_INFO;
  g_infoView = VIEW_TIME;                // open on the clock
  g_viewSwitchMs = millis();
  g_lastInputMs = millis();
  // quick "loading" frame, then fetch if the cached data is stale
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth("loading...")) / 2, 36, "loading...");
  u8g2.sendBuffer();
  refreshWeatherIfStale();
}

void exitInfo() {
  g_mode = MODE_FACE;
  prevFrame.invalidate();
  applyExpression(g_expr);
}

// ============================================================================
// Web control hooks (declared extern in web_ui.h)
// ============================================================================
void ctlSetExpression(uint8_t v) { g_lastInputMs = millis(); if (g_mode == MODE_INFO) exitInfo(); applyExpression(v); }
void ctlSetInfo(bool on) {
  g_lastInputMs = millis();
  if (on) {
    if (g_mode == MODE_INFO) toggleInfoView();
    else enterInfo();
  } else if (g_mode == MODE_INFO) exitInfo();
}
void ctlSetSnow(bool on)         { g_lastInputMs = millis(); g_snow = on; }
void ctlSpark()                  { g_lastInputMs = millis(); sparkFire(); }
void ctlSetContrast(uint8_t c)   { g_lastInputMs = millis(); g_contrast = c; gfxDev.setContrast(c); }
void ctlSay(const char* text) {
  g_lastInputMs = millis();
  strncpy(g_sayText, text, sizeof(g_sayText) - 1);
  g_sayText[sizeof(g_sayText) - 1] = 0;
  g_sayUntil = millis() + SAY_DEFAULT_MS;
}
String ctlStateJson() {
  String s = "{";
  s += "\"fw\":\"" FIRMWARE_VERSION "\",";
  s += "\"ip\":\"" + webIP() + "\",";
  s += "\"rssi\":" + String((WiFi.status() == WL_CONNECTED) ? (int)WiFi.RSSI() : 0) + ",";
  s += "\"ap\":" + String(webIsAP() ? "true" : "false") + ",";
  s += "\"expr\":" + String(g_expr) + ",";
  s += "\"mode\":\"" + String(g_mode == MODE_INFO ? "info" : "face") + "\",";
  s += "\"snow\":" + String(g_snow ? "true" : "false") + ",";
  s += "\"contrast\":" + String(g_contrast);
  s += "}";
  return s;
}

// ============================================================================
// Overlays drawn directly on the buffer (unscaled, screen-space)
// ============================================================================
void drawSayText() {
  u8g2.setFont(u8g2_font_6x12_tf);
  int16_t w = u8g2.getStrWidth(g_sayText);
  int16_t boxW = min((int16_t)(w + 8), (int16_t)OLED_WIDTH);
  int16_t x = (OLED_WIDTH - boxW) / 2;
  int16_t y = OLED_HEIGHT - 14;
  u8g2.setDrawColor(1); u8g2.drawRBox(x, y, boxW, 14, 2);   // filled bubble
  u8g2.setDrawColor(0); u8g2.drawStr(x + 4, y + 11, g_sayText); // knockout text
  u8g2.setDrawColor(1);
}

// ============================================================================
// Boot splash — show where to reach the bot
// ============================================================================
void splash(const String& where) {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(28, 22, "vizMini");
  u8g2.setFont(u8g2_font_5x8_tf);
  if (webIsAP()) {
    u8g2.drawStr(4, 40, "join wifi:");
    u8g2.drawStr(4, 52, where.c_str());
  } else {
    u8g2.drawStr(4, 40, "http://");
    u8g2.drawStr(4, 52, where.c_str());
  }
  u8g2.sendBuffer();
}

// ============================================================================
// Setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  gfxDev.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(20, 36, "vizMini...");
  u8g2.sendBuffer();

  touch.begin();
  face.init();
  blink.init();
  look.init();
  snowInit();

  rgbLedWrite(NEOPIXEL_PIN, 0, 0, 0);   // NeoPixel dark at boot
  g_nextSparkAt = millis() + SPARK_FIRST_MS;

  String where;
#if WIFI_ENABLED
  where = webBegin();                // join wifi or start AP + captive portal
  if (!webIsAP()) {                  // start NTP clock sync once on a network
    configTzTime(TZ_POSIX, NTP_SERVER_1, NTP_SERVER_2);
  }
#else
  WiFi.mode(WIFI_OFF);               // TEMP: radio fully off for the noise test
  where = "wifi-off-test";
#endif
  splash(where);
  Serial.printf("[vizMini] %s  ->  %s\n", webIsAP() ? "AP" : "STA", where.c_str());
  delay(2000);

  g_lastInputMs = millis();
  prevFrame.invalidate();
}

void loop() {
  if (WIFI_ENABLED) webLoop();        // keep web control responsive every pass
  if (SPARK_ENABLED) sparkUpdate();   // occasional NeoPixel glow (dark when idle)

  // ---- WiFi signal readout (gated; rssi also lives in /state) ----
#if WIFI_RSSI_LOG
  {
    static unsigned long lastRssi = 0;
    if (!webIsAP() && WiFi.status() == WL_CONNECTED && millis() - lastRssi > 3000) {
      lastRssi = millis();
      Serial.printf("[wifi] RSSI %d dBm\n", (int)WiFi.RSSI());
    }
  }
#endif

  // ---- touch debug: log raw pin transitions + a heartbeat ----
#if defined(TOUCH_DEBUG) && TOUCH_DEBUG
  {
    static int lastRaw = -1; static unsigned long lastBeat = 0;
    int raw = digitalRead(TOUCH_PIN);
    if (raw != lastRaw) { lastRaw = raw; Serial.printf("[touch] GPIO%d raw=%d\n", TOUCH_PIN, raw); }
    if (millis() - lastBeat > 3000) { lastBeat = millis(); Serial.printf("[touch] heartbeat raw=%d\n", raw); }
  }
#endif

  // ---- touch gestures ----
  TouchEvent ev = touch.update();
  if (ev != TOUCH_NONE) {
#if defined(TOUCH_DEBUG) && TOUCH_DEBUG
    Serial.printf("[touch] event=%d\n", (int)ev);
#endif
    unsigned long now = millis();
    g_lastInputMs = now;
    bool isDouble = (ev == TOUCH_PRESS) && (now - g_lastPressMs < DOUBLE_TAP_MS);
    if (ev == TOUCH_PRESS) g_lastPressMs = now;

    if (g_mode == MODE_INFO) {
      if (ev == TOUCH_PRESS) toggleInfoView();        // single touch flips view
      else if (ev == TOUCH_LONG_PRESS) exitInfo();    // hold to leave
    } else if (ev == TOUCH_LONG_PRESS) {
      enterInfo();                                    // hold -> time/weather
    } else if (ev == TOUCH_PRESS) {
      if (isDouble) enterInfo();                      // double-tap -> time/weather
      else applyExpression((g_expr + 1) % BOT_NUM_EXPRESSIONS); // single -> cycle mood
    }
  }

  // ---- info (time + weather) mode: render and skip the face ----
  if (g_mode == MODE_INFO) {
    if (millis() - g_lastFrame < 250) return;   // a clock only needs ~4 FPS
    g_lastFrame = millis();
    if (INFO_AUTO_SWITCH_MS && millis() - g_viewSwitchMs >= INFO_AUTO_SWITCH_MS)
      toggleInfoView();                                       // auto-flip every 30s
    if (g_infoView == VIEW_WEATHER) refreshWeatherIfStale();  // refresh while shown
    renderInfo();
    return;
  }

  // ---- end a startle reaction ----
  if (g_startleUntil && millis() > g_startleUntil) {
    g_startleUntil = 0;
    applyExpression(g_prevExpr);
  }

  // ---- frame-rate-capped render ----
  if (millis() - g_lastFrame < FRAME_INTERVAL_MS) return;
  g_lastFrame = millis();

  tweenManager.update();
  face.update();
  face.blinkAmount = blink.update();
  look.update(face.dynamicPupilX, face.dynamicPupilY);

  u8g2.clearBuffer();
  if (g_snow) snowDrawAndStep();
  renderBotFace(face, /*bgColor=*/0x0000);
  if (g_sayText[0] && millis() < g_sayUntil) drawSayText();
  else g_sayText[0] = 0;
  u8g2.sendBuffer();
}
