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

// ---- Control hooks implemented in vizmini.ino ------------------------------
extern void   ctlSetExpression(uint8_t v);
extern void   ctlSay(const char* text);
extern void   ctlSetInfo(bool on);
extern void   ctlSetContrast(uint8_t c);
extern void   ctlSetSnow(bool on);
extern void   ctlSpark();
extern void   ctlSetLeds(int mode, int bri);   // -1 = leave unchanged
extern void   ctlMotion(int level, int move);  // -1 / 0 = leave unchanged
extern void   ctlSetFerro(bool on);
extern void   ctlFerroPlay(const String &pat, const String &tent);
extern void   ctlFerroSetting(const String &v);
extern void   ctlFerroVisit();
extern String ctlStateJson();

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

inline String chipSuffix() {
  uint32_t id = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", id);
  return String(buf);
}

// ============================================================================
// HTML — control page (STA mode) and provisioning page (AP mode)
// ============================================================================
#include "web_page.h"   // CONTROL_HTML: the control page (vizBot house style)

static const char PROVISION_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>vizMini setup</title><style>
:root{color-scheme:dark}body{margin:0;background:#0b0f14;color:#e6edf3;
font:15px system-ui,sans-serif;padding:18px}h1{font-size:18px}
input{width:100%;box-sizing:border-box;background:#0d1117;color:#e6edf3;
border:1px solid #30363d;border-radius:8px;padding:10px;margin:6px 0}
button{width:100%;background:#238636;color:#fff;border:0;border-radius:8px;
padding:11px;font-size:15px;margin-top:8px}label{font-size:12px;color:#7d8590}
</style></head><body><h1>Connect vizMini to WiFi</h1>
<form action="/wifi/save" method="POST">
<label>Network (SSID)</label><input name=ssid required>
<label>Password</label><input name=pass type=password>
<button type=submit>Save &amp; reboot</button></form>
<p style="margin-top:16px"><a href="/update" style="color:#58a6ff">Firmware update</a></p></body></html>
)HTML";

static const char UPDATE_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>vizMini update</title><style>
:root{color-scheme:dark}body{margin:0;background:#0b0f14;color:#e6edf3;
font:15px system-ui,sans-serif;padding:18px}h1{font-size:18px;margin:0 0 4px}
.sub{color:#7d8590;font-size:12px;margin-bottom:14px}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:14px}
input[type=file]{width:100%;margin:8px 0;color:#e6edf3}
button{width:100%;background:#238636;color:#fff;border:0;border-radius:8px;
padding:11px;font-size:15px;margin-top:6px}button:disabled{opacity:.5}
progress{width:100%;height:14px;margin-top:10px}#msg{margin-top:10px;font-size:14px}
a{color:#58a6ff}.hint{color:#7d8590;font-size:12px;margin-top:10px}
</style></head><body>
<h1>vizMini firmware</h1><div class=sub id=ver>checking&hellip;</div>
<div class=card>
<input type=file id=f accept=".bin">
<button id=b onclick=up()>Flash firmware</button>
<progress id=pg max=100 value=0 hidden></progress>
<div id=msg></div>
<div class=hint>Upload <code>.pio/build/c3-oled/firmware.bin</code>.</div>
</div>
<p><a href="/">&larr; back</a></p>
<script>
fetch('/state').then(r=>r.json()).then(s=>{ver.textContent='running v'+s.fw}).catch(function(){ver.textContent=''});
function M(t){msg.textContent=t}
function up(){
 var fl=f.files[0]; if(!fl){M('Pick a firmware.bin first.');return;}
 b.disabled=true; pg.hidden=false; M('Uploading…');
 var fd=new FormData(); fd.append('firmware',fl);
 var x=new XMLHttpRequest(); x.open('POST','/update');
 x.upload.onprogress=function(e){if(e.lengthComputable)pg.value=Math.round(e.loaded/e.total*100)};
 x.onload=function(){if(x.status==200&&x.responseText.slice(0,2)=='OK'){M('✅ Success — rebooting. Reconnect in ~10s.')}else{M('❌ Failed: '+x.responseText);b.disabled=false}};
 x.onerror=function(){M('Connection lost (the device may be rebooting if the upload finished).')};
 x.send(fd);
}
</script></body></html>
)HTML";

// ============================================================================
// Request handlers
// ============================================================================
inline uint8_t argU8(const char* name, uint8_t def = 0) {
  if (!server.hasArg(name)) return def;
  return (uint8_t)constrain(server.arg(name).toInt(), 0, 255);
}

inline void handleRoot() {
  if (g_apMode) server.send_P(200, "text/html", PROVISION_HTML);
  else          server.send_P(200, "text/html", CONTROL_HTML);
}

inline void handleWifiSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0) { server.send(400, "text/plain", "ssid required"); return; }
  wifiPrefs.begin(NVS_NAMESPACE, false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  wifiPrefs.end();
  server.send(200, "text/html",
    "<meta charset=utf-8><body style='background:#0b0f14;color:#e6edf3;"
    "font:15px system-ui;padding:20px'>Saved. Rebooting&hellip;</body>");
  delay(600);
  ESP.restart();
}

inline void handleExpression() { ctlSetExpression(argU8("v", EXPR_NEUTRAL)); server.send(200, "text/plain", "ok"); }
inline void handleSay()        { ctlSay(server.arg("text").c_str());        server.send(200, "text/plain", "ok"); }
inline void handleInfo()       { ctlSetInfo(argU8("v", 1) != 0);            server.send(200, "text/plain", "ok"); }
inline void handleContrast()   { ctlSetContrast(argU8("v", DEFAULT_CONTRAST)); server.send(200, "text/plain", "ok"); }
inline void handleSnow()       { ctlSetSnow(argU8("v", 1) != 0);            server.send(200, "text/plain", "ok"); }
inline void handleSpark()      { ctlSpark();                                server.send(200, "text/plain", "ok"); }
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
inline void handleFerro() {
  if (server.hasArg("set")) ctlFerroSetting(server.arg("set"));
  if (server.hasArg("visit")) ctlFerroVisit();
  if (server.hasArg("v")) ctlSetFerro(argU8("v", 1) != 0);
  if (server.hasArg("pat") || server.hasArg("tent")) ctlFerroPlay(server.arg("pat"), server.arg("tent"));
  server.send(200, "text/plain", "ok");
}
inline void handleLeds() {
  int mode = -1, bri = -1;
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    mode = m == "mood" ? 0 : m == "ambient" ? 1 : m == "off" ? 2 : m.toInt();
  }
  if (server.hasArg("bri")) bri = constrain(server.arg("bri").toInt(), 0, 255);
  ctlSetLeds(mode, bri);
  server.send(200, "text/plain", "ok");
}
inline void handleState()      { server.send(200, "application/json", ctlStateJson()); }
// Raw U8g2 frame buffer (1024 bytes: 8 pages of 128 columns, LSB = top pixel)
inline void handleScreen() {
  server.send_P(200, "application/octet-stream", (PGM_P)u8g2.getBufferPtr(), 8 * u8g2.getBufferTileWidth() * u8g2.getBufferTileHeight() * 8 / 8);
}

// ---- OTA firmware update (writes to the spare app slot, then reboots) ------
inline void handleUpdatePage() { server.send_P(200, "text/html", UPDATE_HTML); }

inline void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[ota] ok: %u bytes\n", (unsigned)up.totalSize);
    else Update.printError(Serial);
  }
}

