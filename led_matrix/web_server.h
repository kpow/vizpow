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
extern uint8_t currentMode;
extern CRGBPalette16 currentPalette;

// Emoji-related externs (defined in effects_emoji.h)
extern uint8_t emojiQueueCount;
extern uint16_t emojiCycleTime;
extern uint16_t emojiFadeDuration;
extern bool emojiAutoCycle;
void clearEmojiQueue();
bool addEmojiToQueue(const char* hexData);
void setEmojiSettings(uint16_t cycleTime, uint16_t fadeDuration, bool autoCycle);

// Sprite sheet base64 (emojis.png from ESP32RobotWebServer)
const char emojiSpriteBase64[] PROGMEM = "data:image/png;base64,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";

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
    .hidden { display: none; }
    #spriteContainer {
      background: #000;
      border-radius: 8px;
      padding: 10px;
      margin-bottom: 15px;
      overflow-x: auto;
    }
    #spriteCanvas {
      cursor: crosshair;
      image-rendering: pixelated;
      display: block;
      margin: 0 auto;
    }
    #emojiQueue {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      margin-bottom: 15px;
      min-height: 50px;
    }
    .queueItem {
      width: 40px;
      height: 40px;
      border-radius: 8px;
      background: #000;
      position: relative;
      cursor: pointer;
    }
    .queueItem canvas {
      width: 100%;
      height: 100%;
      image-rendering: pixelated;
      border-radius: 8px;
    }
    .queueItem:hover::after {
      content: 'x';
      position: absolute;
      top: -5px;
      right: -5px;
      background: #ef4444;
      color: #fff;
      width: 18px;
      height: 18px;
      border-radius: 50%;
      font-size: 12px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .queueEmpty {
      color: #666;
      font-style: italic;
      padding: 10px;
    }
    .btn-row { display: flex; gap: 10px; margin-top: 10px; }
    .btn-row button { flex: 1; }
    .btn-danger { background: rgba(239, 68, 68, 0.3); }
  </style>
</head>
<body>
  <h1>LED Matrix Control</h1>

  <div class="card">
    <div class="tab-row">
      <button class="tab active" id="tabMotion" onclick="setMode(0)">Motion</button>
      <button class="tab" id="tabAmbient" onclick="setMode(1)">Ambient</button>
      <button class="tab" id="tabEmoji" onclick="setMode(2)">Emoji</button>
    </div>

    <div id="effectsPanel">
      <div class="grid" id="effects"></div>
    </div>

    <div id="emojiPanel" class="hidden">
      <h2>Select Emoji</h2>
      <div id="spriteContainer">
        <canvas id="spriteCanvas"></canvas>
      </div>
      <h2>Queue (<span id="queueCount">0</span>/8)</h2>
      <div id="emojiQueue"><span class="queueEmpty">Click an emoji above to add</span></div>
      <div class="btn-row">
        <button onclick="sendQueue()">Send to Display</button>
        <button class="btn-danger" onclick="clearQueue()">Clear</button>
      </div>
      <div class="slider-container">
        <div class="slider-label"><span>Cycle Time</span><span id="cycleVal">3s</span></div>
        <input type="range" id="cycleTime" min="1" max="10" value="3">
      </div>
      <div class="slider-container">
        <div class="slider-label"><span>Fade Duration</span><span id="fadeVal">500ms</span></div>
        <input type="range" id="fadeDuration" min="100" max="2000" value="500" step="100">
      </div>
      <div class="toggle-row">
        <span>Auto Cycle</span>
        <div class="toggle on" id="emojiAutoCycle"></div>
      </div>
    </div>
  </div>

  <div class="card" id="paletteCard">
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
    const ambientEffects = ["Plasma", "Rainbow", "Fire", "Ocean", "Sparkle", "Wave", "Spiral", "Breathe", "Matrix", "Lava", "Aurora", "Confetti", "Comet", "Galaxy", "Heart", "Donut"];
    const palettes = ["Rainbow", "Ocean", "Lava", "Forest", "Party", "Heat", "Cloud", "Sunset", "Cyber", "Toxic", "Ice", "Blood", "Vaporwave", "Forest2", "Gold"];

    let state = { effect: 0, palette: 0, brightness: 15, speed: 20, autoCycle: false, currentMode: 0 };
    let emojiQueue = [];
    let spriteImg = null;
    let emojiAutoCycle = true;

    // Sprite sheet grid parameters
    const EMOJI_SIZE = 8;
    const GRID_X_SPACING = 13;
    const GRID_Y_SPACING = 12;
    const GRID_X_OFFSET = 3;
    const GRID_Y_OFFSET = 3;

    function render() {
      const effects = state.currentMode === 0 ? motionEffects : ambientEffects;
      document.getElementById('tabMotion').className = 'tab ' + (state.currentMode === 0 ? 'active' : '');
      document.getElementById('tabAmbient').className = 'tab ' + (state.currentMode === 1 ? 'active' : '');
      document.getElementById('tabEmoji').className = 'tab ' + (state.currentMode === 2 ? 'active' : '');

      document.getElementById('effectsPanel').className = state.currentMode === 2 ? 'hidden' : '';
      document.getElementById('emojiPanel').className = state.currentMode === 2 ? '' : 'hidden';
      document.getElementById('paletteCard').className = state.currentMode === 2 ? 'card hidden' : 'card';

      if (state.currentMode !== 2) {
        document.getElementById('effects').innerHTML = effects.map((e, i) =>
          `<button class="${state.effect === i ? 'active' : ''}" onclick="setEffect(${i})">${e}</button>`
        ).join('');
      }

      document.getElementById('palettes').innerHTML = palettes.map((p, i) =>
        `<button class="${state.palette === i ? 'active' : ''}" onclick="setPalette(${i})">${p}</button>`
      ).join('');
      document.getElementById('brightness').value = state.brightness;
      document.getElementById('brightnessVal').textContent = state.brightness;
      document.getElementById('speed').value = state.speed;
      document.getElementById('speedVal').textContent = state.speed;
      document.getElementById('autoCycle').className = 'toggle ' + (state.autoCycle ? 'on' : '');
    }

    function renderEmojiQueue() {
      const container = document.getElementById('emojiQueue');
      document.getElementById('queueCount').textContent = emojiQueue.length;

      if (emojiQueue.length === 0) {
        container.innerHTML = '<span class="queueEmpty">Click an emoji above to add</span>';
        return;
      }

      container.innerHTML = '';
      emojiQueue.forEach((data, i) => {
        const div = document.createElement('div');
        div.className = 'queueItem';
        div.onclick = () => removeFromQueue(i);

        const canvas = document.createElement('canvas');
        canvas.width = 8;
        canvas.height = 8;
        const ctx = canvas.getContext('2d');
        const imgData = ctx.createImageData(8, 8);

        for (let p = 0; p < 64; p++) {
          const offset = p * 6;
          imgData.data[p * 4] = parseInt(data.substr(offset, 2), 16);
          imgData.data[p * 4 + 1] = parseInt(data.substr(offset + 2, 2), 16);
          imgData.data[p * 4 + 2] = parseInt(data.substr(offset + 4, 2), 16);
          imgData.data[p * 4 + 3] = 255;
        }
        ctx.putImageData(imgData, 0, 0);

        div.appendChild(canvas);
        container.appendChild(div);
      });
    }

    function removeFromQueue(index) {
      emojiQueue.splice(index, 1);
      renderEmojiQueue();
    }

    async function api(endpoint) {
      try { await fetch(endpoint); } catch(e) {}
    }

    function setMode(mode) {
      state.currentMode = mode;
      state.effect = 0;
      render();
      api('/mode?v=' + mode);
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

    // Emoji-specific controls
    document.getElementById('cycleTime').oninput = function() {
      document.getElementById('cycleVal').textContent = this.value + 's';
    };
    document.getElementById('cycleTime').onchange = function() {
      updateEmojiSettings();
    };

    document.getElementById('fadeDuration').oninput = function() {
      document.getElementById('fadeVal').textContent = this.value + 'ms';
    };
    document.getElementById('fadeDuration').onchange = function() {
      updateEmojiSettings();
    };

    document.getElementById('emojiAutoCycle').onclick = function() {
      emojiAutoCycle = !emojiAutoCycle;
      this.className = 'toggle ' + (emojiAutoCycle ? 'on' : '');
      updateEmojiSettings();
    };

    function updateEmojiSettings() {
      const cycleTime = document.getElementById('cycleTime').value * 1000;
      const fadeDuration = document.getElementById('fadeDuration').value;
      api('/emoji/settings?cycle=' + cycleTime + '&fade=' + fadeDuration + '&auto=' + (emojiAutoCycle ? 1 : 0));
    }

    async function sendQueue() {
      if (emojiQueue.length === 0) return;

      try {
        await fetch('/emoji/queue', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ emojis: emojiQueue })
        });
      } catch(e) {}
    }

    function clearQueue() {
      emojiQueue = [];
      renderEmojiQueue();
      api('/emoji/clear');
    }

    // Sprite sheet handling
    function initSpriteSheet() {
      const canvas = document.getElementById('spriteCanvas');
      const ctx = canvas.getContext('2d');

      spriteImg = new Image();
      spriteImg.onload = function() {
        canvas.width = spriteImg.width * 2;
        canvas.height = spriteImg.height * 2;
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(spriteImg, 0, 0, canvas.width, canvas.height);
      };
      spriteImg.src = SPRITE_DATA;

      canvas.onclick = function(e) {
        if (emojiQueue.length >= 8) {
          alert('Queue is full (max 8)');
          return;
        }

        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        const x = (e.clientX - rect.left) * scaleX / 2;
        const y = (e.clientY - rect.top) * scaleY / 2;

        const gridX = Math.floor((x - GRID_X_OFFSET) / GRID_X_SPACING);
        const gridY = Math.floor((y - GRID_Y_OFFSET) / GRID_Y_SPACING);

        if (gridX < 0 || gridY < 0) return;

        const emojiX = GRID_X_OFFSET + gridX * GRID_X_SPACING;
        const emojiY = GRID_Y_OFFSET + gridY * GRID_Y_SPACING;

        // Extract pixel data from sprite
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = spriteImg.width;
        tempCanvas.height = spriteImg.height;
        const tempCtx = tempCanvas.getContext('2d');
        tempCtx.drawImage(spriteImg, 0, 0);

        const imageData = tempCtx.getImageData(emojiX, emojiY, EMOJI_SIZE, EMOJI_SIZE);
        let hexData = '';

        for (let i = 0; i < 64; i++) {
          const r = imageData.data[i * 4];
          const g = imageData.data[i * 4 + 1];
          const b = imageData.data[i * 4 + 2];
          hexData += r.toString(16).padStart(2, '0');
          hexData += g.toString(16).padStart(2, '0');
          hexData += b.toString(16).padStart(2, '0');
        }

        emojiQueue.push(hexData);
        renderEmojiQueue();
      };
    }

    async function getState() {
      try {
        const r = await fetch('/state');
        state = await r.json();
        render();
      } catch(e) {}
    }

    // Get sprite data URL from server
    let SPRITE_DATA = '';
    async function loadSprite() {
      try {
        const r = await fetch('/emoji/sprite');
        SPRITE_DATA = await r.text();
        initSpriteSheet();
      } catch(e) {}
    }

    getState();
    loadSprite();
    renderEmojiQueue();
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
                ",\"currentMode\":" + String(currentMode) + "}";
  server.send(200, "application/json", json);
}

