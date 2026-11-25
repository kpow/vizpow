#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include "config.h"
#include "bot_faces.h"   // for EXPR_* names / BOT_NUM_EXPRESSIONS
#include "device_name.h" // hotspot SSID, mDNS name
#include "weather.h"     // the network lock (an upload waits out a cloud call)
#include "cloud_client.h"
#include "sleep_schedule.h" // /bot/sleep argument checks
#include "esp_app_format.h"

// ---- Control hooks implemented in vizmini.ino ------------------------------
extern void   ctlSetExpression(uint8_t v);
extern void   ctlSay(const char* text);
extern void   ctlSetInfo(bool on);
extern void   ctlInfoSky(const String &v);
extern void   ctlSetContrast(uint8_t c);
extern void   ctlSetSnow(bool on);
extern void   ctlSpark();
extern void   ctlSetLeds(int mode, int bri);   // -1 = leave unchanged
extern void   ctlMotion(int level, int move);  // -1 / 0 = leave unchanged
extern void   ctlSetFerro(bool on);
extern void   ctlFerroPlay(const String &pat, const String &tent);
extern void   ctlFerroSetting(const String &v);
extern void   ctlFerroVisit();
extern void   ctlPartyStack(const String &v);
extern String ctlStateJson();
extern void   ctlSleep(int on, int atMin, int wakeMin, int nap, int act);   // -1 = leave unchanged
extern String sleepJson();

// ============================================================================
// Network state
// ============================================================================
static WebServer  server(80);
static DNSServer  dnsServer;
static Preferences wifiPrefs;
static bool       g_apMode = false;
static IPAddress  g_apIP(192, 168, 4, 1);
static String     g_ssid, g_pass;             // saved creds (loaded once)
static unsigned long g_lastStaTry = 0;        // last background reconnect attempt
static const unsigned long STA_RETRY_MS = 20000;


// ============================================================================
// HTML — control page (STA mode), setup page (AP mode), firmware update page
// ============================================================================
#include "web_page.h"   // CONTROL_HTML: the control page (vizBot house style)

#include "web_setup.h"  // SETUP_HTML (hotspot) + UPDATE_HTML, same house style

// ============================================================================
// Request handlers
// ============================================================================
inline uint8_t argU8(const char* name, uint8_t def = 0) {
  if (!server.hasArg(name)) return def;
  return (uint8_t)constrain(server.arg(name).toInt(), 0, 255);
}

inline void handleRoot() {
  if (g_apMode) server.send_P(200, "text/html", SETUP_HTML);
  else          server.send_P(200, "text/html", CONTROL_HTML);
}

// ---- Setup: scan, test-join, then save ------------------------------------
// The setup page never saves a network blind: /wifi/try joins it while the
// hotspot stays up, webLoop() watches the result, and only a join that gets
// an address is written to NVS (then the bot restarts onto it, so the boot
// intro shows the new address).
enum JoinState : uint8_t { JOIN_IDLE, JOIN_TRYING, JOIN_OK, JOIN_FAIL };
static JoinState     g_join = JOIN_IDLE;
static String        g_joinSsid, g_joinPass, g_joinWhy, g_joinIp;
static unsigned long g_joinAt = 0, g_rebootAt = 0;
static uint8_t       g_lastReason = 0;   // last STA disconnect reason
static uint8_t       g_authFails  = 0;   // handshake/auth failures this try
static const unsigned long JOIN_REBOOT_MS = 8000;   // time for the page to show "connected"
static unsigned long g_setupSeenAt = 0;              // last scan/try from the setup page
static const unsigned long SETUP_QUIET_MS = 60000;   // no background rejoins while someone is setting up

inline String jsonStr(const String &v) {
  String o = "\"";
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char b[7]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += c;
  }
  return o + "\"";
}

