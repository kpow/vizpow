#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include "config.h"
#include "palettes.h"

// External references to globals
extern WebServer server;
extern DNSServer dnsServer;
extern uint8_t effectIndex;
extern uint8_t paletteIndex;
extern uint8_t brightness;
extern uint8_t speed;
extern bool autoCycle;
extern void resetEffectShuffle();
extern uint8_t currentMode;
extern CRGBPalette16 currentPalette;

// System status (populated by boot_sequence.h)
#include "system_status.h"

// Web interface HTML
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>VizBot</title>
  <style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:-apple-system,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;min-height:100vh;padding:20px}h1{text-align:center;margin-bottom:20px;font-size:24px}h2{font-size:14px;color:#888;margin-bottom:10px;text-transform:uppercase}.card{background:rgba(255,255,255,.1);border-radius:16px;padding:20px;margin-bottom:16px}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}button{background:rgba(255,255,255,.15);border:none;color:#fff;padding:14px 10px;border-radius:12px;font-size:13px;cursor:pointer}button.active{background:#6366f1}.slider-container{margin:15px 0}.slider-label{display:flex;justify-content:space-between;margin-bottom:8px}input[type=range]{width:100%;height:8px;border-radius:4px;background:rgba(255,255,255,.2);-webkit-appearance:none}input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;border-radius:50%;background:#6366f1;cursor:pointer}.toggle-row{display:flex;justify-content:space-between;align-items:center;padding:10px 0}.toggle{width:52px;height:32px;background:rgba(255,255,255,.2);border-radius:16px;position:relative;cursor:pointer}.toggle.on{background:#6366f1}.toggle::after{content:'';position:absolute;width:26px;height:26px;background:#fff;border-radius:50%;top:3px;left:3px;transition:transform .3s}.toggle.on::after{transform:translateX(20px)}.status{text-align:center;color:#888;font-size:12px;margin-top:10px}</style>
</head>
<body>
  <h1>VizBot Control</h1>

  <div class="card">
    <h2>Expressions</h2>
    <div class="grid" id="botExpressions"></div>
  </div>

  <div class="card">
    <h2>Say Something</h2>
    <div style="display:flex;gap:8px">
      <input type="text" id="botSayInput" placeholder="Type a message..."
        style="flex:1;padding:10px;border-radius:8px;border:none;background:rgba(255,255,255,0.15);color:#fff;font-size:14px" maxlength="30">
      <button onclick="sendBotSay()" style="padding:10px 16px">Say</button>
    </div>
  </div>

  <div class="card">
    <h2>Face Color</h2>
    <div class="grid4" id="botColors"></div>
    <h2 style="margin-top:15px">Background</h2>
    <div class="grid" id="botBgStyles"></div>
  </div>

  <div class="card">
    <h2>Settings</h2>
    <div class="slider-container">
      <div class="slider-label"><span>Brightness</span><span id="brightnessVal">15</span></div>
      <input type="range" id="brightness" min="1" max="50" value="15">
    </div>
    <div class="toggle-row">
      <span>Time Overlay</span>
      <div class="toggle" id="botTimeToggle" onclick="toggleBotTime()"></div>
    </div>
  </div>

  <div class="status">Connected to VizBot &middot; vizbot.local</div>

  <script>
    const botExprNames = ["Neutral", "Happy", "Sad", "Surprised", "Sleepy", "Angry", "Love", "Dizzy", "Thinking", "Excited", "Mischief", "Dead", "Skeptical", "Worried", "Confused", "Proud", "Shy", "Annoyed", "Bliss", "Focused"];
    const botColorNames = ["White", "Cyan", "Green", "Pink", "Yellow"];
    const botBgStyles = [{n:"Black",v:0},{n:"Ambient",v:4}];
    let curBgStyle = 4;

    function render() {
      document.getElementById('botExpressions').innerHTML = botExprNames.map((name, i) =>
        `<button onclick="setBotExpr(${i})">${name}</button>`
      ).join('');
      document.getElementById('botColors').innerHTML = botColorNames.map((name, i) =>
        `<button onclick="setBotColor(${i})">${name}</button>`
      ).join('');
      document.getElementById('botBgStyles').innerHTML = botBgStyles.map(s =>
        `<button class="${curBgStyle === s.v ? 'active' : ''}" onclick="setBotBgStyle(${s.v})">${s.n}</button>`
      ).join('');
    }

    async function api(endpoint) {
      try { await fetch(endpoint); } catch(e) {}
    }

    function setBotExpr(i) { api('/bot/expression?v=' + i); }
    function sendBotSay() {
      const input = document.getElementById('botSayInput');
      if (input.value.trim()) {
        api('/bot/say?text=' + encodeURIComponent(input.value.trim()));
        input.value = '';
      }
    }
    let botTimeOn = false;
    function toggleBotTime() {
      botTimeOn = !botTimeOn;
      document.getElementById('botTimeToggle').className = 'toggle ' + (botTimeOn ? 'on' : '');
      api('/bot/time?v=' + (botTimeOn ? 1 : 0));
    }
    function setBotColor(i) { api('/bot/background?v=' + i); }
    function setBotBgStyle(i) { curBgStyle = i; render(); api('/bot/background?style=' + i); }

    document.getElementById('brightness').oninput = function() {
      document.getElementById('brightnessVal').textContent = this.value;
    };
    document.getElementById('brightness').onchange = function() { api('/brightness?v=' + this.value); };

    async function getState() {
      try {
        const r = await fetch('/state');
        const state = await r.json();
        document.getElementById('brightness').value = state.brightness;
        document.getElementById('brightnessVal').textContent = state.brightness;
      } catch(e) {}
    }

    getState();
    render();
  </script>
</body>
</html>
)rawliteral";

