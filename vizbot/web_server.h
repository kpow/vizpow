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
    <div class="grid4" id="botExpressions"></div>
  </div>

  <div class="card">
    <h2>Say Something</h2>
    <div style="display:flex;gap:8px">
      <input type="text" id="botSayInput" placeholder="Type a message..."
        style="flex:1;padding:10px;border-radius:8px;border:none;background:rgba(255,255,255,0.15);color:#fff;font-size:14px" maxlength="60">
      <button onclick="sendBotSay()" style="padding:10px 16px">Say</button>
    </div>
  </div>

  <div class="card">
    <h2>Face Color</h2>
    <div class="grid4" id="botColors"></div>
    <h2 style="margin-top:15px">Background</h2>
    <div class="grid" id="botBgStyles"></div>
    <div id="ambientSection" style="display:none;margin-top:15px">
      <h2>Ambient Animation</h2>
      <div class="grid" id="ambientEffects"></div>
    </div>
  </div>

  <div class="card">
    <h2>Settings</h2>
    <div class="slider-container">
      <div class="slider-label"><span>Brightness</span><span id="brightnessVal">15</span></div>
      <input type="range" id="brightness" min="1" max="50" value="15">
    </div>
    <div class="slider-container">
      <div class="slider-label"><span>Volume</span><span id="volumeVal">120</span></div>
      <input type="range" id="volume" min="0" max="255" value="120">
    </div>
    <div class="toggle-row">
      <span>Time Overlay</span>
      <div class="toggle" id="botTimeToggle" onclick="toggleBotTime()"></div>
    </div>
    <div class="toggle-row">
      <span>Hi-Res Background</span>
      <div class="toggle" id="hiResToggle" onclick="toggleHiRes()"></div>
    </div>
  </div>

  <div class="card">
    <h2>Info Mode</h2>
    <div class="toggle-row">
      <span>Show Weather</span>
      <div class="toggle" id="infoToggle" onclick="toggleInfo()"></div>
    </div>
    <div style="margin-top:12px">
      <div style="color:#aaa;font-size:13px;margin-bottom:6px">Location</div>
      <div style="display:flex;gap:8px">
        <input type="text" id="weatherZip" placeholder="Zip code or city name"
          style="flex:1;padding:10px;border-radius:8px;border:none;background:rgba(255,255,255,0.15);color:#fff;font-size:14px" maxlength="30">
        <button onclick="setLocationZip()" id="zipBtn" style="padding:10px 16px">Set</button>
      </div>
      <div id="locationInfo" style="color:#6b7;font-size:12px;margin-top:6px"></div>
    </div>
  </div>

  <div class="card">
    <h2>WiFi Setup</h2>
    <div style="margin-bottom:12px">
      <div style="color:#aaa;font-size:12px;margin-bottom:6px">Device Name &mdash; sets AP name and .local hostname (restart to apply)</div>
      <div style="display:flex;gap:8px">
        <input type="text" id="deviceNameInput" placeholder="e.g. vizbot-desk"
          style="flex:1;padding:10px;border-radius:8px;border:none;background:rgba(255,255,255,0.15);color:#fff;font-size:14px" maxlength="23">
        <button onclick="setDeviceName()" id="deviceNameBtn" style="padding:10px 16px">Set</button>
      </div>
      <div id="deviceNameStatus" style="font-size:12px;color:#aaa;margin-top:4px"></div>
    </div>
    <div id="wifiStatus"></div>
    <div id="wifiScan" style="margin-top:10px">
      <button onclick="wifiDoScan()" id="scanBtn">Scan Networks</button>
    </div>
    <div id="wifiNetworks" style="margin-top:10px"></div>
    <div id="wifiConnect" style="display:none;margin-top:10px">
      <div style="margin-bottom:8px;color:#aaa" id="wifiSelectedSSID"></div>
      <div style="display:flex;gap:8px">
        <input type="password" id="wifiPass" placeholder="Password"
          style="flex:1;padding:10px;border-radius:8px;border:none;background:rgba(255,255,255,0.15);color:#fff;font-size:14px" maxlength="63">
        <button onclick="wifiDoConnect()" id="connectBtn" style="padding:10px 16px">Connect</button>
      </div>
    </div>
    <div id="wifiForget" style="margin-top:12px;display:none">
      <button onclick="wifiDoReset()" style="background:rgba(248,113,113,0.3)">Forget Network</button>
    </div>
  </div>

  <div class="card">
    <h2>WLED Display</h2>
    <div id="wledStatus"></div>
    <div style="margin-top:10px">
      <div class="toggle-row">
        <span>Forward Speech to WLED</span>
        <div class="toggle" id="wledToggle" onclick="toggleWled()"></div>
      </div>
      <div class="toggle-row" style="margin-top:8px">
        <span>Hologram Mode</span>
        <div class="toggle" id="hologramToggle" onclick="toggleHologram()"></div>
      </div>
    </div>
    <div style="margin-top:10px">
      <div style="display:flex;gap:8px">
        <input type="text" id="wledIP" placeholder="WLED IP (e.g. 192.168.1.100)"
          style="flex:1;padding:10px;border-radius:8px;border:none;background:rgba(255,255,255,0.15);color:#fff;font-size:14px" maxlength="15">
        <button onclick="setWledIP()" style="padding:10px 16px">Set</button>
      </div>
    </div>
    <div style="margin-top:10px">
      <button onclick="testWled()">Test</button>
    </div>
  </div>

  <div class="card">
    <h2>WLED Sprites</h2>
    <div class="grid4" id="emojiGrid"></div>
    <div id="emojiQueue" style="margin-top:12px;min-height:20px"></div>
    <div style="margin-top:10px;display:flex;gap:8px">
      <button onclick="clearWledEmoji()" style="flex:1">Clear</button>
      <button onclick="toggleWledEmoji()" id="emojiToggleBtn" style="flex:1;background:rgba(99,102,241,0.6)">Start</button>
    </div>
    <div class="slider-container" style="margin-top:12px">
      <div class="slider-label"><span>Cycle Time</span><span id="emojiCycleVal">4s</span></div>
      <input type="range" id="emojiCycle" min="1" max="10" value="4">
    </div>
  </div>

  <div class="status">Connected to VizBot &middot; vizbot.local</div>

  <script>
    const botExprNames = ["Neutral", "Happy", "Sad", "Surprised", "Sleepy", "Angry", "Love", "Dizzy", "Thinking", "Excited", "Mischief", "Dead", "Skeptical", "Worried", "Confused", "Proud", "Shy", "Annoyed", "Bliss", "Focused", "Winking", "Devious", "Shocked", "Content", "Kissing", "Nervous", "Glitching", "Sassy"];
    const botColorNames = ["White", "Cyan", "Green", "Pink", "Yellow"];
    const botBgStyles = [{n:"Black",v:0},{n:"Ambient",v:4}];
    const ambientNames = ["Plasma","Rainbow","Fire","Ocean","Matrix","Lava","Aurora","Confetti","Galaxy","Heart","Donut"];
    let curBgStyle = 4;
    let curAmbient = 0;
    let wifiSelectedSSID = '';
    let wifiPollTimer = null;

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
      const ambSec = document.getElementById('ambientSection');
      ambSec.style.display = curBgStyle === 4 ? 'block' : 'none';
      document.getElementById('ambientEffects').innerHTML = ambientNames.map((name, i) =>
        `<button class="${curAmbient === i ? 'active' : ''}" onclick="setAmbient(${i})">${name}</button>`
      ).join('');
    }

    async function api(endpoint) {
      try { return await fetch(endpoint); } catch(e) { return null; }
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
    let hiResOn = false;
    function toggleHiRes() {
      hiResOn = !hiResOn;
      document.getElementById('hiResToggle').className = 'toggle ' + (hiResOn ? 'on' : '');
      api('/bot/hires?v=' + (hiResOn ? 1 : 0));
    }
    let infoOn = false;
    function toggleInfo() {
      infoOn = !infoOn;
      document.getElementById('infoToggle').className = 'toggle ' + (infoOn ? 'on' : '');
      api('/info/toggle');
    }
    async function setLocationZip() {
      const zip = document.getElementById('weatherZip').value.trim();
      if (!zip) return;
      document.getElementById('zipBtn').textContent = '...';
      const r = await api('/info/zip?zip=' + encodeURIComponent(zip));
      document.getElementById('zipBtn').textContent = 'Set';
      if (r && r.ok) {
        const d = await r.json();
        document.getElementById('locationInfo').textContent = 'Set to ' + d.lat + ', ' + d.lon;
      } else {
        document.getElementById('locationInfo').textContent = 'Location not found';
      }
    }
    function setBotColor(i) { api('/bot/background?v=' + i); }
    function setBotBgStyle(i) { curBgStyle = i; render(); api('/bot/background?style=' + i); }
    function setAmbient(i) { curAmbient = i; render(); api('/bot/ambient?v=' + i); }

    document.getElementById('brightness').oninput = function() {
      document.getElementById('brightnessVal').textContent = this.value;
    };
    document.getElementById('brightness').onchange = function() { api('/brightness?v=' + this.value); };

    document.getElementById('volume').oninput = function() {
      document.getElementById('volumeVal').textContent = this.value;
    };
    document.getElementById('volume').onchange = function() {
      api('/bot/volume?v=' + this.value);
    };

    async function getState() {
      try {
        const r = await fetch('/state');
        const state = await r.json();
        document.getElementById('brightness').value = state.brightness;
        document.getElementById('brightnessVal').textContent = state.brightness;
        if (state.timeOverlay !== undefined) {
          botTimeOn = state.timeOverlay;
          document.getElementById('botTimeToggle').className = 'toggle ' + (botTimeOn ? 'on' : '');
        }
        if (state.hiRes !== undefined) {
          hiResOn = state.hiRes;
          document.getElementById('hiResToggle').className = 'toggle ' + (hiResOn ? 'on' : '');
        }
        if (state.sensors && state.sensors.soundVolume !== undefined) {
          document.getElementById('volume').value = state.sensors.soundVolume;
          document.getElementById('volumeVal').textContent = state.sensors.soundVolume;
        }
        if (state.ambientEffect !== undefined) {
          curAmbient = state.ambientEffect;
          render();
        }
        if (state.infoActive !== undefined) {
          infoOn = state.infoActive;
          document.getElementById('infoToggle').className = 'toggle ' + (infoOn ? 'on' : '');
        }
        if (state.weatherLat && state.weatherLon) {
          document.getElementById('locationInfo').textContent = 'Current: ' + state.weatherLat + ', ' + state.weatherLon;
        }
        if (state.device) {
          document.querySelector('.status').textContent = 'Connected to ' + state.device + ' \u00B7 ' + state.hostname;
        }
        if (state.deviceName) {
          document.getElementById('deviceNameInput').value = state.deviceName;
        }
        if (state.wledEmoji) {
          emojiQueue = state.wledEmoji.queue || [];
          emojiActive = state.wledEmoji.active || false;
          document.getElementById('emojiToggleBtn').textContent = emojiActive ? 'Stop' : 'Start';
          document.getElementById('emojiToggleBtn').style.background = emojiActive ? '#6366f1' : 'rgba(99,102,241,0.6)';
          if (state.wledEmoji.cycleTime) {
            const sec = Math.round(state.wledEmoji.cycleTime / 1000);
            document.getElementById('emojiCycle').value = sec;
            document.getElementById('emojiCycleVal').textContent = sec + 's';
          }
          renderEmojiGrid();
          renderEmojiQueue();
        }
      } catch(e) {}
    }

    async function setDeviceName() {
      const name = document.getElementById('deviceNameInput').value.trim();
      if (!name) return;
      document.getElementById('deviceNameBtn').textContent = '...';
      document.getElementById('deviceNameBtn').disabled = true;
      const r = await api('/device/name?name=' + encodeURIComponent(name));
      document.getElementById('deviceNameBtn').textContent = 'Set';
      document.getElementById('deviceNameBtn').disabled = false;
      document.getElementById('deviceNameStatus').textContent =
        (r && r.ok) ? 'Saved \u2014 restart device to apply.' : 'Error saving name.';
    }

    // WiFi provisioning UI
    function rssiIcon(rssi) {
      if (rssi > -50) return '||||';
      if (rssi > -65) return '||| ';
      if (rssi > -75) return '||  ';
      return '|   ';
    }

    async function wifiDoScan() {
      document.getElementById('scanBtn').textContent = 'Scanning...';
      document.getElementById('scanBtn').disabled = true;
      await api('/wifi/scan');
      // Poll for scan results
      wifiPollScan();
    }

    async function wifiPollScan() {
      const r = await api('/wifi/status');
      if (!r) { setTimeout(wifiPollScan, 1000); return; }
      const d = await r.json();
      if (d.state === 'scanning') {
        setTimeout(wifiPollScan, 500);
        return;
      }
      document.getElementById('scanBtn').textContent = 'Scan Networks';
      document.getElementById('scanBtn').disabled = false;
      if (d.state === 'scan_done' && d.networks) {
        let html = '';
        d.networks.forEach(n => {
          html += '<button style="display:block;width:100%;text-align:left;margin-bottom:6px;padding:10px 12px" onclick="wifiSelectNet(\'' +
            n.ssid.replace(/'/g, "\\'") + '\',' + (n.open?'true':'false') + ')">' +
            '<span style="font-family:monospace;margin-right:8px;font-size:11px">' + rssiIcon(n.rssi) + '</span>' +
            n.ssid + (n.open ? ' <span style="color:#4ade80;font-size:11px">OPEN</span>' : '') +
            '</button>';
        });
        document.getElementById('wifiNetworks').innerHTML = html;
      }
    }

    function wifiSelectNet(ssid, isOpen) {
      wifiSelectedSSID = ssid;
      document.getElementById('wifiSelectedSSID').textContent = 'Network: ' + ssid;
      document.getElementById('wifiConnect').style.display = 'block';
      if (isOpen) {
        document.getElementById('wifiPass').value = '';
        document.getElementById('wifiPass').placeholder = 'No password needed';
      } else {
        document.getElementById('wifiPass').placeholder = 'Password';
      }
    }

    async function wifiDoConnect() {
      const pass = document.getElementById('wifiPass').value;
      document.getElementById('connectBtn').textContent = 'Connecting...';
      document.getElementById('connectBtn').disabled = true;
      await api('/wifi/connect?ssid=' + encodeURIComponent(wifiSelectedSSID) + '&pass=' + encodeURIComponent(pass));
      // Start polling for connection status
      wifiStartStatusPoll();
    }

    function wifiStartStatusPoll() {
      if (wifiPollTimer) clearInterval(wifiPollTimer);
      wifiPollTimer = setInterval(wifiCheckStatus, 2000);
    }

    async function wifiCheckStatus() {
      const r = await api('/wifi/status');
      if (!r) return;
      const d = await r.json();
      const el = document.getElementById('wifiStatus');
      if (d.state === 'connecting') {
        el.innerHTML = '<div style="color:#facc15;padding:10px">Connecting to ' + (d.ssid||'') + '...</div>';
      } else if (d.state === 'connected' || d.state === 'sta_active') {
        clearInterval(wifiPollTimer);
        el.innerHTML = '<div style="color:#4ade80;padding:10px">Connected to ' + (d.ssid||'') +
          '<br>IP: <strong>' + (d.ip||'') + '</strong>' +
          '<br><span style="color:#aaa;font-size:12px">Switch to your home WiFi and visit ' + (d.ip||'') + '</span></div>';
        document.getElementById('connectBtn').textContent = 'Connect';
        document.getElementById('connectBtn').disabled = false;
        document.getElementById('wifiConnect').style.display = 'none';
        document.getElementById('wifiNetworks').innerHTML = '';
        document.getElementById('wifiForget').style.display = 'block';
      } else if (d.state === 'failed') {
        clearInterval(wifiPollTimer);
        el.innerHTML = '<div style="color:#f87171;padding:10px">Failed: ' + (d.reason||'Unknown error') + '</div>';
        document.getElementById('connectBtn').textContent = 'Connect';
        document.getElementById('connectBtn').disabled = false;
      }
    }

    async function wifiDoReset() {
      await api('/wifi/reset');
      document.getElementById('wifiStatus').innerHTML = '<div style="color:#aaa;padding:10px">Credentials cleared. Back to AP mode.</div>';
      document.getElementById('wifiForget').style.display = 'none';
    }

    // On load, check WiFi status
    async function wifiInitCheck() {
      const r = await api('/wifi/status');
      if (!r) return;
      const d = await r.json();
      if (d.state === 'connected' || d.state === 'sta_active') {
        document.getElementById('wifiStatus').innerHTML = '<div style="color:#4ade80;padding:10px">Connected to ' +
          (d.ssid||'') + ' &middot; IP: ' + (d.ip||'') + '</div>';
        document.getElementById('wifiForget').style.display = 'block';
      } else if (d.state === 'connecting') {
        wifiStartStatusPoll();
      }
    }

    // WLED display controls
    let wledOn = false;
    let hologramOn = false;
    function toggleWled() {
      wledOn = !wledOn;
      document.getElementById('wledToggle').className = 'toggle ' + (wledOn ? 'on' : '');
      api('/wled/config?on=' + (wledOn ? 1 : 0));
    }
    function toggleHologram() {
      hologramOn = !hologramOn;
      document.getElementById('hologramToggle').className = 'toggle ' + (hologramOn ? 'on' : '');
      api('/wled/config?hologram=' + (hologramOn ? 1 : 0));
    }
    function setWledIP() {
      const ip = document.getElementById('wledIP').value.trim();
      if (ip) {
        api('/wled/config?ip=' + encodeURIComponent(ip));
        wledUpdateStatus();
      }
    }
    function testWled() { api('/wled/test'); }
    async function wledUpdateStatus() {
      const r = await api('/wled/status');
      if (!r) return;
      const d = await r.json();
      wledOn = d.enabled;
      document.getElementById('wledToggle').className = 'toggle ' + (wledOn ? 'on' : '');
      hologramOn = !!d.hologram;
      document.getElementById('hologramToggle').className = 'toggle ' + (hologramOn ? 'on' : '');
      if (d.ip) document.getElementById('wledIP').value = d.ip;
      const el = document.getElementById('wledStatus');
      if (d.ip && d.enabled) {
        el.innerHTML = '<div style="color:' + (d.reachable ? '#4ade80' : '#f87171') +
          ';padding:4px;font-size:12px">' + (d.reachable ? 'Reachable' : 'Unreachable') + ' (' + d.ip + ')</div>';
      } else {
        el.innerHTML = '';
      }
    }

    // WLED Emoji sprite controls
    const emojiNames=["Heart","Star","Check","X","Fire","Potion","Sword","Shield","ArrowUp","ArrowDn","ArrowL","ArrowR","Skull","Ghost","Alien","Pacman","PacGhost","ShyGuy","Music","WiFi","Rainbow","Mushroom","Skelly","Chicken","Invader","Dragon","TwnklHrt","Popsicle"];
    let emojiQueue=[];
    let emojiActive=false;

    function renderEmojiGrid() {
      document.getElementById('emojiGrid').innerHTML = emojiNames.map((n,i) => {
        const inQ = emojiQueue.indexOf(i) >= 0;
        return `<button class="${inQ?'active':''}" onclick="addWledEmoji(${i})" style="font-size:11px;padding:10px 4px">${n}</button>`;
      }).join('');
    }

    function renderEmojiQueue() {
      const el = document.getElementById('emojiQueue');
      if (emojiQueue.length === 0) {
        el.innerHTML = '<div style="color:#666;font-size:12px">No sprites selected</div>';
        return;
      }
      el.innerHTML = emojiQueue.map((idx,pos) =>
        `<span onclick="removeWledEmoji(${pos})" style="display:inline-block;background:rgba(99,102,241,0.4);border-radius:8px;padding:4px 10px;margin:2px 4px 2px 0;font-size:12px;cursor:pointer">${emojiNames[idx]} &times;</span>`
      ).join('');
    }

    function addWledEmoji(i) {
      if (emojiQueue.indexOf(i) < 0) emojiQueue.push(i);
      api('/wled/emoji/add?v=' + i);
      renderEmojiGrid();
      renderEmojiQueue();
    }

    function removeWledEmoji(pos) {
      emojiQueue.splice(pos, 1);
      api('/wled/emoji/remove?v=' + pos);
      renderEmojiGrid();
      renderEmojiQueue();
    }

    function clearWledEmoji() {
      emojiQueue = [];
      api('/wled/emoji/clear');
      renderEmojiGrid();
      renderEmojiQueue();
    }

    function toggleWledEmoji() {
      emojiActive = !emojiActive;
      api('/wled/emoji/toggle');
      document.getElementById('emojiToggleBtn').textContent = emojiActive ? 'Stop' : 'Start';
      document.getElementById('emojiToggleBtn').className = emojiActive ? 'active' : '';
      document.getElementById('emojiToggleBtn').style.background = emojiActive ? '#6366f1' : 'rgba(99,102,241,0.6)';
    }

    document.getElementById('emojiCycle').oninput = function() {
      document.getElementById('emojiCycleVal').textContent = this.value + 's';
    };
    document.getElementById('emojiCycle').onchange = function() {
      api('/wled/emoji/settings?cycle=' + (this.value * 1000));
    };

    function initEmoji() { renderEmojiGrid(); renderEmojiQueue(); }

    getState();
    render();
    wifiInitCheck();
    wledUpdateStatus();
    initEmoji();
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

extern bool isBotTimeOverlayEnabled();
extern bool hiResMode;
extern String getWledStatusJson();
extern struct InfoModeData infoMode;
extern char weatherLat[12];
extern char weatherLon[12];

// Cloud status accessors (defined in cloud_client.h when CLOUD_ENABLED)
#ifdef CLOUD_ENABLED
extern const char* getCloudStateStr();
extern CloudMeta cloudMeta;
#endif

void handleState() {
  String json = "{\"brightness\":" + String(brightness) +
                ",\"speed\":" + String(speed) +
                ",\"autoCycle\":" + (autoCycle ? "true" : "false") +
                ",\"timeOverlay\":" + (isBotTimeOverlayEnabled() ? "true" : "false") +
                ",\"hiRes\":" + (hiResMode ? "true" : "false") +
                ",\"ambientEffect\":" + String(effectIndex) +
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
                  ",\"freeHeap\":" + String(ESP.getFreeHeap()) +
                  ",\"maxBlock\":" + String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)) +
                  ",\"psram\":" + (sysStatus.psramAvailable ? "true" : "false") +
                  (sysStatus.psramAvailable ? ",\"psramTotal\":" + String(ESP.getPsramSize()) +
                                              ",\"psramFree\":" + String(ESP.getFreePsram()) : "") +
                  ",\"sta\":" + (sysStatus.staConnected ? "true" : "false") +
                  (sysStatus.staConnected ? ",\"staIP\":\"" + sysStatus.staIP.toString() + "\"" : "") +
                "},\"wled\":" + getWledStatusJson() +
                ",\"wledEmoji\":" + getWledEmojiJson() +
                ",\"infoActive\":" + (infoMode.active ? "true" : "false") +
                ",\"weatherLat\":\"" + String(weatherLat) + "\"" +
                ",\"weatherLon\":\"" + String(weatherLon) + "\"" +
                ",\"device\":\"" + String(apSSID) + "\"" +
                ",\"hostname\":\"" + String(mdnsHostname) + ".local\"" +
                ",\"deviceName\":\"" + String(apSSID) + "\"" +
#ifdef TARGET_CORES3
                ",\"sensors\":{" +
                  "\"speaker\":" + (sysStatus.speakerReady ? "true" : "false") +
                  ",\"mic\":" + (sysStatus.micReady ? "true" : "false") +
                  ",\"proxLight\":" + (sysStatus.proxLightReady ? "true" : "false") +
                  ",\"soundEnabled\":" + (botSounds.enabled ? "true" : "false") +
                  ",\"soundVolume\":" + String(botSounds.volume) +
                  ",\"micEnabled\":" + (audioAnalysis.enabled ? "true" : "false") +
                  ",\"proximity\":" + String(proxLight.rawProximity) +
                  ",\"lux\":" + String(proxLight.ambientLux) +
                "}" +
#endif
#ifdef CLOUD_ENABLED
                ",\"cloud\":{" +
                  "\"state\":\"" + String(getCloudStateStr()) + "\"" +
                  ",\"botId\":\"" + String(cloudMeta.botId) + "\"" +
                  ",\"contentVersion\":" + String(cloudMeta.contentVersion) +
                  ",\"pollInterval\":" + String(cloudMeta.pollIntervalSec) +
                  ",\"registered\":" + (sysStatus.cloudRegistered ? "true" : "false") +
                  ",\"littlefs\":" + (sysStatus.littlefsReady ? "true" : "false") +
                "}" +
#endif
                "}";
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
extern void cmdSetAmbientEffect(uint8_t val);
extern void cmdPlaySound(uint16_t freq, uint16_t duration);
extern void cmdSetVolume(uint8_t vol);

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

extern void cmdSetHiResMode(bool enabled);

void handleBotHiRes() {
  if (server.hasArg("v")) {
    cmdSetHiResMode(server.arg("v").toInt() == 1);
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

void handleBotAmbient() {
  if (server.hasArg("v")) {
    uint8_t val = constrain(server.arg("v").toInt(), 0, NUM_AMBIENT_EFFECTS - 1);
    cmdSetAmbientEffect(val);
  }
  server.send(200, "text/plain", "OK");
}

// ============================================================================
// Info Mode Handlers
// ============================================================================

extern void cmdToggleInfoMode();
extern void requestWeatherFetch();

void handleInfoToggle() {
  cmdToggleInfoMode();
  server.send(200, "text/plain", "OK");
}

void handleInfoLocation() {
  if (server.hasArg("lat") && server.hasArg("lon")) {
    strncpy(weatherLat, server.arg("lat").c_str(), sizeof(weatherLat) - 1);
    weatherLat[sizeof(weatherLat) - 1] = '\0';
    strncpy(weatherLon, server.arg("lon").c_str(), sizeof(weatherLon) - 1);
    weatherLon[sizeof(weatherLon) - 1] = '\0';
    markSettingsDirty();
    // Re-fetch weather with new location
    requestWeatherFetch();
    DBGLN("Location updated: " + String(weatherLat) + ", " + String(weatherLon));
  }
  server.send(200, "text/plain", "OK");
}

void handleInfoZip() {
  if (!server.hasArg("zip")) {
    server.send(400, "text/plain", "Missing zip");
    return;
  }
  String zip = server.arg("zip");

  // Use Open-Meteo geocoding API to resolve zip to lat/lon
  WiFiClient client;
  client.setTimeout(5000);
  if (!client.connect("geocoding-api.open-meteo.com", 80)) {
    server.send(500, "text/plain", "Geocode connect failed");
    return;
  }

  String path = "GET /v1/search?name=" + zip + "&count=1&language=en&format=json HTTP/1.1";
  client.println(path);
  client.println("Host: geocoding-api.open-meteo.com");
  client.println("Connection: close");
  client.println();

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 6000) {
      client.stop();
      server.send(500, "text/plain", "Geocode timeout");
      return;
    }
    delay(10);
  }

  // Read response
  String response = "";
  while (client.available()) {
    response += (char)client.read();
  }
  client.stop();

  // Find body after headers
  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart < 0) {
    server.send(500, "text/plain", "Bad geocode response");
    return;
  }
  String body = response.substring(bodyStart + 4);

  // Extract latitude and longitude from JSON
  int latIdx = body.indexOf("\"latitude\":");
  int lonIdx = body.indexOf("\"longitude\":");
  if (latIdx < 0 || lonIdx < 0) {
    server.send(404, "text/plain", "Location not found");
    return;
  }

  // Parse lat
  int latStart = latIdx + 11;
  int latEnd = body.indexOf(',', latStart);
  String latStr = body.substring(latStart, latEnd);
  latStr.trim();

  // Parse lon
  int lonStart = lonIdx + 12;
  int lonEnd = body.indexOf(',', lonStart);
  if (lonEnd < 0) lonEnd = body.indexOf('}', lonStart);
  String lonStr = body.substring(lonStart, lonEnd);
  lonStr.trim();

  strncpy(weatherLat, latStr.c_str(), sizeof(weatherLat) - 1);
  weatherLat[sizeof(weatherLat) - 1] = '\0';
  strncpy(weatherLon, lonStr.c_str(), sizeof(weatherLon) - 1);
  weatherLon[sizeof(weatherLon) - 1] = '\0';
  markSettingsDirty();
  requestWeatherFetch();

  String result = "{\"lat\":\"" + String(weatherLat) + "\",\"lon\":\"" + String(weatherLon) + "\"}";
  server.send(200, "application/json", result);
  DBGLN("Zip lookup: " + zip + " -> " + String(weatherLat) + ", " + String(weatherLon));
}

