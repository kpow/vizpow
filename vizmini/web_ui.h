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
static const char CONTROL_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>vizMini</title><style>
:root{color-scheme:dark}
body{margin:0;background:#0b0f14;color:#e6edf3;font:15px system-ui,sans-serif;padding:16px}
h1{font-size:18px;margin:0 0 4px}.sub{color:#7d8590;font-size:12px;margin-bottom:14px}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:12px;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(84px,1fr));gap:6px}
button{background:#21262d;color:#e6edf3;border:1px solid #30363d;border-radius:8px;
 padding:9px 6px;font-size:13px;cursor:pointer}button:active{background:#2d333b}
input[type=text]{width:100%;box-sizing:border-box;background:#0d1117;color:#e6edf3;
 border:1px solid #30363d;border-radius:8px;padding:9px}
input[type=range]{width:100%}.row{display:flex;gap:8px;margin-top:8px}
label{font-size:12px;color:#7d8590}
</style></head><body>
<h1>vizMini</h1><div class=sub id=sub>the little yeti</div>

<div class=card><label>Say something</label>
<div class=row><input id=t type=text maxlength=48 placeholder="hello!">
<button onclick="say()">Say</button></div></div>

<div class=card><label>Expression</label>
<div class=grid id=ex></div></div>

<div class=card><label>Contrast</label>
<input id=c type=range min=10 max=255 value=180 oninput="g('/contrast?v='+this.value)">
<div class=row>
<button onclick="g('/bot/info?v=1')">Time/Wx</button>
<button onclick="snow()">Snow</button>
<button onclick="g('/bot/spark')">Spark</button>
</div>
<div class=row>
<button onclick="g('/bot/ferro?v=1')">Ferro now</button>
<button onclick="g('/bot/ferro?visit=1')">Visit (60s)</button>
<button onclick="g('/bot/ferro?v=0')">Back to face</button>
</div>
<div class=row>
<button onclick="fs('off')">Ferro: off</button>
<button onclick="fs('auto')">auto</button>
<button onclick="fs('on')">always</button>
<span id=fsl></span>
</div>
<div class=grid id=fp></div></div>

<div class=card><label>Lights</label>
<div class=row>
<button onclick="g('/bot/leds?mode=0')">Mood</button>
<button onclick="g('/bot/leds?mode=1')">Ambient</button>
<button onclick="g('/bot/leds?mode=2')">Off</button>
</div>
<input id=lb type=range min=0 max=255 value=160 onchange="g('/bot/leds?bri='+this.value)"></div>

<div class=card><label>Motion <span id=ml></span></label>
<div class=row>
<button onclick="lvl('off')">Off</button>
<button onclick="lvl('rare')">Rare</button>
<button onclick="lvl('often')">Often</button>
</div>
<div class=grid id=mv></div></div>

<div class=card><a href="/update" style="color:#58a6ff;text-decoration:none">&#8593; Firmware update</a></div>

<script>
var X=["neutral","happy","sad","surprised","chill","angry","love","dizzy","thinking",
"excited","mischief","skeptical","worried","confused","proud","shy","annoyed","focused",
"winking","devious","shocked","kissing","nervous","glitch","sassy"];
var snowOn=true;
function g(u){fetch(u)}
function say(){var v=document.getElementById('t').value;if(v)fetch('/bot/say?text='+encodeURIComponent(v))}
function snow(){snowOn=!snowOn;g('/bot/snow?v='+(snowOn?1:0))}
var e=document.getElementById('ex');
X.forEach(function(n,i){var b=document.createElement('button');b.textContent=n;
 b.onclick=function(){g('/bot/expression?v='+i)};e.appendChild(b)});
function fs(n){g('/bot/ferro?set='+n);document.getElementById('fsl').textContent=n}
function lvl(n){g('/bot/motion?level='+n);document.getElementById('ml').textContent='('+n+')'}
["tilt","wobble","pendulum","drift","spin","spin2","flip"].forEach(function(n){
 var b=document.createElement('button');b.textContent=n;
 b.onclick=function(){g('/bot/motion?move='+n)};document.getElementById('mv').appendChild(b)});
["crown3","crown5","star6","field12","dipole","porcupine","split","wave","breathe","bloom","carousel"].forEach(function(n){
 var b=document.createElement('button');b.textContent=n;b.onclick=function(){g('/bot/ferro?pat='+n)};document.getElementById('fp').appendChild(b)});
["reach","feeler","flick","arm"].forEach(function(n){
 var b=document.createElement('button');b.textContent='~'+n;b.onclick=function(){g('/bot/ferro?tent='+n)};document.getElementById('fp').appendChild(b)});
fetch('/state').then(r=>r.json()).then(s=>{document.getElementById('c').value=s.contrast;
 document.getElementById('lb').value=s.ledBri;
 document.getElementById('ml').textContent='('+s.motion+')';
 document.getElementById('fsl').textContent=s.ferroSet;
 document.getElementById('sub').textContent='v'+s.fw+' · '+s.ip}).catch(()=>{});
</script></body></html>
)HTML";

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

inline bool tryStation() {
  if (!loadCreds()) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_CONNECT_TIMEOUT_MS) {
    delay(150);
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
inline String webBegin() {
  String where;
  if (tryStation()) {
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
inline String webIP()     { return g_apMode ? g_apIP.toString() : WiFi.localIP().toString(); }

#endif // WEB_UI_H