// ============================================================================
// Web Server Handlers
// ============================================================================
// These push to the command queue (Sprint 2) so state changes are
// applied atomically between frames on Core 1.

void handleRoot() {
  server.send(200, "text/html", webpage);
}

void handleState() {
  String json = "{\"brightness\":" + String(brightness) +
                ",\"speed\":" + String(speed) +
                ",\"autoCycle\":" + (autoCycle ? "true" : "false") +
                ",\"sys\":{" +
                  "\"lcd\":" + (sysStatus.lcdReady ? "true" : "false") +
                  ",\"leds\":" + (sysStatus.ledsReady ? "true" : "false") +
                  ",\"i2c\":" + (sysStatus.i2cReady ? "true" : "false") +
                  ",\"imu\":" + (sysStatus.imuReady ? "true" : "false") +
                  ",\"touch\":" + (sysStatus.touchReady ? "true" : "false") +
                  ",\"wifi\":" + (sysStatus.wifiReady ? "true" : "false") +
                  ",\"dns\":" + (sysStatus.dnsReady ? "true" : "false") +
                  ",\"mdns\":" + (sysStatus.mdnsReady ? "true" : "false") +
                  ",\"bootMs\":" + String(sysStatus.bootTimeMs) +
                  ",\"fails\":" + String(sysStatus.failCount) +
                  ",\"bootReason\":" + String(sysStatus.bootReason) +
                "}}";
  server.send(200, "application/json", json);
}

// Command queue helpers (defined in task_manager.h)
extern void cmdSetBrightness(uint8_t val);
extern void cmdSetExpression(uint8_t val);
extern void cmdSetFaceColor(uint16_t color);
extern void cmdSetBgStyle(uint8_t val);
extern void cmdSayText(const char* text, uint16_t durationMs);
extern void cmdSetTimeOverlay(bool enabled);
extern void cmdToggleTimeOverlay();

void handleBrightness() {
  if (server.hasArg("v")) {
    uint8_t val = constrain(server.arg("v").toInt(), 1, 50);
    cmdSetBrightness(val);
  }
  server.send(200, "text/plain", "OK");
}

