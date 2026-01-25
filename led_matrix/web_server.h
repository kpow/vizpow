#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>
#include <FastLED.h>
#include "config.h"
#include "palettes.h"

// External references to globals
extern WebServer server;
extern uint8_t effectIndex;
extern uint8_t paletteIndex;
extern uint8_t brightness;
extern uint8_t speed;
extern bool autoCycle;
extern bool motionMode;
extern CRGBPalette16 currentPalette;

// Web interface HTML
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>LED Matrix</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { 
      font-family: -apple-system, sans-serif; 
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%); 
      color: #fff; 
      min-height: 100vh;
      padding: 20px;
    }
    h1 { text-align: center; margin-bottom: 20px; font-size: 24px; }
    h2 { font-size: 14px; color: #888; margin-bottom: 10px; text-transform: uppercase; }
    .card { 
      background: rgba(255,255,255,0.1); 
      border-radius: 16px; 
      padding: 20px; 
      margin-bottom: 16px;
      backdrop-filter: blur(10px);
    }
    .grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
    .grid4 { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; }
    button {
      background: rgba(255,255,255,0.15);
      border: none;
      color: #fff;
      padding: 14px 10px;
      border-radius: 12px;
      font-size: 13px;
      cursor: pointer;
      transition: all 0.2s;
    }
    button:active { transform: scale(0.95); }
    button.active { background: #6366f1; }
    .tab-row { display: flex; gap: 10px; margin-bottom: 15px; }
    .tab {
      flex: 1;
      background: rgba(255,255,255,0.1);
      border: none;
      color: #888;
      padding: 12px;
      border-radius: 10px;
      font-size: 14px;
      cursor: pointer;
    }
    .tab.active { background: #6366f1; color: #fff; }
    .slider-container { margin: 15px 0; }
    .slider-label { display: flex; justify-content: space-between; margin-bottom: 8px; }
    input[type="range"] {
      width: 100%;
      height: 8px;
      border-radius: 4px;
      background: rgba(255,255,255,0.2);
      -webkit-appearance: none;
    }
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 24px;
      height: 24px;
      border-radius: 50%;
      background: #6366f1;
      cursor: pointer;
    }
    .toggle-row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; }
    .toggle {
      width: 52px;
      height: 32px;
      background: rgba(255,255,255,0.2);
      border-radius: 16px;
      position: relative;
      cursor: pointer;
      transition: background 0.3s;
    }
    .toggle.on { background: #6366f1; }
    .toggle::after {
      content: '';
      position: absolute;
      width: 26px;
      height: 26px;
      background: #fff;
      border-radius: 50%;
      top: 3px;
      left: 3px;
      transition: transform 0.3s;
    }
    .toggle.on::after { transform: translateX(20px); }
    .status { text-align: center; color: #888; font-size: 12px; margin-top: 10px; }
  </style>
</head>
<body>
  <h1>LED Matrix Control</h1>
  
  <div class="card">
    <div class="tab-row">
      <button class="tab active" id="tabMotion" onclick="setMode(true)">Motion</button>
      <button class="tab" id="tabAmbient" onclick="setMode(false)">Ambient</button>
    </div>
    <div class="grid" id="effects"></div>
  </div>
  
  <div class="card">
    <h2>Palettes</h2>
    <div class="grid4" id="palettes"></div>
  </div>
  
  <div class="card">
    <h2>Settings</h2>
    <div class="slider-container">
      <div class="slider-label"><span>Brightness</span><span id="brightnessVal">15</span></div>
      <input type="range" id="brightness" min="1" max="50" value="15">
    </div>
    <div class="slider-container">
      <div class="slider-label"><span>Speed</span><span id="speedVal">20</span></div>
      <input type="range" id="speed" min="5" max="100" value="20">
    </div>
    <div class="toggle-row">
      <span>Auto Cycle</span>
      <div class="toggle" id="autoCycle"></div>
    </div>
  </div>
  
  <div class="status">Connected to LED-Matrix</div>

  <script>
    const motionEffects = ["Tilt Ball", "Motion Plasma", "Shake Sparkle", "Tilt Wave", "Spin Trails", "Gravity Pixels", "Motion Noise", "Tilt Ripple", "Gyro Swirl", "Shake Explode", "Tilt Fire", "Motion Rainbow"];
    const ambientEffects = ["Plasma", "Rainbow", "Fire", "Ocean", "Sparkle", "Wave", "Spiral", "Breathe", "Matrix", "Lava", "Aurora", "Confetti", "Comet", "Galaxy", "Heart"];
    const palettes = ["Rainbow", "Ocean", "Lava", "Forest", "Party", "Heat", "Cloud", "Sunset", "Cyber", "Toxic", "Ice", "Blood", "Vaporwave", "Forest2", "Gold"];
    
    let state = { effect: 0, palette: 0, brightness: 15, speed: 20, autoCycle: false, motionMode: true };
    
    function render() {
      const effects = state.motionMode ? motionEffects : ambientEffects;
      document.getElementById('tabMotion').className = 'tab ' + (state.motionMode ? 'active' : '');
      document.getElementById('tabAmbient').className = 'tab ' + (!state.motionMode ? 'active' : '');
      document.getElementById('effects').innerHTML = effects.map((e, i) => 
        `<button class="${state.effect === i ? 'active' : ''}" onclick="setEffect(${i})">${e}</button>`
      ).join('');
      document.getElementById('palettes').innerHTML = palettes.map((p, i) => 
        `<button class="${state.palette === i ? 'active' : ''}" onclick="setPalette(${i})">${p}</button>`
      ).join('');
      document.getElementById('brightness').value = state.brightness;
      document.getElementById('brightnessVal').textContent = state.brightness;
      document.getElementById('speed').value = state.speed;
      document.getElementById('speedVal').textContent = state.speed;
      document.getElementById('autoCycle').className = 'toggle ' + (state.autoCycle ? 'on' : '');
    }
    
    async function api(endpoint) {
      try { await fetch(endpoint); } catch(e) {}
    }
    
    function setMode(isMotion) { 
      state.motionMode = isMotion; 
      state.effect = 0;
      render(); 
      api('/mode?v=' + (isMotion ? 1 : 0)); 
    }
    function setEffect(i) { state.effect = i; render(); api('/effect?v=' + i); }
    function setPalette(i) { state.palette = i; render(); api('/palette?v=' + i); }
    
    document.getElementById('brightness').oninput = function() {
      state.brightness = this.value;
      document.getElementById('brightnessVal').textContent = this.value;
    };
    document.getElementById('brightness').onchange = function() { api('/brightness?v=' + this.value); };
    
    document.getElementById('speed').oninput = function() {
      state.speed = this.value;
      document.getElementById('speedVal').textContent = this.value;
    };
    document.getElementById('speed').onchange = function() { api('/speed?v=' + this.value); };
    
    document.getElementById('autoCycle').onclick = function() {
      state.autoCycle = !state.autoCycle;
      render();
      api('/autocycle?v=' + (state.autoCycle ? 1 : 0));
    };
    
    async function getState() {
      try {
        const r = await fetch('/state');
        state = await r.json();
        render();
      } catch(e) {}
    }
    
    getState();
  </script>
</body>
</html>
)rawliteral";

// Web server handlers
void handleRoot() {
  server.send(200, "text/html", webpage);
}

void handleState() {
  String json = "{\"effect\":" + String(effectIndex) + 
                ",\"palette\":" + String(paletteIndex) + 
                ",\"brightness\":" + String(brightness) + 
                ",\"speed\":" + String(speed) + 
                ",\"autoCycle\":" + (autoCycle ? "true" : "false") +
                ",\"motionMode\":" + (motionMode ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleMode() {
  if (server.hasArg("v")) {
    motionMode = server.arg("v").toInt() == 1;
    effectIndex = 0;
    FastLED.clear();
  }
  server.send(200, "text/plain", "OK");
}

void handleEffect() {
  if (server.hasArg("v")) {
    int maxEffects = motionMode ? NUM_MOTION_EFFECTS : NUM_AMBIENT_EFFECTS;
    effectIndex = server.arg("v").toInt() % maxEffects;
    FastLED.clear();
  }
  server.send(200, "text/plain", "OK");
}

void handlePalette() {
  if (server.hasArg("v")) {
    paletteIndex = server.arg("v").toInt() % NUM_PALETTES;
    currentPalette = palettes[paletteIndex];
  }
  server.send(200, "text/plain", "OK");
}

void handleBrightness() {
  if (server.hasArg("v")) {
    brightness = constrain(server.arg("v").toInt(), 1, 50);
    FastLED.setBrightness(brightness);
  }
  server.send(200, "text/plain", "OK");
}

void handleSpeed() {
  if (server.hasArg("v")) {
    speed = constrain(server.arg("v").toInt(), 5, 100);
  }
  server.send(200, "text/plain", "OK");
}

void handleAutoCycle() {
  if (server.hasArg("v")) {
    autoCycle = server.arg("v").toInt() == 1;
  }
  server.send(200, "text/plain", "OK");
}

// Setup all web server routes
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/mode", handleMode);
  server.on("/effect", handleEffect);
  server.on("/palette", handlePalette);
  server.on("/brightness", handleBrightness);
  server.on("/speed", handleSpeed);
  server.on("/autocycle", handleAutoCycle);
  server.begin();
}

#endif