// ============================================================================
// WiFi Provisioning Handlers
// ============================================================================
// Forward declarations from wifi_provisioning.h
extern void startWifiScan();
extern void requestWifiConnect(const char* ssid, const char* pass);
extern void resetWifiProvisioning();
extern String getWifiStatusJson();

void handleWifiScan() {
  startWifiScan();
  server.send(200, "text/plain", "OK");
}

void handleWifiConnect() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Missing ssid");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";

  // Just save creds and set flag — main loop will do the actual WiFi calls.
  // This avoids calling WiFi.mode/begin from inside a Core 0 handler.
  requestWifiConnect(ssid.c_str(), pass.c_str());

  server.send(200, "text/plain", "OK");
}

void handleWifiStatus() {
  server.send(200, "application/json", getWifiStatusJson());
}

void handleWifiReset() {
  resetWifiProvisioning();
  server.send(200, "text/plain", "OK");
}

void handleSetDeviceName() {
  if (server.hasArg("name")) {
    String name = server.arg("name");
    name.trim();
    if (name.length() > 0 && name.length() < DEVICE_NAME_MAX) {
      saveDeviceName(name.c_str());
      server.send(200, "text/plain", "Saved. Restart device to apply new name.");
      return;
    }
    if (name.length() == 0) {
      // Empty name clears the custom name — MAC suffix will be used on next boot
      saveDeviceName("");
      server.send(200, "text/plain", "Cleared. MAC suffix will be used on next boot.");
      return;
    }
  }
  server.send(400, "text/plain", "Invalid name");
}