// Bot mode handlers — push commands to queue instead of direct writes
void handleBotExpression() {
  if (server.hasArg("v")) {
    uint8_t expr = constrain(server.arg("v").toInt(), 0, BOT_NUM_EXPRESSIONS - 1);
    cmdSetExpression(expr);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotSay() {
  if (server.hasArg("text")) {
    String text = server.arg("text");
    uint16_t dur = 4000;
    if (server.hasArg("dur")) {
      dur = constrain(server.arg("dur").toInt(), 1000, 10000);
    }
    cmdSayText(text.c_str(), dur);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotTime() {
  if (server.hasArg("v")) {
    if (server.arg("v").toInt() == 2) {
      cmdToggleTimeOverlay();
    } else {
      cmdSetTimeOverlay(server.arg("v").toInt() == 1);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleBotBackground() {
  if (server.hasArg("v")) {
    uint16_t colors[] = { 0xFFFF, 0x07FF, 0x07E0, 0xF81F, 0xFFE0 };
    uint8_t idx = constrain(server.arg("v").toInt(), 0, 4);
    cmdSetFaceColor(colors[idx]);
  }
  if (server.hasArg("style")) {
    uint8_t style = constrain(server.arg("style").toInt(), 0, 4);
    cmdSetBgStyle(style);
  }
  server.send(200, "text/plain", "OK");
}

// ============================================================================
// Captive Portal — redirect OS connectivity checks to control page
// ============================================================================
// When a phone/laptop connects to vizBot WiFi, the OS sends a connectivity
// check (e.g., http://captive.apple.com/hotspot-detect.html). DNS resolves
// ALL domains to 192.168.4.1 (us). We return a 302 redirect so the OS
// detects "captive portal" and auto-opens the control page.

void handleCaptiveRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
  server.send(302, "text/plain", "");
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/brightness", handleBrightness);

  // Bot mode endpoints
  server.on("/bot/expression", handleBotExpression);
  server.on("/bot/say", handleBotSay);
  server.on("/bot/time", handleBotTime);
  server.on("/bot/background", handleBotBackground);

  // Captive portal detection endpoints — all redirect to root
  server.on("/generate_204", handleCaptiveRedirect);          // Android
  server.on("/gen_204", handleCaptiveRedirect);                // Android alt
  server.on("/hotspot-detect.html", handleCaptiveRedirect);    // Apple iOS/macOS
  server.on("/library/test/success.html", handleCaptiveRedirect); // Apple legacy
  server.on("/connecttest.txt", handleCaptiveRedirect);        // Windows
  server.on("/ncsi.txt", handleCaptiveRedirect);               // Windows NCSI
  server.on("/redirect", handleCaptiveRedirect);               // Firefox
  server.on("/canonical.html", handleCaptiveRedirect);         // Firefox alt
  server.on("/check_network_status.txt", handleCaptiveRedirect); // Kindle

  // Catch-all: any unknown URL also redirects to control page
  server.onNotFound(handleCaptiveRedirect);

  server.begin();
  DBGLN("Web server started on port 80 (captive portal enabled)");
}

// Start DNS server (wildcard — all domains resolve to us)
void startDNS() {
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  DBGLN("DNS server started (wildcard -> 192.168.4.1)");
}

// Stop DNS server
void stopDNS() {
  dnsServer.stop();
  DBGLN("DNS server stopped");
}

// Start mDNS (vizbot.local)
bool startMDNS() {
  bool ok = MDNS.begin(MDNS_HOSTNAME);
  if (ok) {
    MDNS.addService("http", "tcp", 80);
    DBG("mDNS started: ");
    DBG(MDNS_HOSTNAME);
    DBGLN(".local");
  } else {
    DBGLN("mDNS failed to start");
  }
  return ok;
}

#endif