void handleMode() {
  if (server.hasArg("v")) {
    currentMode = constrain(server.arg("v").toInt(), 0, 2);
    effectIndex = 0;
    FastLED.clear();
  }
  server.send(200, "text/plain", "OK");
}

void handleEffect() {
  if (server.hasArg("v")) {
    int maxEffects = (currentMode == MODE_MOTION) ? NUM_MOTION_EFFECTS : NUM_AMBIENT_EFFECTS;
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

// Emoji handlers
void handleEmojiSprite() {
  server.send(200, "text/plain", emojiSpriteBase64);
}

void handleEmojiQueue() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String body = server.arg("plain");

  // Clear existing queue first
  clearEmojiQueue();

  // Parse JSON array of hex strings
  // Simple parsing: find each hex string between quotes
  int start = 0;
  while ((start = body.indexOf('"', start)) != -1) {
    start++; // Move past opening quote
    int end = body.indexOf('"', start);
    if (end == -1) break;

    String hexData = body.substring(start, end);
    if (hexData.length() == 384) {  // Valid emoji hex data
      addEmojiToQueue(hexData.c_str());
    }
    start = end + 1;
  }

  server.send(200, "text/plain", "OK");
}

void handleEmojiSettings() {
  uint16_t cycleTime = emojiCycleTime;
  uint16_t fadeDuration = emojiFadeDuration;
  bool autoCycleVal = emojiAutoCycle;

  if (server.hasArg("cycle")) {
    cycleTime = server.arg("cycle").toInt();
  }
  if (server.hasArg("fade")) {
    fadeDuration = server.arg("fade").toInt();
  }
  if (server.hasArg("auto")) {
    autoCycleVal = server.arg("auto").toInt() == 1;
  }

  setEmojiSettings(cycleTime, fadeDuration, autoCycleVal);
  server.send(200, "text/plain", "OK");
}

void handleEmojiClear() {
  clearEmojiQueue();
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

  // Emoji endpoints
  server.on("/emoji/sprite", handleEmojiSprite);
  server.on("/emoji/queue", HTTP_POST, handleEmojiQueue);
  server.on("/emoji/settings", handleEmojiSettings);
  server.on("/emoji/clear", handleEmojiClear);

  server.begin();
}

#endif