// ============================================================================
// WLED Display Handlers
// ============================================================================
// Forward declarations from wled_display.h
extern void wledSetIP(const char* ip);
extern void wledSetEnabled(bool on);
extern void wledSetColor(uint8_t r, uint8_t g, uint8_t b);
extern void wledSetSpeed(uint8_t spd);
extern void wledSetIx(uint8_t ix);
extern String getWledStatusJson();
extern void wledQueueText(const char* text, uint16_t durationMs);
extern void wledSetHologram(bool on);

void handleWledStatus() {
  server.send(200, "application/json", getWledStatusJson());
}

void handleWledConfig() {
  if (server.hasArg("ip")) {
    wledSetIP(server.arg("ip").c_str());
  }
  if (server.hasArg("on")) {
    wledSetEnabled(server.arg("on").toInt() == 1);
  }
  if (server.hasArg("speed")) {
    wledSetSpeed(constrain(server.arg("speed").toInt(), 0, 255));
  }
  if (server.hasArg("ix")) {
    wledSetIx(constrain(server.arg("ix").toInt(), 0, 255));
  }
  if (server.hasArg("hologram")) {
    wledSetHologram(server.arg("hologram").toInt() == 1);
  }
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    wledSetColor(
      constrain(server.arg("r").toInt(), 0, 255),
      constrain(server.arg("g").toInt(), 0, 255),
      constrain(server.arg("b").toInt(), 0, 255));
  }
  server.send(200, "text/plain", "OK");
}