// /wifi/scan[?fresh=1] — {"scanning":true} while the async scan runs, then
// {"nets":[{"s":ssid,"r":rssi,"l":locked}],"saved":ssid}: one entry per name
// (strongest), strongest first, hidden networks dropped, at most 20
inline void handleScan() {
  int16_t n = WiFi.scanComplete();
  bool fresh = server.hasArg("fresh") && n != WIFI_SCAN_RUNNING;
  g_setupSeenAt = millis();
  if (g_join != JOIN_TRYING && (n == WIFI_SCAN_FAILED || fresh)) {
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false);   // a pending join blocks scans
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    n = WIFI_SCAN_RUNNING;
  }
  if (n == WIFI_SCAN_RUNNING) { server.send(200, "application/json", "{\"scanning\":true}"); return; }
  int idx[20], cnt = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i).length() == 0) continue;
    int dup = -1;
    for (int k = 0; k < cnt; k++) if (WiFi.SSID(idx[k]) == WiFi.SSID(i)) dup = k;
    if (dup >= 0) { if (WiFi.RSSI(i) > WiFi.RSSI(idx[dup])) idx[dup] = i; continue; }
    if (cnt < 20) idx[cnt++] = i;
  }
  for (int a = 1; a < cnt; a++)            // strongest first
    for (int b = a; b > 0 && WiFi.RSSI(idx[b]) > WiFi.RSSI(idx[b - 1]); b--) { int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t; }
  String j = "{\"nets\":[";
  for (int k = 0; k < cnt; k++) {
    int i = idx[k];
    if (k) j += ',';
    j += "{\"s\":" + jsonStr(WiFi.SSID(i)) + ",\"r\":" + String(WiFi.RSSI(i)) +
         ",\"l\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1") + "}";
  }
  j += "],\"saved\":" + jsonStr(g_ssid) + "}";
  server.send(200, "application/json", j);
}

// POST /wifi/try ssid, pass — start a test join (result via /wifi/status)
inline void handleTry() {
  String ssid = server.arg("ssid");
  if (ssid.length() == 0 || ssid.length() > 32 || server.arg("pass").length() > 63) {
    server.send(400, "application/json", "{\"ok\":false}"); return;
  }
  g_joinSsid = ssid; g_joinPass = server.arg("pass");
  g_join = JOIN_TRYING; g_joinAt = g_setupSeenAt = millis(); g_joinWhy = ""; g_joinIp = "";
  g_lastReason = 0; g_authFails = 0;
  WiFi.scanDelete();
  WiFi.disconnect(false);                  // drop any background retry of the saved network
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(g_joinSsid.c_str(), g_joinPass.c_str());
  ctlSay(("joining " + ssid).substring(0, 48).c_str());
  server.send(200, "application/json", "{\"ok\":true}");
}

// /wifi/status — {"state":"idle|trying|ok|fail","ssid","ip","why","host","ap","fw"}
inline void handleWifiStatus() {
  static const char* ST[] = { "idle", "trying", "ok", "fail" };
  String j = "{\"state\":\"" + String(ST[g_join]) + "\",\"ssid\":" + jsonStr(g_joinSsid) +
             ",\"ip\":\"" + g_joinIp + "\",\"why\":" + jsonStr(g_joinWhy) +
             ",\"host\":\"" + String(g_devHost) + ".local\",\"ap\":" + jsonStr(g_devName) +
             ",\"fw\":\"" FIRMWARE_VERSION "\"}";
  server.send(200, "application/json", j);
}

// Reason codes are esp_wifi's wifi_err_reason_t
inline bool reasonIsAuth(uint8_t r)     { return r == 2 || r == 15 || r == 202 || r == 204; }
inline bool reasonIsNotFound(uint8_t r) { return r == 201 || (r >= 210 && r <= 212); }

inline void joinFail(const char* why) {
  g_join = JOIN_FAIL; g_joinWhy = why;
  WiFi.disconnect(false);
  ctlSay("wifi didn't take");
  Serial.printf("[setup] join '%s' failed: %s (reason %d)\n", g_joinSsid.c_str(), why, g_lastReason);
}

// Called every loop while a test join runs
inline void joinPoll() {
  unsigned long t = millis() - g_joinAt;
  if (WiFi.status() == WL_CONNECTED) {
    g_join = JOIN_OK; g_joinIp = WiFi.localIP().toString();
    wifiPrefs.begin(NVS_NAMESPACE, false);
    wifiPrefs.putString("ssid", g_joinSsid);
    wifiPrefs.putString("pass", g_joinPass);
    wifiPrefs.end();
    g_rebootAt = millis() + JOIN_REBOOT_MS;
    ctlSay("got it!");
    Serial.printf("[setup] joined '%s' as %s, saved; restarting\n", g_joinSsid.c_str(), g_joinIp.c_str());
  } else if (g_authFails >= 2 || WiFi.status() == WL_CONNECT_FAILED) {
    joinFail("Wrong password");
  } else if (t > STA_CONNECT_TIMEOUT_MS) {
    joinFail(reasonIsAuth(g_lastReason)     ? "Wrong password" :
             reasonIsNotFound(g_lastReason) ? "He can't see that network from here" :
                                              "Timed out joining it");
  }
}

