#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include "config.h"
#include "palettes.h"
#include "ota_update.h"

// External references to globals
extern WebServer server;
extern DNSServer dnsServer;
extern uint8_t effectIndex;
extern uint8_t paletteIndex;
extern uint8_t brightness;
extern uint8_t speed;
extern bool autoCycle;
extern unsigned long lastChange;
extern void resetEffectShuffle();
extern uint8_t currentMode;
extern CRGBPalette16 currentPalette;

// System status (populated by boot_sequence.h)
#include "system_status.h"
#ifdef BOARD_HAS_STACKCHAN_BASE
#include "stackchan_base.h"
#include "stackchan_leds.h"
#include "stackchan_touch.h"
#endif

// Web interface HTML
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>VizBot</title>
  <style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;background:#e8e4dc;color:#000;min-height:100vh}
.hdr{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:#FFD23F;border:3px solid #000;box-shadow:0 5px 0 0 #000}
.logo{font-size:20px;font-weight:800;color:#000;letter-spacing:-0.5px}
.dev-name{color:#333;font-size:12px;margin-left:10px;font-weight:600}
.hdr-r{display:flex;align-items:center;gap:6px}
.dot{width:10px;height:10px;border-radius:50%;border:2px solid #000;background:#ddd;display:inline-block}
.dot.on{background:#88D498}
.dot.err{background:#FF6B6B}
.sl{color:#333;font-size:11px;margin-right:8px;font-weight:600}
.layout{display:grid;grid-template-columns:1fr;gap:12px;padding:16px;max-width:1400px;margin:0 auto}
@media(min-width:768px){.layout{grid-template-columns:1fr 1fr;padding:20px;gap:14px}}
@media(min-width:1200px){.layout{grid-template-columns:3fr 2fr;padding:24px}}
.card{background:#fffdf5;border:3px solid #000;box-shadow:5px 5px 0 0 #000;padding:16px;margin-bottom:0}
h2{font-size:13px;color:#000;text-transform:uppercase;letter-spacing:0.08em;margin-bottom:12px;font-weight:800}
.shdr{cursor:pointer;user-select:none;display:flex;justify-content:space-between;align-items:center;margin-bottom:0;padding:0}
.shdr:not(.shut){margin-bottom:12px}
.shdr:hover{color:#555}
.chv{font-size:10px;transition:transform .2s;display:inline-block}
.shdr.shut .chv{transform:rotate(-90deg)}
.sbody{overflow:hidden;transition:max-height .3s ease;max-height:2000px}
.sbody.shut{max-height:0}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
.grid5{display:grid;grid-template-columns:repeat(5,1fr);gap:6px}
@media(max-width:767px){.grid5{grid-template-columns:repeat(3,1fr)}}
button{background:#e8e8e8;border:2px solid #000;box-shadow:3px 3px 0 0 #000;color:#000;padding:10px 8px;border-radius:0;font-size:12px;font-weight:600;cursor:pointer;transition:transform .1s,box-shadow .1s}
button:hover{transform:translate(-1px,-1px);box-shadow:4px 4px 0 0 #000}
button:active{transform:translate(2px,2px);box-shadow:none}
button.active{background:#FFD23F;border-color:#000}
.btn-send{background:#FFA552;border-color:#000;font-weight:700}
.btn-send:hover{background:#ffb76b}
.btn-full{width:100%}
.btn-danger{background:#fdd;border-color:#000;color:#c00;box-shadow:3px 3px 0 0 #c00}
.btn-danger:hover{background:#fcc;transform:translate(-1px,-1px);box-shadow:4px 4px 0 0 #c00}
.btn-danger:active{transform:translate(2px,2px);box-shadow:none}
.btn-start{background:#B8A9FA;border-color:#000}
.btn-start.on{background:#FFD23F}
.inp,.sel{width:100%;padding:10px 12px;border:2px solid #000;box-shadow:3px 3px 0 0 #000;background:#fff;color:#000;font-size:14px;border-radius:0;font-family:inherit}
.inp:focus,.sel:focus{outline:none;box-shadow:3px 3px 0 0 #74B9FF}
.inp-sm{width:56px;padding:6px 8px;border:2px solid #000;box-shadow:2px 2px 0 0 #000;background:#fff;color:#000;font-size:13px;text-align:center;border-radius:0}
.flex1{flex:1}
.row{display:flex;gap:8px;align-items:center}
.srow{display:flex;justify-content:space-between;margin-bottom:4px;font-size:13px;color:#555;font-weight:600}
input[type=range]{width:100%;height:6px;background:#ddd;border:1px solid #000;-webkit-appearance:none;border-radius:0;margin-bottom:12px}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;background:#B8A9FA;border:2px solid #000;box-shadow:2px 2px 0 0 #000;cursor:pointer;border-radius:0}
.trow{display:flex;justify-content:space-between;align-items:center;padding:8px 0;font-size:14px;font-weight:500}
.tog{width:44px;height:24px;background:#ddd;border:2px solid #000;box-shadow:2px 2px 0 0 #000;position:relative;cursor:pointer;transition:background .2s;border-radius:0}
.tog.on{background:#B8A9FA}
.tog::after{content:'';position:absolute;width:16px;height:16px;background:#fff;border:2px solid #000;top:2px;left:2px;transition:transform .2s;border-radius:0}
.tog.on::after{transform:translateX(20px)}
.lbl{font-size:13px;color:#555;font-weight:600}
.hint{font-size:12px;color:#888;margin-top:4px}
.status{text-align:center;color:#555;font-size:11px;padding:12px;font-weight:700;letter-spacing:0.05em;text-transform:uppercase;border-top:3px solid #000;background:#fffdf5}
  </style>
</head>
<body>
  <header class="hdr">
    <div style="display:flex;align-items:center">
      <span class="logo">VizBot</span>
      <span class="dev-name" id="hdrVer"></span>
      <span class="dev-name" id="deviceLabel"></span>
    </div>
    <div class="hdr-r">
      <span class="dot on" id="connDot"></span><span class="sl">Connected</span>
      <span class="dot" id="wledDot"></span><span class="sl">WLED</span>
    </div>
  </header>

  <div class="layout">
    <div class="col-main">
      <div class="card">
        <h2>Expressions</h2>
        <div class="grid5" id="botExpressions"></div>
      </div>

      <div class="card">
        <h2>Say Something</h2>
        <div class="row">
          <input type="text" id="botSayInput" placeholder="Type a message..." maxlength="60" class="inp flex1">
          <button onclick="sendBotSay()" class="btn-send">Send</button>
        </div>
      </div>

      <div class="card">
        <h2 class="shdr" onclick="tgl('secPers')">Personality <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secPers">
          <select id="personalitySelect" onchange="setPersonality(this.value)" class="sel"></select>
          <div class="row" style="margin-top:8px">
            <span class="lbl">Rotate:</span>
            <input type="checkbox" id="rotateCheck" onchange="toggleRotation()">
            <span class="lbl">every</span>
            <input type="number" id="rotateMin" value="5" min="1" max="60" class="inp-sm">
            <span class="lbl">min</span>
          </div>
        </div>
      </div>

      <div class="card">
        <h2 class="shdr" onclick="tgl('secAppear')">Appearance <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secAppear">
          <span class="lbl">Face Color</span>
          <div class="grid5" id="botColors" style="margin-top:6px"></div>
          <span class="lbl" style="display:block;margin-top:12px">Background</span>
          <div class="row" id="botBgStyles" style="margin-top:6px;gap:6px"></div>
          <div id="ambientSection" style="display:none;margin-top:12px">
            <div style="display:flex;justify-content:space-between;align-items:center">
              <span class="lbl">Ambient Effect</span>
              <button id="autoCycleBtn" onclick="toggleAutoCycle()" style="font-size:11px;padding:3px 10px"></button>
            </div>
            <div class="grid3" id="ambientEffects" style="margin-top:6px"></div>
          </div>
        </div>
      </div>

      <div class="card">
        <h2 class="shdr" onclick="tgl('secSprites')">WLED Sprites <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secSprites">
          <div class="grid4" id="emojiGrid"></div>
          <div id="emojiQueue" style="margin-top:8px;min-height:20px"></div>
          <div class="row" style="margin-top:8px">
            <button onclick="clearWledEmoji()" class="flex1">Clear</button>
            <button onclick="toggleWledEmoji()" id="emojiToggleBtn" class="btn-start flex1">Start</button>
          </div>
          <div class="srow" style="margin-top:8px"><span>Cycle Time</span><span id="emojiCycleVal">4s</span></div>
          <input type="range" id="emojiCycle" min="1" max="10" value="4">
        </div>
      </div>
    </div>

    <div class="col-side">

      <div class="card">
        <h2 class="shdr" onclick="tgl('secDev')">Device <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secDev">
          <div class="srow"><span>Brightness</span><span id="brightnessVal">15</span></div>
          <input type="range" id="brightness" min="1" max="255" value="15">
          <div class="srow"><span>Volume</span><span id="volumeVal">120</span></div>
          <input type="range" id="volume" min="0" max="255" value="120">
          <div class="trow"><span>Time Overlay</span><div class="tog" id="botTimeToggle" onclick="toggleBotTime()"></div></div>
          <div class="trow"><span>Hi-Res Background</span><div class="tog" id="hiResToggle" onclick="toggleHiRes()"></div></div>
          <div style="border-top:1px solid #eee;margin-top:10px;padding-top:10px">
            <div class="trow"><span>Firmware <span id="fwVer" style="font-weight:700"></span></span><a href="/update" style="display:inline-block;padding:5px 12px;background:#FFD23F;border:2px solid #000;box-shadow:2px 2px 0 0 #000;font-weight:700;font-size:11px;text-transform:uppercase;color:#000;text-decoration:none">Update</a></div>
          </div>
        </div>
      </div>

      <div class="card">
        <h2 class="shdr" onclick="tgl('secSounds')">Sounds <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secSounds">
          <div class="trow"><span>MIDI Synth</span><span id="midiStatus" style="font-size:12px;font-weight:700;color:#888">---</span></div>
          <div class="grid4" id="soundGrid" style="margin-top:8px"></div>
        </div>
      </div>

      <div class="card">
        <h2 class="shdr" onclick="tgl('secInfo')">Weather &amp; Info <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secInfo">
          <div class="trow"><span>Show Weather</span><div class="tog" id="infoToggle" onclick="toggleInfo()"></div></div>
          <span class="lbl">Location</span>
          <div class="row" style="margin-top:4px">
            <input type="text" id="weatherZip" placeholder="Zip or city" class="inp flex1" maxlength="30">
            <button onclick="setLocationZip()" id="zipBtn">Set</button>
          </div>
          <div id="locationInfo" class="hint"></div>
          <div class="trow" style="margin-top:10px"><span>Scheduled Content</span><div class="tog" id="schedToggle" onclick="toggleSched()"></div></div>
          <div class="row">
            <span class="lbl">Cycle every</span>
            <input type="number" id="schedMin" value="30" min="1" max="120" class="inp-sm" onchange="updateSched()">
            <span class="lbl">min</span>
          </div>
          <div id="schedStatus" class="hint"></div>
        </div>
      </div>

      <div class="card">
        <h2 class="shdr" onclick="tgl('secWifi')">WiFi <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secWifi">
          <span class="lbl">Device Name</span>
          <div class="row" style="margin-top:4px">
            <input type="text" id="deviceNameInput" placeholder="e.g. vizbot-desk" class="inp flex1" maxlength="23">
            <button onclick="setDeviceName()" id="deviceNameBtn">Set</button>
          </div>
          <div id="deviceNameStatus" class="hint"></div>
          <div id="wifiStatus" style="margin-top:8px"></div>
          <button onclick="wifiDoScan()" id="scanBtn" class="btn-full" style="margin-top:8px">Scan Networks</button>
          <div id="wifiNetworks" style="margin-top:8px"></div>
          <div id="wifiConnect" style="display:none;margin-top:8px">
            <div class="hint" id="wifiSelectedSSID"></div>
            <div class="row" style="margin-top:4px">
              <input type="password" id="wifiPass" placeholder="Password" class="inp flex1" maxlength="63">
              <button onclick="wifiDoConnect()" id="connectBtn">Connect</button>
            </div>
          </div>
          <div id="wifiForget" style="margin-top:8px;display:none">
            <button onclick="wifiDoReset()" class="btn-danger btn-full">Forget Network</button>
          </div>
        </div>
      </div>

      <div class="card">
        <h2 class="shdr" onclick="tgl('secWled')">WLED Display <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secWled">
          <div id="wledStatus"></div>
          <div class="trow"><span>Forward Speech</span><div class="tog" id="wledToggle" onclick="toggleWled()"></div></div>
          <div class="trow"><span>Hologram Mode</span><div class="tog" id="hologramToggle" onclick="toggleHologram()"></div></div>
          <div class="row" style="margin-top:8px">
            <input type="text" id="wledIP" placeholder="WLED IP" class="inp flex1" maxlength="15">
            <button onclick="setWledIP()">Set</button>
          </div>
          <button onclick="testWled()" class="btn-full" style="margin-top:8px">Test Connection</button>
        </div>
      </div>

      <div class="card" id="scCard" style="display:none">
        <h2 class="shdr" onclick="tgl('secSC')">StackChan <span class="chv">&#9662;</span></h2>
        <div class="sbody" id="secSC">
          <span class="lbl">Head Control</span>
          <div class="grid3" style="margin-top:6px">
            <button onclick="scPreset('left')">&#8592; Left</button>
            <button onclick="scPreset('nod')">Nod</button>
            <button onclick="scPreset('right')">Right &#8594;</button>
            <button onclick="scPreset('lookup')">&#8593; Up</button>
            <button onclick="api('/bot/head/recenter')">Center</button>
            <button onclick="scPreset('lookdown')">&#8595; Down</button>
          </div>
          <div class="row" style="margin-top:6px">
            <button onclick="scPreset('shake')" class="flex1">Shake</button>
          </div>
          <div class="srow" style="margin-top:12px"><span>Yaw</span><span id="scYawVal">0&deg;</span></div>
          <input type="range" id="scYaw" min="-900" max="900" value="0">
          <div class="srow"><span>Pitch</span><span id="scPitchVal">50&deg;</span></div>
          <input type="range" id="scPitch" min="250" max="850" value="500">
          <div style="border-top:1px solid #eee;margin-top:8px;padding-top:10px">
            <span class="lbl">Base LEDs</span>
            <select id="scLedMode" onchange="scSetLedMode(this.value)" class="sel" style="margin-top:6px"></select>
            <div class="srow" style="margin-top:8px"><span>Brightness</span><span id="scLedBrVal">80</span></div>
            <input type="range" id="scLedBr" min="10" max="255" value="80">
            <div class="srow"><span>Speed</span><span id="scLedSpVal">128</span></div>
            <input type="range" id="scLedSp" min="10" max="255" value="128">
          </div>
          <div style="border-top:1px solid #eee;margin-top:8px;padding-top:10px">
            <div class="trow"><span>Chill Mode (10 min)</span><div class="tog" id="scChillToggle" onclick="scToggleChill()"></div></div>
            <div class="hint">Hold head 2s or tap here. Stops movement.</div>
          </div>
          <div id="scBattery" style="border-top:1px solid #eee;margin-top:8px;padding-top:10px;display:none">
            <div class="srow"><span>Battery</span><span id="scBatVal">--</span></div>
          </div>
        </div>
      </div>

    </div>
  </div>

  <div class="status" id="statusBar">Connected to VizBot</div>

  <script>
    function tgl(id) {
      const b = document.getElementById(id);
      const h = b.previousElementSibling;
      b.classList.toggle('shut');
      h.classList.toggle('shut');
      const states = JSON.parse(localStorage.getItem('vb_sections') || '{}');
      states[id] = b.classList.contains('shut');
      localStorage.setItem('vb_sections', JSON.stringify(states));
    }
    (function restoreSections() {
      const states = JSON.parse(localStorage.getItem('vb_sections') || '{}');
      for (const [id, shut] of Object.entries(states)) {
        if (shut) {
          const b = document.getElementById(id);
          if (b) { b.classList.add('shut'); b.previousElementSibling.classList.add('shut'); }
        }
      }
    })();

    const botExprNames = ["Neutral","Happy","Sad","Surprised","Chill","Angry","Love","Dizzy","Thinking","Excited","Mischief","Skeptical","Worried","Confused","Proud","Shy","Annoyed","Focused","Winking","Devious","Shocked","Kissing","Nervous","Glitching","Sassy"];
    const botColorNames = ["White","Cyan","Green","Pink","Yellow"];
    const botBgStyles = [{n:"Black",v:0},{n:"Ambient",v:4}];
    const ambientNames = ["Plasma","Galaxy","Ripple","Chevrons","Stripes","Checker","Scanline","Perlin","Distorsion","ZVortex","Snakes","Sinusoid","Puzzle","Bumpmap","Xorcery","Hiphotic"];
    let curBgStyle = 4;
    let curAmbient = 0;
    let curExpr = 0;
    let curColor = 0;
    let autoCycleOn = true;
    let wifiSelectedSSID = '';
    let wifiPollTimer = null;

    function render() {
      document.getElementById('botExpressions').innerHTML = botExprNames.map((name, i) =>
        `<button class="${curExpr===i?'active':''}" onclick="setBotExpr(${i})">${name}</button>`
      ).join('');
      document.getElementById('botColors').innerHTML = botColorNames.map((name, i) =>
        `<button class="${curColor===i?'active':''}" onclick="setBotColor(${i})">${name}</button>`
      ).join('');
      document.getElementById('botBgStyles').innerHTML = botBgStyles.map(s =>
        `<button class="${curBgStyle===s.v?'active':''}" onclick="setBotBgStyle(${s.v})">${s.n}</button>`
      ).join('');
      const ambSec = document.getElementById('ambientSection');
      ambSec.style.display = curBgStyle === 4 ? 'block' : 'none';
      document.getElementById('ambientEffects').innerHTML = ambientNames.map((name, i) =>
        `<button class="${curAmbient===i?'active':''}" onclick="setAmbient(${i})">${name}</button>`
      ).join('');
      const acBtn = document.getElementById('autoCycleBtn');
      if (acBtn) acBtn.textContent = autoCycleOn ? '⏸ Stop Auto' : '▶ Auto';
    }

    async function api(endpoint) {
      try { return await fetch(endpoint); } catch(e) { return null; }
    }

    function setBotExpr(i) { curExpr=i; render(); api('/bot/expression?v=' + i); }
    function setPersonality(i) { api('/bot/personality?v=' + i); }
    function toggleRotation() {
      const on = document.getElementById('rotateCheck').checked;
      const min = parseInt(document.getElementById('rotateMin').value) || 5;
      if (on) {
        const sel = document.getElementById('personalitySelect');
        const all = [];
        for (let j = 0; j < sel.options.length; j++) all.push(parseInt(sel.options[j].value));
        fetch('/bot/personality/rotation', {
          method:'POST', headers:{'Content-Type':'application/json'},
          body: JSON.stringify({list: all, interval: min * 60000})
        });
      } else {
        const cur = document.getElementById('personalitySelect').value;
        api('/bot/personality?v=' + cur);
      }
    }
    async function loadPersonalities() {
      try {
        const r = await fetch('/bot/personality');
        const d = await r.json();
        const sel = document.getElementById('personalitySelect');
        sel.innerHTML = d.personalities.map(p =>
          '<option value="' + p.index + '"' + (p.index === d.current ? ' selected' : '') + '>'
          + p.name + (p.cloud ? ' (cloud)' : '') + '</option>').join('');
        document.getElementById('rotateCheck').checked = d.rotInterval > 0;
        if (d.rotInterval > 0) {
          document.getElementById('rotateMin').value = Math.round(d.rotInterval / 60000);
        }
      } catch(e) {}
    }
    loadPersonalities();
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
      document.getElementById('botTimeToggle').className = 'tog ' + (botTimeOn ? 'on' : '');
      api('/bot/time?v=' + (botTimeOn ? 1 : 0));
    }
    let hiResOn = false;
    function toggleHiRes() {
      hiResOn = !hiResOn;
      document.getElementById('hiResToggle').className = 'tog ' + (hiResOn ? 'on' : '');
      api('/bot/hires?v=' + (hiResOn ? 1 : 0));
    }
    let schedOn = false;
    function toggleSched() {
      schedOn = !schedOn;
      document.getElementById('schedToggle').className = 'tog ' + (schedOn ? 'on' : '');
      api('/schedule?enabled=' + (schedOn ? 1 : 0));
    }
    function updateSched() {
      const min = document.getElementById('schedMin').value;
      api('/schedule?intervalMin=' + min);
    }
    async function loadSchedule() {
      try {
        const r = await fetch('/schedule');
        const d = await r.json();
        schedOn = d.enabled;
        document.getElementById('schedToggle').className = 'tog ' + (schedOn ? 'on' : '');
        document.getElementById('schedMin').value = d.intervalMin;
        const phases = ['Idle','Weather','Gap','Emoji'];
        document.getElementById('schedStatus').textContent =
          d.isOwner ? 'Owner \u2022 Phase: ' + (phases[d.phase] || 'Idle') : (d.enabled ? 'Deferred to another bot' : '');
      } catch(e) {}
    }
    loadSchedule();

    let infoOn = false;
    function toggleInfo() {
      infoOn = !infoOn;
      document.getElementById('infoToggle').className = 'tog ' + (infoOn ? 'on' : '');
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
    function setBotColor(i) { curColor=i; render(); api('/bot/background?v=' + i); }
    function setBotBgStyle(i) { curBgStyle = i; render(); api('/bot/background?style=' + i); }
    function setAmbient(i) { curAmbient = i; autoCycleOn = false; render(); api('/bot/ambient?v=' + i + '&lock=1'); }
    function toggleAutoCycle() { autoCycleOn = !autoCycleOn; render(); api('/bot/autocycle?v=' + (autoCycleOn ? '1' : '0')); }

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
        if (state.sensors) {
          var ms = document.getElementById('midiStatus');
          if (state.sensors.useMidi) { ms.textContent = 'MIDI Active'; ms.style.color = '#88D498'; }
          else if (state.sensors.speaker) { ms.textContent = 'Speaker'; ms.style.color = '#FFA552'; }
          else { ms.textContent = 'Off'; ms.style.color = '#FF6B6B'; }
        }
        if (state.timeOverlay !== undefined) {
          botTimeOn = state.timeOverlay;
          document.getElementById('botTimeToggle').className = 'tog ' + (botTimeOn ? 'on' : '');
        }
        if (state.hiRes !== undefined) {
          hiResOn = state.hiRes;
          document.getElementById('hiResToggle').className = 'tog ' + (hiResOn ? 'on' : '');
        }
        if (state.sensors && state.sensors.soundVolume !== undefined) {
          document.getElementById('volume').value = state.sensors.soundVolume;
          document.getElementById('volumeVal').textContent = state.sensors.soundVolume;
        }
        if (state.ambientEffect !== undefined) {
          curAmbient = state.ambientEffect;
        }
        if (state.autoCycle !== undefined) {
          autoCycleOn = state.autoCycle;
        }
        if (state.infoActive !== undefined) {
          infoOn = state.infoActive;
          document.getElementById('infoToggle').className = 'tog ' + (infoOn ? 'on' : '');
        }
        if (state.weatherLat && state.weatherLon) {
          document.getElementById('locationInfo').textContent = 'Current: ' + state.weatherLat + ', ' + state.weatherLon;
        }
        if (state.firmwareVersion) {
          document.getElementById('fwVer').textContent = 'v' + state.firmwareVersion;
          document.getElementById('hdrVer').textContent = 'v' + state.firmwareVersion;
        }
        if (state.device) {
          document.getElementById('deviceLabel').textContent = state.device;
          document.getElementById('statusBar').textContent = 'Connected to ' + state.device + ' \u00B7 ' + state.hostname;
        }
        if (state.deviceName) {
          document.getElementById('deviceNameInput').value = state.deviceName;
        }
        if (state.wledEmoji) {
          emojiQueue = state.wledEmoji.queue || [];
          emojiActive = state.wledEmoji.active || false;
          document.getElementById('emojiToggleBtn').textContent = emojiActive ? 'Stop' : 'Start';
          document.getElementById('emojiToggleBtn').className = 'btn-start flex1' + (emojiActive ? ' on' : '');
          if (state.wledEmoji.cycleTime) {
            const sec = Math.round(state.wledEmoji.cycleTime / 1000);
            document.getElementById('emojiCycle').value = sec;
            document.getElementById('emojiCycleVal').textContent = sec + 's';
          }
          renderEmojiGrid();
          renderEmojiQueue();
        }
        if (state.stackchan) scUpdateFromState(state);
        render();
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
      wifiPollScan();
    }

    async function wifiPollScan() {
      const r = await api('/wifi/status');
      if (!r) { setTimeout(wifiPollScan, 1000); return; }
      const d = await r.json();
      if (d.state === 'scanning') { setTimeout(wifiPollScan, 500); return; }
      document.getElementById('scanBtn').textContent = 'Scan Networks';
      document.getElementById('scanBtn').disabled = false;
      if (d.state === 'scan_done' && d.networks) {
        let html = '';
        d.networks.forEach(n => {
          html += '<button class="btn-full" style="text-align:left;margin-bottom:6px" onclick="wifiSelectNet(\'' +
            n.ssid.replace(/'/g, "\\'") + '\',' + (n.open?'true':'false') + ')">' +
            '<span style="font-family:monospace;margin-right:8px;font-size:11px">' + rssiIcon(n.rssi) + '</span>' +
            n.ssid + (n.open ? ' <span style="color:#88D498;font-size:11px;font-weight:800">OPEN</span>' : '') +
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
        el.innerHTML = '<div style="color:#FFA552;padding:8px;font-weight:600">Connecting to ' + (d.ssid||'') + '...</div>';
      } else if (d.state === 'connected' || d.state === 'sta_active') {
        clearInterval(wifiPollTimer);
        el.innerHTML = '<div style="color:#88D498;padding:8px;font-weight:600">Connected to ' + (d.ssid||'') +
          '<br>IP: <strong>' + (d.ip||'') + '</strong>' +
          '<br><span style="color:#888;font-size:12px">Switch to your home WiFi and visit ' + (d.ip||'') + '</span></div>';
        document.getElementById('connectBtn').textContent = 'Connect';
        document.getElementById('connectBtn').disabled = false;
        document.getElementById('wifiConnect').style.display = 'none';
        document.getElementById('wifiNetworks').innerHTML = '';
        document.getElementById('wifiForget').style.display = 'block';
      } else if (d.state === 'failed') {
        clearInterval(wifiPollTimer);
        el.innerHTML = '<div style="color:#FF6B6B;padding:8px;font-weight:600">Failed: ' + (d.reason||'Unknown error') + '</div>';
        document.getElementById('connectBtn').textContent = 'Connect';
        document.getElementById('connectBtn').disabled = false;
      }
    }

    async function wifiDoReset() {
      await api('/wifi/reset');
      document.getElementById('wifiStatus').innerHTML = '<div style="color:#888;padding:8px">Credentials cleared. Back to AP mode.</div>';
      document.getElementById('wifiForget').style.display = 'none';
    }

    async function wifiInitCheck() {
      const r = await api('/wifi/status');
      if (!r) return;
      const d = await r.json();
      if (d.state === 'connected' || d.state === 'sta_active') {
        document.getElementById('wifiStatus').innerHTML = '<div style="color:#88D498;padding:8px;font-weight:600">Connected to ' +
          (d.ssid||'') + ' &middot; IP: ' + (d.ip||'') + '</div>';
        document.getElementById('wifiForget').style.display = 'block';
      } else if (d.state === 'connecting') {
        wifiStartStatusPoll();
      }
    }

    let wledOn = false;
    let hologramOn = false;
    function toggleWled() {
      wledOn = !wledOn;
      document.getElementById('wledToggle').className = 'tog ' + (wledOn ? 'on' : '');
      api('/wled/config?on=' + (wledOn ? 1 : 0));
    }
    function toggleHologram() {
      hologramOn = !hologramOn;
      document.getElementById('hologramToggle').className = 'tog ' + (hologramOn ? 'on' : '');
      api('/wled/config?hologram=' + (hologramOn ? 1 : 0));
    }
    function setWledIP() {
      const ip = document.getElementById('wledIP').value.trim();
      if (ip) { api('/wled/config?ip=' + encodeURIComponent(ip)); wledUpdateStatus(); }
    }
    function testWled() { api('/wled/test'); }
    async function wledUpdateStatus() {
      const r = await api('/wled/status');
      if (!r) return;
      const d = await r.json();
      wledOn = d.enabled;
      document.getElementById('wledToggle').className = 'tog ' + (wledOn ? 'on' : '');
      hologramOn = !!d.hologram;
      document.getElementById('hologramToggle').className = 'tog ' + (hologramOn ? 'on' : '');
      if (d.ip) document.getElementById('wledIP').value = d.ip;
      const dot = document.getElementById('wledDot');
      if (d.enabled && d.reachable) dot.className = 'dot on';
      else if (d.enabled) dot.className = 'dot err';
      else dot.className = 'dot';
      const el = document.getElementById('wledStatus');
      if (d.ip && d.enabled) {
        el.innerHTML = '<div style="color:' + (d.reachable ? '#88D498' : '#FF6B6B') +
          ';font-size:12px;font-weight:700;margin-bottom:8px">' + (d.reachable ? 'Reachable' : 'Unreachable') + ' (' + d.ip + ')</div>';
      } else { el.innerHTML = ''; }
    }

    const emojiNames=["Heart","Star","Check","X","Fire","Potion","Sword","Shield","ArrowUp","ArrowDn","ArrowL","ArrowR","Skull","Ghost","Alien","Pacman","PacGhost","ShyGuy","Music","WiFi","Rainbow","Mushroom","Skelly","Chicken","Invader","Dragon","TwnklHrt","Popsicle"];
    let emojiQueue=[];
    let emojiActive=false;

    function renderEmojiGrid() {
      document.getElementById('emojiGrid').innerHTML = emojiNames.map((n,i) => {
        const inQ = emojiQueue.indexOf(i) >= 0;
        return `<button class="${inQ?'active':''}" onclick="addWledEmoji(${i})" style="font-size:11px;padding:8px 4px">${n}</button>`;
      }).join('');
    }

    function renderEmojiQueue() {
      const el = document.getElementById('emojiQueue');
      if (emojiQueue.length === 0) {
        el.innerHTML = '<div class="hint">No sprites selected</div>';
        return;
      }
      el.innerHTML = emojiQueue.map((idx,pos) =>
        `<span onclick="removeWledEmoji(${pos})" style="display:inline-block;background:#FFD23F;border:2px solid #000;padding:3px 8px;margin:2px 4px 2px 0;font-size:11px;font-weight:700;cursor:pointer">${emojiNames[idx]} &times;</span>`
      ).join('');
    }

    function addWledEmoji(i) {
      if (emojiQueue.indexOf(i) < 0) emojiQueue.push(i);
      api('/wled/emoji/add?v=' + i);
      renderEmojiGrid(); renderEmojiQueue();
    }

    function removeWledEmoji(pos) {
      emojiQueue.splice(pos, 1);
      api('/wled/emoji/remove?v=' + pos);
      renderEmojiGrid(); renderEmojiQueue();
    }

    function clearWledEmoji() {
      emojiQueue = [];
      api('/wled/emoji/clear');
      renderEmojiGrid(); renderEmojiQueue();
    }

    function toggleWledEmoji() {
      emojiActive = !emojiActive;
      api('/wled/emoji/toggle');
      document.getElementById('emojiToggleBtn').textContent = emojiActive ? 'Stop' : 'Start';
      document.getElementById('emojiToggleBtn').className = 'btn-start flex1' + (emojiActive ? ' on' : '');
    }

    document.getElementById('emojiCycle').oninput = function() {
      document.getElementById('emojiCycleVal').textContent = this.value + 's';
    };
    document.getElementById('emojiCycle').onchange = function() {
      api('/wled/emoji/settings?cycle=' + (this.value * 1000));
    };

    function initEmoji() { renderEmojiGrid(); renderEmojiQueue(); }

    async function loadSounds() {
      try {
        const r = await fetch('/bot/sequences');
        const seqs = await r.json();
        document.getElementById('soundGrid').innerHTML = seqs.map(s =>
          `<button onclick="api('/bot/sound?seq=${s.id}')" style="font-size:11px;padding:8px 4px">${s.name}</button>`
        ).join('');
      } catch(e) {}
    }

    // ---- StackChan controls ----
    const scLedModes = ['off','breathing','rainbow','chase','fire','twinkle','pulse','aurora','mood'];
    let scLedMode = 2;

    function scPreset(name) { api('/bot/head/preset?name=' + name); }

    function scInitSliders() {
      const yaw = document.getElementById('scYaw');
      const pitch = document.getElementById('scPitch');
      yaw.oninput = function() {
        document.getElementById('scYawVal').textContent = (this.value / 10) + '°';
      };
      yaw.onchange = function() {
        api('/bot/head/set_angles?yaw=' + this.value + '&time=400');
      };
      pitch.oninput = function() {
        document.getElementById('scPitchVal').textContent = (this.value / 10) + '°';
      };
      pitch.onchange = function() {
        api('/bot/head/set_angles?pitch=' + this.value + '&time=400');
      };
      const ledBr = document.getElementById('scLedBr');
      ledBr.oninput = function() {
        document.getElementById('scLedBrVal').textContent = this.value;
      };
      ledBr.onchange = function() {
        api('/bot/base_leds/mode?brightness=' + this.value);
      };
      const ledSp = document.getElementById('scLedSp');
      ledSp.oninput = function() {
        document.getElementById('scLedSpVal').textContent = this.value;
      };
      ledSp.onchange = function() {
        api('/bot/base_leds/mode?speed=' + this.value);
      };
    }

    function scSetLedMode(v) {
      scLedMode = parseInt(v);
      api('/bot/base_leds/mode?mode=' + v);
    }

    function scRenderLedModes() {
      const sel = document.getElementById('scLedMode');
      sel.innerHTML = scLedModes.map((name, i) =>
        `<option value="${i}" ${i===scLedMode?'selected':''}>${name.charAt(0).toUpperCase()+name.slice(1)}</option>`
      ).join('');
    }

    let scChillOn = false;
    async function scToggleChill() {
      const r = await fetch('/bot/chill');
      if (r) {
        const d = await r.json();
        scChillOn = d.chill;
        document.getElementById('scChillToggle').className = 'tog ' + (scChillOn ? 'on' : '');
      }
    }

    function scUpdateFromState(s) {
      if (!s.stackchan) return;
      document.getElementById('scCard').style.display = '';
      const sc = s.stackchan;
      if (sc.chill !== undefined) {
        scChillOn = sc.chill;
        document.getElementById('scChillToggle').className = 'tog ' + (scChillOn ? 'on' : '');
      }
      if (sc.ledMode !== undefined) {
        scLedMode = sc.ledMode;
        document.getElementById('scLedMode').value = sc.ledMode;
      }
      if (sc.ledBrightness !== undefined) {
        document.getElementById('scLedBr').value = sc.ledBrightness;
        document.getElementById('scLedBrVal').textContent = sc.ledBrightness;
      }
      if (sc.ledSpeed !== undefined) {
        document.getElementById('scLedSp').value = sc.ledSpeed;
        document.getElementById('scLedSpVal').textContent = sc.ledSpeed;
      }
      if (sc.voltage !== undefined) {
        document.getElementById('scBattery').style.display = '';
        document.getElementById('scBatVal').textContent = sc.voltage + 'V / ' + (sc.current * 1000).toFixed(0) + 'mA';
      }
    }

    scRenderLedModes();
    scInitSliders();

    getState();
    render();
    wifiInitCheck();
    wledUpdateStatus();
    initEmoji();
    loadSounds();
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
  size_t len = strlen_P(webpage);
  server.setContentLength(len);
  server.send(200, "text/html", "");
  const size_t CHUNK = 1024;
  for (size_t i = 0; i < len; i += CHUNK) {
    char buf[CHUNK + 1];
    size_t n = min(CHUNK, len - i);
    memcpy_P(buf, webpage + i, n);
    buf[n] = '\0';
    server.sendContent(buf);
  }
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
                ",\"firmwareVersion\":\"" FIRMWARE_VERSION "\"" +
                ",\"boardType\":\"" BOARD_TYPE "\"" +
#ifdef TARGET_CORES3
                ",\"sensors\":{" +
                  "\"speaker\":" + (sysStatus.speakerReady ? "true" : "false") +
                  ",\"midiSynth\":" + (sysStatus.midiReady ? "true" : "false") +
                  ",\"useMidi\":" + (botSounds.useMidi ? "true" : "false") +
                  ",\"mic\":" + (sysStatus.micReady ? "true" : "false") +
                  ",\"proxLight\":" + (sysStatus.proxLightReady ? "true" : "false") +
                  ",\"soundEnabled\":" + (botSounds.enabled ? "true" : "false") +
                  ",\"soundVolume\":" + String(botSounds.volume) +
                  ",\"micEnabled\":" + (audioAnalysis.enabled ? "true" : "false") +
                  ",\"proximity\":" + String(proxLight.rawProximity) +
                  ",\"lux\":" + String(proxLight.ambientLux) +
                "}" +
#endif
#ifdef BOARD_HAS_STACKCHAN_BASE
                ",\"stackchan\":{" +
                  "\"ioExpander\":" + (sysStatus.scIoExpanderReady ? "true" : "false") +
                  ",\"vmEn\":" + (sysStatus.scVmEnReady ? "true" : "false") +
                  ",\"servoX\":" + (sysStatus.scServoXReady ? "true" : "false") +
                  ",\"servoY\":" + (sysStatus.scServoYReady ? "true" : "false") +
                  ",\"baseLeds\":" + (sysStatus.scBaseLedsReady ? "true" : "false") +
                  ",\"headTouch\":" + (sysStatus.scHeadTouchReady ? "true" : "false") +
                  ",\"battery\":" + (sysStatus.scBatteryMonReady ? "true" : "false") +
                  (sysStatus.scServoXReady ? ",\"servoXPos\":" + String(scReadServoPos(SC_SERVO_X_ID)) : "") +
                  (sysStatus.scServoYReady ? ",\"servoYPos\":" + String(scReadServoPos(SC_SERVO_Y_ID)) : "") +
                  (sysStatus.scBatteryMonReady ? ",\"voltage\":" + String(scGetBatteryVoltage(), 2) +
                                                  ",\"current\":" + String(scGetBatteryCurrent(), 3) : "") +
                  ",\"chill\":" + (scTouch_state.chillMode ? "true" : "false") +
                  (sysStatus.scBaseLedsReady ? ",\"ledMode\":" + String(scLeds.mode) +
                                                ",\"ledModeName\":\"" + String(SC_LED_MODE_NAMES[scLeds.mode]) + "\"" +
                                                ",\"ledBrightness\":" + String(scLeds.brightness) +
                                                ",\"ledSpeed\":" + String(scLeds.speed) : "") +
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
                  ",\"ntpSynced\":" + (sysStatus.ntpSynced ? "true" : "false") +
                  ",\"groups\":" + String(cloudMeta.groupCount) +
                  ",\"fleetTotal\":" + String(cloudMeta.fleetTotal) +
                  ",\"fleetOnline\":" + String(cloudMeta.fleetOnline) +
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
extern void cmdPlaySequence(uint8_t seqId);
extern void cmdSetVolume(uint8_t vol);

void handleBrightness() {
  if (server.hasArg("v")) {
    uint8_t val = constrain(server.arg("v").toInt(), 1, 255);
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
    // lock=1 stops autoCycle so the selected effect stays
    if (server.hasArg("lock") && server.arg("lock") == "1") {
      autoCycle = false;
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleAutoCycle() {
  if (server.hasArg("v")) {
    autoCycle = (server.arg("v") == "1");
    if (autoCycle) {
      lastChange = millis();  // reset timer so it doesn't immediately cycle
    }
  }
  server.send(200, "text/plain", "OK");
}

// ============================================================================
// Personality Handlers
// ============================================================================

extern void setBotPersonality(uint8_t index);
extern uint8_t getBotPersonality();
extern RuntimePersonality runtimePersonalities[];
extern uint8_t runtimePersonalityCount;

void handleBotPersonality() {
  if (server.method() == HTTP_GET) {
    // Return current personality + list of all loaded
    String json = "{\"current\":";
    json += botMode.personalityIndex;
    json += ",\"rotInterval\":";
    json += botMode.personalityRotIntervalMs;
    json += ",\"rotList\":[";
    for (uint8_t i = 0; i < botMode.personalityListCount; i++) {
      if (i > 0) json += ",";
      json += botMode.personalityList[i];
    }
    json += "],\"personalities\":[";
    for (uint8_t i = 0; i < runtimePersonalityCount; i++) {
      if (i > 0) json += ",";
      json += "{\"index\":";
      json += i;
      json += ",\"name\":\"";
      json += runtimePersonalities[i].name;
      json += "\",\"cloud\":";
      json += (runtimePersonalities[i].cloudId[0] != '\0') ? "true" : "false";
      json += "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
  } else {
    // POST: set single personality (stops rotation)
    if (server.hasArg("v")) {
      uint8_t idx = constrain(server.arg("v").toInt(), 0, runtimePersonalityCount - 1);
      cmdSetPersonality(idx);
    }
    server.send(200, "text/plain", "OK");
  }
}

void handleBotPersonalityRotation() {
  // POST body: JSON with list[] and interval
  if (server.hasArg("plain")) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    uint32_t interval = doc["interval"] | 300000;
    botMode.personalityRotIntervalMs = interval;
    botMode.lastPersonalityRotMs = millis();
    botMode.personalityListCount = 0;

    JsonArray list = doc["list"];
    if (list) {
      for (size_t i = 0; i < list.size() && i < MAX_RUNTIME_PERSONALITIES; i++) {
        botMode.personalityList[botMode.personalityListCount++] = list[i] | 0;
      }
    }
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
  if (server.hasArg("seq")) {
    uint8_t seqId = constrain(server.arg("seq").toInt(), 0, 255);
    cmdPlaySequence(seqId);
  } else if (server.hasArg("freq") && server.hasArg("dur")) {
    uint16_t freq = constrain(server.arg("freq").toInt(), 100, 8000);
    uint16_t dur = constrain(server.arg("dur").toInt(), 10, 5000);
    cmdPlaySound(freq, dur);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotSequences() {
  String json = "[";
  for (uint8_t i = 1; i < BUILTIN_SEQ_COUNT; i++) {
    if (i > 1) json += ",";
    json += "{\"id\":" + String(i) + ",\"name\":\"" + String(builtinSequences[i].name) + "\"}";
  }
  for (uint8_t i = 0; i < cloudSequenceCount; i++) {
    json += ",{\"id\":" + String(SEQ_CLOUD_BASE + i) + ",\"name\":\"" + String(cloudSequences[i].name) + "\",\"cloud\":true}";
  }
  json += "]";
  server.send(200, "application/json", json);
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

// ============================================================================
// Schedule Handlers
// ============================================================================

extern ScheduledContentState schedContent;
extern void saveScheduleSettings();

void handleSchedule() {
  bool changed = false;
  if (server.hasArg("enabled")) {
    bool wasEnabled = schedContent.enabled;
    schedContent.enabled = server.arg("enabled") == "1";
    changed = true;
    // First enable: trigger first cycle in ~1 minute instead of waiting full interval
    if (schedContent.enabled && !wasEnabled) {
      schedContent.lastCycleStartMs = millis() - schedContent.cycleIntervalMs + 60000;
    }
  }
  if (server.hasArg("intervalMin")) {
    uint32_t min = constrain(server.arg("intervalMin").toInt(), 1, 120);
    schedContent.cycleIntervalMs = min * 60000;
    changed = true;
  }
  if (changed) saveScheduleSettings();

  String json = "{\"enabled\":";
  json += schedContent.enabled ? "true" : "false";
  json += ",\"intervalMin\":";
  json += schedContent.cycleIntervalMs / 60000;
  json += ",\"phase\":";
  json += (uint8_t)schedContent.phase;
  json += ",\"isOwner\":";
  json += schedContent.isOwner ? "true" : "false";
  json += "}";
  server.send(200, "application/json", json);
}

// ============================================================================
// StackChan Endpoints
// ============================================================================
#ifdef BOARD_HAS_STACKCHAN_BASE

void scSendDeferred(uint8_t phase) {
  String json = "{\"status\":\"deferred\",\"phase\":";
  json += phase;
  json += "}";
  server.send(501, "application/json", json);
}

// POST /bot/head/set_angles?yaw=N&pitch=N&time=N
// yaw: tenths of degrees (-1280 to 1280), pitch: tenths (0 to 900)
// time: movement duration in ms (default 500)
void handleScHeadSetAngles() {
  if (!sysStatus.scServoXReady && !sysStatus.scServoYReady) {
    server.send(503, "application/json", "{\"error\":\"servos not ready\"}");
    return;
  }
  int yaw   = server.hasArg("yaw")   ? server.arg("yaw").toInt()   : 0;
  int pitch = server.hasArg("pitch") ? server.arg("pitch").toInt() : 0;
  int timeMs = server.hasArg("time") ? server.arg("time").toInt()  : 500;
  if (timeMs < 20) timeMs = 20;
  if (timeMs > 5000) timeMs = 5000;

  if (server.hasArg("yaw"))   scMoveYaw(yaw, timeMs);
  if (server.hasArg("pitch")) scMovePitch(pitch, timeMs);

  String json = "{\"ok\":true,\"yaw\":";
  json += yaw;
  json += ",\"pitch\":";
  json += pitch;
  json += ",\"time\":";
  json += timeMs;
  json += "}";
  server.send(200, "application/json", json);
}

// POST /bot/head/preset?name=nod|shake|lookup|lookdown|left|right
void handleScHeadPreset() {
  if (!sysStatus.scServoXReady && !sysStatus.scServoYReady) {
    server.send(503, "application/json", "{\"error\":\"servos not ready\"}");
    return;
  }
  String name = server.hasArg("name") ? server.arg("name") : "nod";

  // Presets use home pitch as anchor, all angles in tenths of degrees
  constexpr int homePitch = SC_SERVO_Y_HOME_DEG * 10;
  if (name == "nod") {
    // Dip down 15° from home, return — 3 cycles
    for (int i = 0; i < 3; i++) {
      scMovePitch(homePitch - 150, 250);
      delay(250);
      scMovePitch(homePitch, 250);
      delay(250);
    }
  } else if (name == "shake") {
    // Hold home pitch, swing yaw ±25° — 3 cycles, return center
    for (int i = 0; i < 3; i++) {
      scMoveYaw(250, 200);
      delay(200);
      scMoveYaw(-250, 200);
      delay(200);
    }
    scMoveYaw(0, 250);
  } else if (name == "lookup") {
    scMovePitch(900, 500);   // Full up (90 degrees)
  } else if (name == "lookdown") {
    scMovePitch(SC_SERVO_Y_MIN_DEG * 10, 500);
  } else if (name == "left") {
    scMoveYaw(1000, 500);    // 100 degrees left (BSP example value)
  } else if (name == "right") {
    scMoveYaw(-1000, 500);   // 100 degrees right (BSP example value)
  } else {
    server.send(400, "application/json", "{\"error\":\"unknown preset\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true,\"preset\":\"" + name + "\"}");
}

// POST /bot/head/recenter
void handleScHeadRecenter() {
  if (!sysStatus.scServoXReady && !sysStatus.scServoYReady) {
    server.send(503, "application/json", "{\"error\":\"servos not ready\"}");
    return;
  }
  scGoHome(500);
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /bot/base_leds/set?r=N&g=N&b=N  OR  ?index=N&r=N&g=N&b=N
void handleScBaseLeds() {
  if (!sysStatus.scBaseLedsReady) {
    server.send(503, "application/json", "{\"error\":\"base LEDs not ready\"}");
    return;
  }
  uint8_t r = server.hasArg("r") ? server.arg("r").toInt() : 0;
  uint8_t g = server.hasArg("g") ? server.arg("g").toInt() : 0;
  uint8_t b = server.hasArg("b") ? server.arg("b").toInt() : 0;

  if (server.hasArg("index")) {
    int idx = server.arg("index").toInt();
    if (idx < 0 || idx >= SC_BASE_LED_COUNT) {
      server.send(400, "application/json", "{\"error\":\"index 0-11\"}");
      return;
    }
    scSetBaseLedColor(idx, r, g, b);
    scRefreshBaseLeds();
  } else {
    scSetAllBaseLeds(r, g, b);
  }

  String json = "{\"ok\":true,\"r\":";
  json += r;
  json += ",\"g\":";
  json += g;
  json += ",\"b\":";
  json += b;
  json += "}";
  server.send(200, "application/json", json);
}

// GET /bot/battery/status
void handleScBatteryStatus() {
  if (!sysStatus.scBatteryMonReady) {
    server.send(503, "application/json", "{\"error\":\"battery monitor not ready\"}");
    return;
  }
  float volts = scGetBatteryVoltage();
  float amps  = scGetBatteryCurrent();

  String json = "{\"voltage\":";
  json += String(volts, 2);
  json += ",\"current\":";
  json += String(amps, 3);
  json += "}";
  server.send(200, "application/json", json);
}

// POST /bot/base_leds/mode?mode=N (0-8) or ?name=rainbow etc.
// Optional: ?brightness=N (0-255), ?speed=N (0-255)
void handleScBaseLedMode() {
  if (!sysStatus.scBaseLedsReady) {
    server.send(503, "application/json", "{\"error\":\"base LEDs not ready\"}");
    return;
  }

  if (server.hasArg("mode")) {
    int m = server.arg("mode").toInt();
    if (m >= 0 && m < SC_LED_MODE_COUNT) scLeds.mode = m;
  } else if (server.hasArg("name")) {
    String name = server.arg("name");
    for (int i = 0; i < SC_LED_MODE_COUNT; i++) {
      if (name.equalsIgnoreCase(SC_LED_MODE_NAMES[i])) {
        scLeds.mode = i;
        break;
      }
    }
  }
  if (server.hasArg("brightness")) {
    scLeds.brightness = constrain(server.arg("brightness").toInt(), 0, 255);
  }
  if (server.hasArg("speed")) {
    scLeds.speed = constrain(server.arg("speed").toInt(), 0, 255);
  }

  String json = "{\"ok\":true,\"mode\":";
  json += scLeds.mode;
  json += ",\"name\":\"";
  json += SC_LED_MODE_NAMES[scLeds.mode];
  json += "\",\"brightness\":";
  json += scLeds.brightness;
  json += ",\"speed\":";
  json += scLeds.speed;
  json += ",\"modes\":[";
  for (int i = 0; i < SC_LED_MODE_COUNT; i++) {
    if (i > 0) json += ",";
    json += "\"";
    json += SC_LED_MODE_NAMES[i];
    json += "\"";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// POST /bot/chill — toggle chill mode
void handleScChillToggle() {
  scFireChillMode();
  server.send(200, "application/json",
    scTouch_state.chillMode ? "{\"chill\":true}" : "{\"chill\":false}");
}

// Camera endpoints remain stubs (Phase 4)
void handleScPhotoCapture()   { scSendDeferred(4); }
void handleScPhotoList()      { scSendDeferred(4); }
void handleScPhotoGet()       { scSendDeferred(4); }
void handleScPhotoDelete()    { scSendDeferred(4); }

#endif // BOARD_HAS_STACKCHAN_BASE

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
  server.on("/bot/autocycle", handleAutoCycle);
  server.on("/bot/personality", handleBotPersonality);
  server.on("/bot/personality/rotation", handleBotPersonalityRotation);

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
  server.on("/bot/sequences", handleBotSequences);
  server.on("/bot/mic", handleBotMic);
  #endif

  // Cloud endpoints
  #ifdef CLOUD_ENABLED
  server.on("/cloud/status", handleCloudStatus);
  server.on("/cloud/sync", handleCloudSync);
  #endif

  // StackChan stub endpoints (501 until driver bring-up)
  #ifdef BOARD_HAS_STACKCHAN_BASE
  server.on("/bot/head/set_angles", handleScHeadSetAngles);
  server.on("/bot/head/preset", handleScHeadPreset);
  server.on("/bot/head/recenter", handleScHeadRecenter);
  server.on("/bot/base_leds/set", handleScBaseLeds);
  server.on("/bot/base_leds/mode", handleScBaseLedMode);
  server.on("/bot/chill", handleScChillToggle);
  server.on("/bot/photo/capture", handleScPhotoCapture);
  server.on("/bot/photos", handleScPhotoList);
  server.on("/bot/photo/get", handleScPhotoGet);
  server.on("/bot/photo/delete", handleScPhotoDelete);
  server.on("/bot/battery/status", handleScBatteryStatus);
  #endif

  // Schedule endpoints
  server.on("/schedule", handleSchedule);

  // OTA firmware update endpoints
  server.on("/update", HTTP_GET, handleOTAPage);
  server.on("/update", HTTP_POST, handleOTAResult, handleOTAUpload);

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