void handleWledTest() {
  wledQueueText("Hello", 5000);
  server.send(200, "text/plain", "OK");
}

// WLED Emoji handlers (defined in wled_emoji.h, included via wled_display.h)

void handleWledEmojiAdd() {
  if (server.hasArg("v")) {
    uint8_t idx = constrain(server.arg("v").toInt(), 0, ICON_COUNT - 1);
    wledEmojiAdd(idx);
  }
  server.send(200, "text/plain", "OK");
}

void handleWledEmojiRemove() {
  if (server.hasArg("v")) {
    uint8_t pos = server.arg("v").toInt();
    wledEmojiRemove(pos);
  }
  server.send(200, "text/plain", "OK");
}

void handleWledEmojiClear() {
  wledEmojiClear();
  if (wledEmoji.active) wledEmojiStop();
  server.send(200, "text/plain", "OK");
}

void handleWledEmojiToggle() {
  if (wledEmoji.active) {
    wledEmojiStop();
  } else {
    wledEmojiStart();
  }
  server.send(200, "text/plain", "OK");
}

void handleWledEmojiSettings() {
  if (server.hasArg("cycle")) {
    wledEmoji.cycleTimeMs = constrain(server.arg("cycle").toInt(), 1000, 10000);
  }
  if (server.hasArg("fade")) {
    wledEmoji.fadeTimeMs = constrain(server.arg("fade").toInt(), 200, 2000);
  }
  server.send(200, "text/plain", "OK");
}