inline void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok ? "OK rebooting" : (String("FAIL ") + Update.errorString()));
  if (ok) { delay(400); ESP.restart(); }
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
  server.on("/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/bot/expression", handleExpression);
  server.on("/bot/say", handleSay);
  server.on("/bot/info", handleInfo);
  server.on("/bot/snow", handleSnow);
  server.on("/bot/spark", handleSpark);
  server.on("/bot/leds", handleLeds);
  server.on("/bot/motion", handleMotion);
  server.on("/bot/ferro", handleFerro);
  server.on("/contrast", handleContrast);
  server.on("/state", handleState);
  server.on("/debug/screen", handleScreen);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
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
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: Serial.printf("[wifi] %lu ms disconnected reason %d rssi %d\n", millis(),
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
  WiFi.onEvent(wifiEventLog);
  WiFi.persistent(false);
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
  String apName = String(WIFI_AP_BASE) + "-" + chipSuffix();
  // If we have saved creds, run AP+STA so we can keep trying to join the real
  // network in the background while still serving the setup/control page.
  WiFi.mode(g_ssid.length() > 0 ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(g_apIP, g_apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str());
  dnsServer.start(53, "*", g_apIP);  // captive portal
  if (g_ssid.length() > 0) {
    WiFi.enableIPv6();
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    g_lastStaTry = millis();
  }
}

// Background reconnect succeeded: drop the AP, go pure STA, start mDNS + NTP.
inline void promoteToStation() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_apMode = false;
  if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);
  configTzTime(TZ_POSIX, NTP_SERVER_1, NTP_SERVER_2);
  Serial.printf("[vizMini] reconnected STA -> %s\n", WiFi.localIP().toString().c_str());
}

// Returns the address the user can reach the bot at.
inline String webBegin(void (*onWait)(unsigned long ms) = nullptr) {
  String where;
  if (tryStation(onWait)) {
    g_apMode = false;
    if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);
    where = WiFi.localIP().toString();
  } else {
    startAccessPoint();
    where = String(WIFI_AP_BASE) + "-" + chipSuffix();
  }
  registerRoutes();
  server.begin();
  return where;
}

inline void webLoop() {
  if (g_apMode) {
    dnsServer.processNextRequest();
    if (g_ssid.length() > 0) {                 // background reconnect attempts
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
inline const String &webSsid() { return g_ssid; }
inline String webIP()     { return g_apMode ? g_apIP.toString() : WiFi.localIP().toString(); }

#endif // WEB_UI_H