inline void handleExpression() { ctlSetExpression(argU8("v", EXPR_NEUTRAL)); server.send(200, "text/plain", "ok"); }
inline void handleSay()        { ctlSay(server.arg("text").c_str());        server.send(200, "text/plain", "ok"); }
// /bot/info?v=1|0 (time/weather on, or flip / off)  &  ?sky=<condition>|real (test the ink weather)
inline void handleInfo() {
  if (server.hasArg("v")) ctlSetInfo(argU8("v", 1) != 0);
  if (server.hasArg("sky")) ctlInfoSky(server.arg("sky"));
  server.send(200, "text/plain", "ok");
}
inline void handleContrast()   { ctlSetContrast(argU8("v", DEFAULT_CONTRAST)); server.send(200, "text/plain", "ok"); }
inline void handleSnow()       { ctlSetSnow(argU8("v", 1) != 0);            server.send(200, "text/plain", "ok"); }
inline void handleSpark()      { ctlSpark();                                server.send(200, "text/plain", "ok"); }

// "HH:MM" -> minute-of-day, or -1
inline int parseHHMM(const String &v) {
  int h = -1, m = -1;
  char tail = 0;
  if (sscanf(v.c_str(), "%d:%d%c", &h, &m, &tail) != 2) return -1;
  return sleepsched::minuteOfDay(h, m);
}
// GET /bot/sleep                                 -> state JSON
//     ?on=0|1 &at=HH:MM &wake=HH:MM &nap=0..120   (any subset) -> sets them
//     ?do=sleep | ?do=wake                         -> sleep now / wake now
// Every argument is checked first: one bad value and nothing changes (400).
inline void handleSleep() {
  int on = -1, at = -1, wake = -1, nap = -1, act = -1;
  if (server.hasArg("on")) {
    const String v = server.arg("on");
    if (v != "0" && v != "1") { server.send(400, "text/plain", "on must be 0 or 1"); return; }
    on = v == "1";
  }
  if (server.hasArg("at") && (at = parseHHMM(server.arg("at"))) < 0) {
    server.send(400, "text/plain", "at must be HH:MM"); return;
  }
  if (server.hasArg("wake") && (wake = parseHHMM(server.arg("wake"))) < 0) {
    server.send(400, "text/plain", "wake must be HH:MM"); return;
  }
  if (server.hasArg("nap")) {
    const String v = server.arg("nap");
    const long n = v.toInt();
    if ((n == 0 && v != "0") || n < 0 || n > sleepsched::kNapMaxMin) {
      server.send(400, "text/plain", "nap must be 0-120"); return;
    }
    nap = (int)n;
  }
  if (server.hasArg("do")) {
    const String v = server.arg("do");
    if (v == "sleep") act = 1;
    else if (v == "wake") act = 0;
    else { server.send(400, "text/plain", "do must be sleep or wake"); return; }
  }
  ctlSleep(on, at, wake, nap, act);
  server.send(200, "application/json", sleepJson());
}
// /bot/motion?level=off|rare|often (or 0-2)  &  ?move=tilt|wobble|pendulum|drift|spin|spin2|flip
inline void handleMotion() {
  int level = -1, move = 0;
  if (server.hasArg("level")) {
    String l = server.arg("level");
    level = l == "off" ? 0 : l == "rare" ? 1 : l == "often" ? 2 : l.toInt();
  }
  if (server.hasArg("move")) {
    String m = server.arg("move");
    for (uint8_t i = 1; i < MOVE_NUM; i++) if (m == faceMoveNames[i]) move = i;
  }
  ctlMotion(level, move);
  server.send(200, "text/plain", "ok");
}
// /bot/ferro?v=1|0 (now / back to face)  &  ?visit=1 (a 60 s visit)  &  ?set=off|on|auto
//            &  ?pat=crown5 (see ferroPatternNames)  &  ?tent=reach|feeler|flick|arm
// Party mode (the ferrofluid drop; named like vizBot's /bot/party):
// ?v=1|0 now / back to the face, ?visit=1 a 60 s visit, ?set=off|auto|on,
// ?seq=<pattern> (vizMini or vizBot names), ?hero=reach|feeler|flick|arm,
// ?stack=off|some|wild. /bot/ferro is the old name, kept as an alias.
inline void handleParty() {
  if (server.hasArg("set")) ctlFerroSetting(server.arg("set"));
  if (server.hasArg("visit")) ctlFerroVisit();
  if (server.hasArg("v")) ctlSetFerro(argU8("v", 1) != 0);
  if (server.hasArg("stack")) ctlPartyStack(server.arg("stack"));
  String seq = server.hasArg("seq") ? server.arg("seq") : server.arg("pat");
  String hero = server.hasArg("hero") ? server.arg("hero") : server.arg("tent");
  if (seq.length() || hero.length()) ctlFerroPlay(seq, hero);
  server.send(200, "text/plain", "ok");
}
// /bot/leds?mode=mood|ambient|solid|off &bri=0-255 &pat=auto|<pattern>
//   &pal=auto|<palette> &speed=1-10 &pace=chill|normal|fast &c1/c2/c3=RRGGBB
inline void handleLeds() {
  int mode = -1, bri = -1;
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    for (uint8_t i = 0; i < LEDS_NUM_MODES; i++) if (m == ledModeNames[i]) mode = i;
    if (mode < 0 && m.length() == 1 && isDigit(m[0])) mode = m.toInt();
  }
  if (server.hasArg("bri")) bri = constrain(server.arg("bri").toInt(), 0, 255);
  ctlSetLeds(mode, bri);
  if (server.hasArg("pat")) leds.setPattern(server.arg("pat"));
  if (server.hasArg("pal")) leds.setPalette(server.arg("pal"));
  if (server.hasArg("speed")) leds.setSpeed(server.arg("speed").toInt());
  if (server.hasArg("pace")) leds.setPace(server.arg("pace"));
  static const char *const ck[3] = { "c1", "c2", "c3" };
  for (int i = 0; i < 3; i++) if (server.hasArg(ck[i])) leds.setColor(i, server.arg(ck[i]));
  server.send(200, "text/plain", "ok");
}
inline void handleState()      { server.send(200, "application/json", ctlStateJson()); }
// Render one moment of the boot intro and return that frame (for review):
// ?t=<ms> the opening/joining, or ?k=<ms>&ap=0|1 the result and swell
extern void ctlIntroFrame(long t, long k, bool ap);
inline void handleIntroFrame() {
  ctlIntroFrame(server.hasArg("t") ? server.arg("t").toInt() : -1, server.hasArg("k") ? server.arg("k").toInt() : -1,
                server.arg("ap") == "1");
  server.send_P(200, "application/octet-stream", (PGM_P)u8g2.getBufferPtr(), OLED_WIDTH * OLED_HEIGHT / 8);
}
// Raw U8g2 frame buffer (1024 bytes: 8 pages of 128 columns, LSB = top pixel)
inline void handleScreen() {
  server.send_P(200, "application/octet-stream", (PGM_P)u8g2.getBufferPtr(), 8 * u8g2.getBufferTileWidth() * u8g2.getBufferTileHeight() * 8 / 8);
}