// ============================================================================
// Cloud Endpoints
// ============================================================================
#ifdef CLOUD_ENABLED
extern bool cloudRegister();

void handleCloudStatus() {
  String json = "{\"state\":\"" + String(getCloudStateStr()) + "\"" +
                ",\"botId\":\"" + String(cloudMeta.botId) + "\"" +
                ",\"contentVersion\":" + String(cloudMeta.contentVersion) +
                ",\"pollInterval\":" + String(cloudMeta.pollIntervalSec) +
                ",\"registered\":" + (sysStatus.cloudRegistered ? "true" : "false") +
                ",\"littlefs\":" + (sysStatus.littlefsReady ? "true" : "false") +
                "}";
  server.send(200, "application/json", json);
}

void handleCloudSync() {
  // Manual sync trigger — re-register if not yet registered
  if (!cloudMeta.registered) {
    bool ok = cloudRegister();
    sysStatus.cloudRegistered = ok;
    server.send(200, "text/plain", ok ? "Registered" : "Failed");
  } else {
    server.send(200, "text/plain", "OK — sync happens on next poll");
  }
}
#endif

// ============================================================================
// Core S3 Sensor Endpoints — Sound & Mic
// ============================================================================
#ifdef TARGET_CORES3
extern struct BotSounds botSounds;
extern struct AudioAnalysis audioAnalysis;
extern struct ProxLightState proxLight;

