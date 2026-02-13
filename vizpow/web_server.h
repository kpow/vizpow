#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>
#include <Preferences.h>
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
extern void resetEffectShuffle();
extern uint8_t currentMode;
extern CRGBPalette16 currentPalette;

// WiFi STA externs
extern Preferences preferences;
extern char wifiStaSSID[];
extern char wifiStaPassword[];
extern bool staConnected;
extern bool ntpSynced;
extern unsigned long lastNTPRetry;

// Emoji-related externs (defined in effects_emoji.h)
extern uint8_t emojiQueueCount;
extern uint16_t emojiCycleTime;
extern uint16_t emojiFadeDuration;
extern bool emojiAutoCycle;
void clearEmojiQueue();
bool addEmojiToQueue(const char* hexData);
bool addEmojiByIndex(uint8_t spriteIndex);
void setEmojiSettings(uint16_t cycleTime, uint16_t fadeDuration, bool autoCycle);


// Web interface HTML
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>VizPow</title>
  <style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:-apple-system,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;min-height:100vh;padding:20px}h1{text-align:center;margin-bottom:20px;font-size:24px}h2{font-size:14px;color:#888;margin-bottom:10px;text-transform:uppercase}.card{background:rgba(255,255,255,.1);border-radius:16px;padding:20px;margin-bottom:16px}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}button{background:rgba(255,255,255,.15);border:none;color:#fff;padding:14px 10px;border-radius:12px;font-size:13px;cursor:pointer}button.active{background:#6366f1}.tab-row{display:flex;gap:10px;margin-bottom:15px}.tab{flex:1;background:rgba(255,255,255,.1);border:none;color:#888;padding:12px;border-radius:10px;font-size:14px;cursor:pointer}.tab.active{background:#6366f1;color:#fff}.slider-container{margin:15px 0}.slider-label{display:flex;justify-content:space-between;margin-bottom:8px}input[type=range]{width:100%;height:8px;border-radius:4px;background:rgba(255,255,255,.2);-webkit-appearance:none}input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;border-radius:50%;background:#6366f1;cursor:pointer}.toggle-row{display:flex;justify-content:space-between;align-items:center;padding:10px 0}.toggle{width:52px;height:32px;background:rgba(255,255,255,.2);border-radius:16px;position:relative;cursor:pointer}.toggle.on{background:#6366f1}.toggle::after{content:'';position:absolute;width:26px;height:26px;background:#fff;border-radius:50%;top:3px;left:3px;transition:transform .3s}.toggle.on::after{transform:translateX(20px)}.status{text-align:center;color:#888;font-size:12px;margin-top:10px}.hidden{display:none}#emojiGrid{max-height:300px;overflow-y:auto;margin-bottom:15px}#emojiQueue{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:15px;min-height:40px}.queueItem{background:rgba(99,102,241,.5);padding:8px 12px;border-radius:8px;font-size:12px}.queueEmpty{color:#666;font-style:italic;padding:10px}.btn-row{display:flex;gap:10px;margin-top:10px}.btn-row button{flex:1}.btn-danger{background:rgba(239,68,68,.3)}</style>
</head>
<body>
  <h1>VizPow Control</h1>

  <div class="card">
    <div class="tab-row">
      <button class="tab active" id="tabMotion" onclick="setMode(0)">Motion</button>
      <button class="tab" id="tabAmbient" onclick="setMode(1)">Ambient</button>
      <button class="tab" id="tabEmoji" onclick="setMode(2)">Emoji</button>
      <button class="tab" id="tabBot" onclick="setMode(3)">Bot</button>
    </div>

    <div id="effectsPanel">
      <div class="grid" id="effects"></div>
    </div>

    <div id="emojiPanel" class="hidden">
      <h2>Select Emoji</h2>
      <div class="grid" id="emojiGrid"></div>
      <h2>Queue (<span id="queueCount">0</span>/16)</h2>
      <div id="emojiQueue"><span class="queueEmpty">Click an emoji above to add</span></div>
      <div class="btn-row">
        <button class="btn-danger" onclick="clearQueue()">Clear Queue</button>
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

    <div id="botPanel" class="hidden">
      <h2>Expressions</h2>
      <div class="grid" id="botExpressions"></div>
      <h2 style="margin-top:15px">Say Something</h2>
      <div style="display:flex;gap:8px">
        <input type="text" id="botSayInput" placeholder="Type a message..."
          style="flex:1;padding:10px;border-radius:8px;border:none;background:rgba(255,255,255,0.15);color:#fff;font-size:14px" maxlength="30">
        <button onclick="sendBotSay()" style="padding:10px 16px">Say</button>
      </div>
      <h2 style="margin-top:15px">Personality</h2>
      <div class="grid4" id="botPersonalities"></div>
      <h2 style="margin-top:15px">Face Color</h2>
      <div class="grid4" id="botColors"></div>
      <div class="toggle-row" style="margin-top:12px">
        <span>Time Overlay</span>
        <div class="toggle" id="botTimeToggle" onclick="toggleBotTime()"></div>
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

  <div class="status">Connected to VizPow</div>

  <script>
    const motionEffects = ["Tilt Ball", "Motion Plasma", "Shake Sparkle", "Tilt Wave", "Tilt Ripple", "Gyro Swirl", "Shake Explode"];
    const ambientEffects = ["Plasma", "Rainbow", "Fire", "Ocean", "Sparkle", "Matrix", "Lava", "Aurora", "Confetti", "Comet", "Galaxy", "Heart", "Donut"];
    const palettes = ["Rainbow", "Ocean", "Lava", "Forest", "Party", "Heat", "Cloud", "Sunset", "Cyber", "Toxic", "Ice", "Blood", "Vaporwave", "Forest2", "Gold"];
    const botExprNames = ["Neutral", "Happy", "Sad", "Surprised", "Sleepy", "Angry", "Love", "Dizzy", "Thinking", "Excited", "Mischief", "Dead", "Skeptical", "Worried", "Confused", "Proud", "Shy", "Annoyed", "Bliss", "Focused"];
    const botPersonalityNames = ["Chill", "Hyper", "Grumpy", "Sleepy"];
    const botColorNames = ["White", "Cyan", "Green", "Pink", "Yellow"];
    let curPersonality = 0;

    let state = { effect: 0, palette: 0, brightness: 15, speed: 20, autoCycle: false, currentMode: 0 };
    let emojiQueue = [];
    let emojiAutoCycle = true;

    // Icon names matching emoji_sprites.h (41 icons)
    const emojiNames = [
      "Heart", "Star", "Smiley", "Check",
      "X", "Question", "Exclaim", "Sun",
      "Moon", "Cloud", "Rain", "Lightning",
      "Fire", "Snow", "Tree", "Coin",
      "Key", "Gem", "Potion", "Sword",
      "Shield", "ArrowUp", "ArrowDown", "ArrowLeft",
      "ArrowRight", "Skull", "Ghost", "Alien",
      "Pacman", "PacGhost", "ShyGuy", "Music", "WiFi", "Rainbow", "Mushroom", "Skelly", "chicken", "invader", "dragon", "twinkleheart", "popsicle"];

    function render() {
      const effects = state.currentMode === 0 ? motionEffects : ambientEffects;
      document.getElementById('tabMotion').className = 'tab ' + (state.currentMode === 0 ? 'active' : '');
      document.getElementById('tabAmbient').className = 'tab ' + (state.currentMode === 1 ? 'active' : '');
      document.getElementById('tabEmoji').className = 'tab ' + (state.currentMode === 2 ? 'active' : '');
      document.getElementById('tabBot').className = 'tab ' + (state.currentMode === 3 ? 'active' : '');

      const isBot = state.currentMode === 3;
      const isEmoji = state.currentMode === 2;
      document.getElementById('effectsPanel').className = (isEmoji || isBot) ? 'hidden' : '';
      document.getElementById('emojiPanel').className = isEmoji ? '' : 'hidden';
      document.getElementById('botPanel').className = isBot ? '' : 'hidden';
      document.getElementById('paletteCard').className = (isEmoji || isBot) ? 'card hidden' : 'card';

      if (isBot) {
        document.getElementById('botExpressions').innerHTML = botExprNames.map((name, i) =>
          `<button onclick="setBotExpr(${i})">${name}</button>`
        ).join('');
        document.getElementById('botPersonalities').innerHTML = botPersonalityNames.map((name, i) =>
          `<button class="${curPersonality === i ? 'active' : ''}" onclick="setBotPers(${i})">${name}</button>`
        ).join('');
        document.getElementById('botColors').innerHTML = botColorNames.map((name, i) =>
          `<button onclick="setBotColor(${i})">${name}</button>`
        ).join('');
      } else if (!isEmoji) {
        document.getElementById('effects').innerHTML = effects.map((e, i) =>
          `<button class="${state.effect === i ? 'active' : ''}" onclick="setEffect(${i})">${e}</button>`
        ).join('');
      } else {
        document.getElementById('emojiGrid').innerHTML = emojiNames.map((name, i) =>
          `<button onclick="addEmoji(${i})">${name}</button>`
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

      container.innerHTML = emojiQueue.map((idx, i) =>
        `<button class="queueItem" onclick="removeFromQueue(${i})">${emojiNames[idx]}</button>`
      ).join('');
    }

    function removeFromQueue(index) {
      emojiQueue.splice(index, 1);
      renderEmojiQueue();
      api('/emoji/clear');
      emojiQueue.forEach(idx => api('/emoji/add?v=' + idx));
    }

    function addEmoji(index) {
      if (emojiQueue.length >= 16) {
        alert('Queue full (max 16)');
        return;
      }
      emojiQueue.push(index);
      renderEmojiQueue();
      api('/emoji/add?v=' + index);
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
    function setBotPers(i) { curPersonality = i; render(); api('/bot/personality?v=' + i); }
    function setBotColor(i) { api('/bot/background?v=' + i); }

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

    function clearQueue() {
      emojiQueue = [];
      renderEmojiQueue();
      api('/emoji/clear');
    }

    async function getState() {
      try {
        const r = await fetch('/state');
        state = await r.json();
        render();
      } catch(e) {}
    }

    getState();
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

// Bot mode — functions defined in bot_mode.h (included before this file)
#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
// bot_faces.h, bot_eyes.h, bot_mode.h already included via vizpow.ino
#endif

void handleMode() {
  if (server.hasArg("v")) {
    uint8_t newMode = constrain(server.arg("v").toInt(), 0, NUM_MODES - 1);
    #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
      if (currentMode == MODE_BOT && newMode != MODE_BOT) exitBotMode();
      if (newMode == MODE_BOT && currentMode != MODE_BOT) enterBotMode();
    #else
      if (newMode == MODE_BOT) newMode = MODE_MOTION;  // No bot on non-LCD
    #endif
    currentMode = newMode;
    effectIndex = 0;
    resetEffectShuffle();
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
void handleEmojiAdd() {
  if (server.hasArg("v")) {
    uint8_t spriteIndex = server.arg("v").toInt();
    addEmojiByIndex(spriteIndex);
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

// Bot mode handlers
#if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
void handleBotExpression() {
  if (server.hasArg("v")) {
    uint8_t expr = constrain(server.arg("v").toInt(), 0, BOT_NUM_EXPRESSIONS - 1);
    setBotExpression(expr);
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
    showBotSaying(text.c_str(), dur);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotTime() {
  if (server.hasArg("v")) {
    if (server.arg("v").toInt() == 2) {
      toggleBotTimeOverlay();
    } else {
      // Explicit on/off
      botMode.timeOverlay.enabled = (server.arg("v").toInt() == 1);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleBotPersonality() {
  if (server.hasArg("v")) {
    uint8_t p = constrain(server.arg("v").toInt(), 0, BOT_NUM_PERSONALITIES - 1);
    setBotPersonality(p);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotBackground() {
  if (server.hasArg("v")) {
    // Map index to color: 0=white, 1=cyan, 2=green, 3=magenta, 4=yellow
    uint16_t colors[] = { 0xFFFF, 0x07FF, 0x07E0, 0xF81F, 0xFFE0 };
    uint8_t idx = constrain(server.arg("v").toInt(), 0, 4);
    setBotFaceColor(colors[idx]);
  }
  if (server.hasArg("style")) {
    uint8_t style = constrain(server.arg("style").toInt(), 0, 4);
    setBotBackgroundStyle(style);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotWeather() {
  if (server.hasArg("v")) {
    setBotWeatherEnabled(server.arg("v").toInt() == 1);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotWeatherConfig() {
  if (server.hasArg("lat") && server.hasArg("lon")) {
    float lat = server.arg("lat").toFloat();
    float lon = server.arg("lon").toFloat();
    setBotWeatherLocation(lat, lon);
  }
  server.send(200, "text/plain", "OK");
}

void handleBotState() {
  // Cloud-ready full bot state JSON
  String json = "{\"expression\":" + String(getBotExpression()) +
                ",\"state\":" + String(getBotState()) +
                ",\"personality\":" + String(getBotPersonality()) +
                ",\"timeOverlay\":" + (isBotTimeOverlayEnabled() ? "true" : "false") +
                ",\"faceColor\":" + String(botFaceColor) +
                ",\"background\":" + String(getBotBackgroundStyle()) +
                ",\"numExpressions\":" + String(BOT_NUM_EXPRESSIONS) +
                ",\"numPersonalities\":" + String(BOT_NUM_PERSONALITIES) +
                ",\"speechBubbleActive\":" + (botMode.speechBubble.active ? "true" : "false") +
                "}";
  server.send(200, "application/json", json);
}
#endif

// Setup all web server routes
// WiFi STA configuration endpoint
void handleWifiConfig() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    // Save to NVS
    preferences.begin("vizpow", false);
    preferences.putString("sta_ssid", ssid);
    preferences.putString("sta_pass", pass);
    preferences.end();

    // Update runtime credentials
    ssid.toCharArray(wifiStaSSID, 33);
    pass.toCharArray(wifiStaPassword, 65);

    // Disconnect and reconnect with new credentials
    WiFi.disconnect(false);
    staConnected = false;
    ntpSynced = false;
    lastNTPRetry = millis();
    delay(200);
    WiFi.begin(wifiStaSSID, wifiStaPassword);

    server.send(200, "application/json", "{\"ok\":true,\"ssid\":\"" + ssid + "\"}");
  } else {
    String json = "{\"ssid\":\"" + String(wifiStaSSID) + "\",\"connected\":" +
                  (staConnected ? "true" : "false") + ",\"ntp\":" +
                  (ntpSynced ? "true" : "false") + "}";
    server.send(200, "application/json", json);
  }
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/mode", handleMode);
  server.on("/effect", handleEffect);
  server.on("/palette", handlePalette);
  server.on("/brightness", handleBrightness);
  server.on("/speed", handleSpeed);
  server.on("/autocycle", handleAutoCycle);
  server.on("/wifi/config", handleWifiConfig);

  // Emoji endpoints
  server.on("/emoji/add", handleEmojiAdd);
  server.on("/emoji/settings", handleEmojiSettings);
  server.on("/emoji/clear", handleEmojiClear);

  // Bot mode endpoints
  #if defined(DISPLAY_LCD_ONLY) || defined(DISPLAY_DUAL)
  server.on("/bot/expression", handleBotExpression);
  server.on("/bot/say", handleBotSay);
  server.on("/bot/time", handleBotTime);
  server.on("/bot/personality", handleBotPersonality);
  server.on("/bot/background", handleBotBackground);
  server.on("/bot/weather", handleBotWeather);
  server.on("/bot/weather/config", handleBotWeatherConfig);
  server.on("/bot/state", handleBotState);
  #endif

  server.begin();
}

#endif