// ---- OTA firmware update (writes to the spare app slot, then reboots) ------
inline void handleUpdatePage() { server.send_P(200, "text/html", UPDATE_HTML); }

// Checks before anything is written: the file name carries BOARD_TYPE (the
// builds/ name, vizmini-c3-v<version>.bin), the first byte is the ESP image
// magic 0xE9 and the header names the ESP32-C3. The upload also takes the
// network: no weather fetch or cloud call starts, and one already running is
// waited out (the WebServer blocks the loop for the whole upload anyway).
static const char *g_upErr = nullptr;    // why this upload was refused, nullptr = fine so far
static bool        g_upBegun = false;

inline void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] start: %s\n", up.filename.c_str());
    g_upErr = nullptr; g_upBegun = false;
    if (up.filename.indexOf(BOARD_TYPE) < 0) g_upErr = "Wrong board type: the file name must contain " BOARD_TYPE;
    else if (cloudOtaRunning()) g_upErr = "A cloud firmware update is running. Try again in a few minutes";
    else if (!netHoldForUpload(12000)) g_upErr = "Busy with a cloud call. Try again in a few seconds";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_upErr) return;
    netUploadTouch();
    if (!g_upBegun) {                    // first chunk: the image header
      const esp_image_header_t *h = (const esp_image_header_t *)up.buf;
      if (up.currentSize < sizeof(esp_image_header_t) || up.buf[0] != ESP_IMAGE_HEADER_MAGIC) { g_upErr = "Not an ESP32 firmware image (bad magic byte)"; return; }
      if (h->chip_id != ESP_CHIP_ID_ESP32C3) { g_upErr = "That firmware is for another chip, not the ESP32-C3"; return; }
      cloudAcceptRunningImage();         // the owner's own upload ends a cloud update's probation
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); g_upErr = Update.errorString(); return; }
      g_upBegun = true;
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_upErr) { Serial.printf("[ota] refused: %s\n", g_upErr); }
    else if (!g_upBegun) g_upErr = "Empty file";
    else if (Update.end(true)) Serial.printf("[ota] ok: %u bytes\n", (unsigned)up.totalSize);
    else Update.printError(Serial);
    netUploadDone();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (g_upBegun) Update.abort();
    g_upBegun = false;
    g_upErr = "Upload aborted";
    netUploadDone();
  }
}