void handleBotSound() {
  if (server.hasArg("freq") && server.hasArg("dur")) {
    uint16_t freq = constrain(server.arg("freq").toInt(), 100, 8000);
    uint16_t dur = constrain(server.arg("dur").toInt(), 10, 5000);
    cmdPlaySound(freq, dur);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotVolume() {
  if (server.hasArg("v")) {
    uint8_t vol = constrain(server.arg("v").toInt(), 0, 255);
    cmdSetVolume(vol);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotMic() {
  String json = "{\"rms\":" + String(audioAnalysis.rmsLevel, 1) +
                ",\"smooth\":" + String(audioAnalysis.smoothLevel, 1) +
                ",\"peak\":" + String(audioAnalysis.peakLevel, 1) +
                ",\"normalized\":" + String(audioAnalysis.getNormalizedLevel(), 3) +
                ",\"spike\":" + (audioAnalysis.spikeDetected ? "true" : "false") +
                ",\"speech\":" + (audioAnalysis.speechDetected ? "true" : "false") +
                ",\"enabled\":" + (audioAnalysis.enabled ? "true" : "false") +
                "}";
  server.send(200, "application/json", json);
}
#endif

// ============================================================================
// Captive Portal — redirect OS connectivity checks to control page
// ============================================================================
// When a phone/laptop connects to vizBot WiFi, the OS sends a connectivity
// check (e.g., http://captive.apple.com/hotspot-detect.html). DNS resolves
// ALL domains to 192.168.4.1 (us). We return a 302 redirect so the OS
// detects "captive portal" and auto-opens the control page.

void handleCaptiveRedirect() {
  // In STA-only mode, no captive portal — just serve root
  String ip = sysStatus.staConnected ? sysStatus.staIP.toString() : WiFi.softAPIP().toString();
  server.sendHeader("Location", String("http://") + ip + "/");
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
  server.on("/bot/hires", handleBotHiRes);
  server.on("/bot/background", handleBotBackground);
  server.on("/bot/ambient", handleBotAmbient);

  // Info mode endpoints
  server.on("/info/toggle", handleInfoToggle);
  server.on("/info/location", handleInfoLocation);
  server.on("/info/zip", handleInfoZip);

  // WiFi provisioning endpoints
  server.on("/wifi/scan", handleWifiScan);
  server.on("/wifi/connect", handleWifiConnect);
  server.on("/wifi/status", handleWifiStatus);
  server.on("/wifi/reset", handleWifiReset);
  server.on("/device/name", handleSetDeviceName);

  // WLED display endpoints
  server.on("/wled/status", handleWledStatus);
  server.on("/wled/config", handleWledConfig);
  server.on("/wled/test", handleWledTest);

  // WLED emoji display endpoints
  server.on("/wled/emoji/add", handleWledEmojiAdd);
  server.on("/wled/emoji/remove", handleWledEmojiRemove);
  server.on("/wled/emoji/clear", handleWledEmojiClear);
  server.on("/wled/emoji/toggle", handleWledEmojiToggle);
  server.on("/wled/emoji/settings", handleWledEmojiSettings);

  // Core S3 sensor endpoints
  #ifdef TARGET_CORES3
  server.on("/bot/sound", handleBotSound);
  server.on("/bot/volume", handleBotVolume);
  server.on("/bot/mic", handleBotMic);
  #endif

  // Cloud endpoints
  #ifdef CLOUD_ENABLED
  server.on("/cloud/status", handleCloudStatus);
  server.on("/cloud/sync", handleCloudSync);
  #endif

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

// Start mDNS with the per-device unique hostname (e.g. vizbot-a3f2.local)
bool startMDNS() {
  bool ok = MDNS.begin(mdnsHostname);
  if (ok) {
    MDNS.addService("http", "tcp", 80);
    DBG("mDNS started: ");
    DBG(mdnsHostname);
    DBGLN(".local");
  } else {
    DBGLN("mDNS failed to start");
  }
  return ok;
}

#endif