inline void handleUpdateDone() {
  bool ok = !g_upErr && g_upBegun && !Update.hasError();
  netUploadDone();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok ? "OK rebooting" : (String("FAIL ") + (g_upErr ? g_upErr : Update.errorString())));
  if (ok) { delay(400); ESP.restart(); }
}

// ---- device name and cloud server (System and Cloud cards) ----------------
// POST /device/name v=<name> ("" = back to the MAC default)
// -> {"ok":true,"name":..,"host":..} or 400 {"ok":false,"why":..}
inline void handleDeviceName() {
  String v = server.arg("v");
  v.trim();
  if (!deviceNameSet(v.c_str())) {
    server.send(400, "application/json", "{\"ok\":false,\"why\":\"1 to 23 plain characters, no quotes or backslashes\"}");
    return;
  }
  webApplyName();
  server.send(200, "application/json", "{\"ok\":true,\"name\":" + jsonStr(g_devName) + ",\"host\":\"" + String(g_devHost) + "\"}");
}
// POST /cloud/server url=<http(s)://host[:port]> ("" = the built-in server)
inline void handleCloudServer() {
  const char *why = cloudSetServer(server.arg("url"));
  if (why) { server.send(400, "application/json", "{\"ok\":false,\"why\":" + jsonStr(why) + "}"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
}

// /debug/leds?id=runs|path[&s=seconds] — wiring check (see StripLeds::identify)
inline void handleLedId() {
  bool path = server.arg("id") == "path";
  uint32_t secs = server.hasArg("s") ? constrain(server.arg("s").toInt(), 1, 120) : 20;
  leds.identify(path, secs * 1000UL);
  server.send(200, "text/plain", path ? "path" : "runs");
}

inline void handleNotFound() {
  if (g_apMode) {  // captive-portal: bounce everything back to setup
    server.sendHeader("Location", String("http://") + g_apIP.toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "not found");
  }
}

inline void registerRoutes() {
  server.on("/", handleRoot);
  server.on("/wifi/scan", handleScan);
  server.on("/wifi/try", HTTP_POST, handleTry);
  server.on("/wifi/status", handleWifiStatus);
  server.on("/bot/expression", handleExpression);
  server.on("/bot/say", handleSay);
  server.on("/bot/info", handleInfo);
  server.on("/bot/snow", handleSnow);
  server.on("/bot/spark", handleSpark);
  server.on("/bot/sleep", handleSleep);
  server.on("/bot/leds", handleLeds);
  server.on("/bot/motion", handleMotion);
  server.on("/bot/party", handleParty);
  server.on("/bot/ferro", handleParty);   // old name
  server.on("/contrast", handleContrast);
  server.on("/state", handleState);
  server.on("/debug/screen", handleScreen);
  server.on("/debug/intro", handleIntroFrame);
  server.on("/debug/leds", handleLedId);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/device/name", HTTP_POST, handleDeviceName);
  server.on("/cloud/server", HTTP_POST, handleCloudServer);
  server.onNotFound(handleNotFound);
}

// ============================================================================
// Bring-up: try the saved network, else fall back to an AP + captive portal
// ============================================================================
inline bool loadCreds() {
  wifiPrefs.begin(NVS_NAMESPACE, true);
  g_ssid = wifiPrefs.getString("ssid", "");
  g_pass = wifiPrefs.getString("pass", "");
  wifiPrefs.end();
  return g_ssid.length() > 0;
}

// Boot WiFi diagnostics: every STA event with its time and reason code
inline void wifiEventLog(WiFiEvent_t ev, WiFiEventInfo_t info) {
  switch (ev) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:    Serial.printf("[wifi] %lu ms associated ch %d\n", millis(), info.wifi_sta_connected.channel); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:       Serial.printf("[wifi] %lu ms got ip\n", millis()); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      g_lastReason = info.wifi_sta_disconnected.reason;
      if (g_join == JOIN_TRYING && reasonIsAuth(g_lastReason)) g_authFails++;
      Serial.printf("[wifi] %lu ms disconnected reason %d rssi %d\n", millis(),
                                                info.wifi_sta_disconnected.reason, info.wifi_sta_disconnected.rssi); break;
    default: break;
  }
}

// Join the saved network. onWait runs every ~40 ms while it tries, so the
// boot screen can animate instead of freezing.
//
// The scan method is the fix for "not connected" at boot (2026-11-03): with
// the default fast scan the C3 fired hundreds of join attempts 30 ms apart
// (reason 1 / 203), hit a 202 auth fail that stops the retries, and fell back
// to the hotspot on 4 of 5 boots. A full all-channel scan first joins on the
// first try (5/5, IP at ~7 s); putting the fast scan back brought the storm
// straight back. vizbot's wifi_provisioning.h uses the same settings.
inline bool tryStation(void (*onWait)(unsigned long ms)) {
  if (!loadCreds()) return false;
  WiFi.persistent(false);
  WiFi.setHostname(g_devHost);       // the router's client list shows his name
  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setSleep(false);
  // IPv6 link-local so mDNS answers the AAAA half of a lookup: macOS asks for
  // A and AAAA together and waited 5 s for an AAAA reply that never came
  WiFi.enableIPv6();
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_CONNECT_TIMEOUT_MS) {
    if (onWait) onWait(millis() - start);
    delay(40);
  }
  return WiFi.status() == WL_CONNECTED;
}

inline void startAccessPoint() {
  g_apMode = true;
  String apName = g_devName;          // his device name (device_name.h)
  // AP+STA always: the STA side scans for the setup page, test-joins a network
  // from it, and (with saved creds) keeps trying the real network in the
  // background while the setup page is served.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(g_apIP, g_apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str());
  dnsServer.start(53, "*", g_apIP);  // captive portal
  if (g_ssid.length() > 0) {
    WiFi.enableIPv6();
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    g_lastStaTry = millis();
  } else {
    WiFi.scanNetworks(true);         // so the setup page has a list straight away
  }
}

// Background reconnect succeeded: drop the AP, go pure STA, start mDNS + NTP.
inline void promoteToStation() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_apMode = false;
  if (MDNS.begin(g_devHost)) MDNS.addService("http", "tcp", 80);
  configTzTime(TZ_POSIX, NTP_SERVER_1, NTP_SERVER_2);
  Serial.printf("[vizMini] reconnected STA -> %s\n", WiFi.localIP().toString().c_str());
}

// Returns the address the user can reach the bot at.
inline String webBegin(void (*onWait)(unsigned long ms) = nullptr) {
  String where;
  WiFi.onEvent(wifiEventLog);
  if (tryStation(onWait)) {
    g_apMode = false;
    if (MDNS.begin(g_devHost)) MDNS.addService("http", "tcp", 80);
    where = WiFi.localIP().toString();
  } else {
    startAccessPoint();
    where = g_devName;
  }
  registerRoutes();
  server.begin();
  return where;
}

inline void webLoop() {
  if (g_apMode) {
    dnsServer.processNextRequest();
    if (g_join == JOIN_TRYING) joinPoll();     // setup page test join
    if (g_rebootAt && (long)(millis() - g_rebootAt) >= 0) ESP.restart();
    bool setupBusy = g_join == JOIN_TRYING || g_join == JOIN_OK ||
                     (g_setupSeenAt && millis() - g_setupSeenAt < SETUP_QUIET_MS);
    if (g_ssid.length() > 0 && !setupBusy) {   // background reconnect attempts
      if (WiFi.status() == WL_CONNECTED) {
        promoteToStation();                    // network came back — leave AP
      } else if (millis() - g_lastStaTry > STA_RETRY_MS) {
        g_lastStaTry = millis();
        WiFi.begin(g_ssid.c_str(), g_pass.c_str());
      }
    }
  }
  server.handleClient();
}

inline bool webIsAP()     { return g_apMode; }
// A new device name: the .local address follows at once. The hotspot keeps
// its old name until it next starts, so a phone on it is not thrown off.
inline void webApplyName() {
  if (g_apMode) return;
  MDNS.end();
  if (MDNS.begin(g_devHost)) MDNS.addService("http", "tcp", 80);
}
inline const String &webSsid() { return g_ssid; }
inline String webIP()     { return g_apMode ? g_apIP.toString() : WiFi.localIP().toString(); }

#endif // WEB_UI_H
