/*
 * ESP32-S3 LED Matrix Controller
 * 
 * A motion-reactive LED matrix controller for wearable displays.
 * Uses Waveshare ESP32-S3-Matrix with onboard 8x8 WS2812B and QMI8658 IMU.
 * 
 * Features:
 * - 12 motion-reactive effects (accelerometer/gyroscope driven)
 * - 14 ambient effects (no motion input)
 * - 15 color palettes
 * - WiFi AP web interface for control
 * - Adjustable brightness and speed
 * 
 * Hardware:
 * - Waveshare ESP32-S3-Matrix
 * - 8x8 WS2812B LED Matrix (GPIO14)
 * - QMI8658 6-axis IMU (I2C: SDA=GPIO11, SCL=GPIO12)
 * 
 * License: MIT
 */

#include <FastLED.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include "SensorQMI8658.hpp"

// Hardware configuration
#define NUM_LEDS 64
#define DATA_PIN 14
#define WIDTH 8
#define HEIGHT 8
#define I2C_SDA 11
#define I2C_SCL 12

// WiFi AP configuration
const char* ssid = "LED-Matrix";
const char* password = "12345678";

// Global objects
CRGB leds[NUM_LEDS];
SensorQMI8658 imu;
WebServer server(80);

// State variables
uint8_t effectIndex = 0;
uint8_t paletteIndex = 0;
uint8_t brightness = 15;
uint8_t speed = 20;
bool autoCycle = false;
bool motionMode = true;
unsigned long lastChange = 0;
unsigned long lastPaletteChange = 0;

// IMU data
float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;

// Effect counts
#define NUM_MOTION_EFFECTS 12
#define NUM_AMBIENT_EFFECTS 14
#define NUM_PALETTES 15

// Custom color palettes
DEFINE_GRADIENT_PALETTE(sunset_gp) {
  0,     255,  60,   0,
  128,   255,   0, 100,
  255,   255,  60,   0
};

DEFINE_GRADIENT_PALETTE(cyber_gp) {
  0,       0, 255, 255,
  128,   255,   0, 255,
  255,     0, 255, 255
};

DEFINE_GRADIENT_PALETTE(toxic_gp) {
  0,       0, 255,   0,
  128,   180, 255,   0,
  255,     0, 255,   0
};

DEFINE_GRADIENT_PALETTE(ice_gp) {
  0,       0, 100, 255,
  128,   100, 200, 255,
  255,     0, 100, 255
};

DEFINE_GRADIENT_PALETTE(blood_gp) {
  0,     255,   0,   0,
  128,   180,   0,   0,
  255,   255,   0,   0
};

DEFINE_GRADIENT_PALETTE(vaporwave_gp) {
  0,     255, 100, 200,
  128,   100, 200, 255,
  255,   255, 100, 200
};

DEFINE_GRADIENT_PALETTE(forest_deep_gp) {
  0,       0, 100,  20,
  128,    50, 180,  20,
  255,     0, 100,  20
};

DEFINE_GRADIENT_PALETTE(gold_gp) {
  0,     255, 180,   0,
  128,   255, 120,   0,
  255,   255, 180,   0
};

// Palette array
CRGBPalette16 palettes[] = {
  RainbowColors_p,
  OceanColors_p,
  LavaColors_p,
  ForestColors_p,
  PartyColors_p,
  HeatColors_p,
  CloudColors_p,
  sunset_gp,
  cyber_gp,
  toxic_gp,
  ice_gp,
  blood_gp,
  vaporwave_gp,
  forest_deep_gp,
  gold_gp
};

CRGBPalette16 currentPalette;

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
    const ambientEffects = ["Plasma", "Rainbow", "Fire", "Ocean", "Sparkle", "Wave", "Spiral", "Breathe", "Matrix", "Lava", "Aurora", "Confetti", "Comet", "Galaxy"];
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

// XY mapping for serpentine matrix layout
uint16_t XY(uint8_t x, uint8_t y) {
  if (y & 1) {
    return y * WIDTH + (WIDTH - 1 - x);
  } else {
    return y * WIDTH + x;
  }
}

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

// Color test at startup
void colorTest() {
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
  delay(500);
  
  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(500);
  
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(500);
  
  FastLED.clear();
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  
  // Initialize LEDs
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  
  // Run color test
  colorTest();
  
  // Initialize IMU
  Wire.begin(I2C_SDA, I2C_SCL);
  
  if (imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
    imu.configAccelerometer(
      SensorQMI8658::ACC_RANGE_4G,
      SensorQMI8658::ACC_ODR_250Hz,
      SensorQMI8658::LPF_MODE_0
    );
    imu.configGyroscope(
      SensorQMI8658::GYR_RANGE_512DPS,
      SensorQMI8658::GYR_ODR_896_8Hz,
      SensorQMI8658::LPF_MODE_0
    );
    imu.enableAccelerometer();
    imu.enableGyroscope();
    Serial.println("IMU initialized");
  } else {
    Serial.println("IMU initialization failed");
  }
  
  currentPalette = palettes[0];
  
  // Start WiFi AP
  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/mode", handleMode);
  server.on("/effect", handleEffect);
  server.on("/palette", handlePalette);
  server.on("/brightness", handleBrightness);
  server.on("/speed", handleSpeed);
  server.on("/autocycle", handleAutoCycle);
  server.begin();
  
  Serial.println("Web server started");
}

void readIMU() {
  if (imu.getDataReady()) {
    imu.getAccelerometer(accelX, accelY, accelZ);
    imu.getGyroscope(gyroX, gyroY, gyroZ);
  }
}

void loop() {
  server.handleClient();
  readIMU();
  
  int maxEffects = motionMode ? NUM_MOTION_EFFECTS : NUM_AMBIENT_EFFECTS;
  
  if (autoCycle && millis() - lastChange > 10000) {
    lastChange = millis();
    effectIndex = (effectIndex + 1) % maxEffects;
    FastLED.clear();
  }
  
  if (autoCycle && millis() - lastPaletteChange > 5000) {
    lastPaletteChange = millis();
    paletteIndex = (paletteIndex + 1) % NUM_PALETTES;
    currentPalette = palettes[paletteIndex];
  }

  if (motionMode) {
    switch (effectIndex) {
      case 0: tiltBall(); break;
      case 1: motionPlasma(); break;
      case 2: shakeSparkle(); break;
      case 3: tiltWave(); break;
      case 4: spinTrails(); break;
      case 5: gravityPixels(); break;
      case 6: motionNoise(); break;
      case 7: tiltRipple(); break;
      case 8: gyroSwirl(); break;
      case 9: shakeExplode(); break;
      case 10: tiltFire(); break;
      case 11: motionRainbow(); break;
    }
  } else {
    switch (effectIndex) {
      case 0: ambientPlasma(); break;
      case 1: ambientRainbow(); break;
      case 2: ambientFire(); break;
      case 3: ambientOcean(); break;
      case 4: ambientSparkle(); break;
      case 5: ambientWave(); break;
      case 6: ambientSpiral(); break;
      case 7: ambientBreathe(); break;
      case 8: ambientMatrix(); break;
      case 9: ambientLava(); break;
      case 10: ambientAurora(); break;
      case 11: ambientConfetti(); break;
      case 12: ambientComet(); break;
      case 13: ambientGalaxy(); break;
    }
  }

  FastLED.show();
  delay(speed);
}

// ==================== MOTION EFFECTS ====================

void tiltBall() {
  static float ballX = 3.5, ballY = 3.5;
  
  float targetX = 3.5 + accelX * 3.5;
  float targetY = 3.5 - accelY * 3.5;
  
  ballX += (targetX - ballX) * 0.3;
  ballY += (targetY - ballY) * 0.3;
  
  ballX = constrain(ballX, 0, 7);
  ballY = constrain(ballY, 0, 7);
  
  fadeToBlackBy(leds, NUM_LEDS, 100);
  
  int ix = (int)ballX;
  int iy = (int)ballY;
  
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      int nx = ix + dx;
      int ny = iy + dy;
      if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
        float dist = sqrt((ballX - nx) * (ballX - nx) + (ballY - ny) * (ballY - ny));
        uint8_t bright = 255 - constrain(dist * 150, 0, 255);
        leds[XY(nx, ny)] = ColorFromPalette(currentPalette, millis() / 20, bright);
      }
    }
  }
}

void motionPlasma() {
  static uint16_t t = 0;
  float motion = sqrt(gyroX * gyroX + gyroY * gyroY + gyroZ * gyroZ);
  t += 1 + (motion / 50);
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t value = sin8(x * 32 + t) + sin8(y * 32 + t) + sin8((x + y) * 16 + t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, value);
    }
  }
}

void shakeSparkle() {
  float shake = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  fadeToBlackBy(leds, NUM_LEDS, 20);
  
  if (shake > 1.5) {
    int numSparks = constrain((shake - 1) * 10, 1, 20);
    for (int i = 0; i < numSparks; i++) {
      int pos = random16(NUM_LEDS);
      leds[pos] = ColorFromPalette(currentPalette, random8(), 255);
    }
  }
}

void tiltWave() {
  static uint8_t t = 0;
  t++;
  
  float angle = atan2(accelY, accelX);
  
  FastLED.clear();
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      float projected = x * cos(angle) + y * sin(angle);
      uint8_t bright = sin8(projected * 40 + t * 3);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, projected * 20 + t, bright);
    }
  }
}

void spinTrails() {
  static float angle = 0;
  angle += gyroZ / 500.0;
  
  fadeToBlackBy(leds, NUM_LEDS, 30);
  
  for (float r = 0; r < 4; r += 0.5) {
    int x = 3.5 + cos(angle + r) * r;
    int y = 3.5 + sin(angle + r) * r;
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
      leds[XY(x, y)] = ColorFromPalette(currentPalette, r * 50 + millis() / 20);
    }
  }
}

void gravityPixels() {
  static float px[8], py[8], vx[8], vy[8];
  static bool init = false;
  
  if (!init) {
    for (int i = 0; i < 8; i++) {
      px[i] = random8(WIDTH);
      py[i] = random8(HEIGHT);
      vx[i] = 0;
      vy[i] = 0;
    }
    init = true;
  }
  
  FastLED.clear();
  
  for (int i = 0; i < 8; i++) {
    vx[i] += accelX * 0.2;
    vy[i] -= accelY * 0.2;
    vx[i] *= 0.95;
    vy[i] *= 0.95;
    
    px[i] += vx[i];
    py[i] += vy[i];
    
    if (px[i] < 0) { px[i] = 0; vx[i] *= -0.5; }
    if (px[i] > 7) { px[i] = 7; vx[i] *= -0.5; }
    if (py[i] < 0) { py[i] = 0; vy[i] *= -0.5; }
    if (py[i] > 7) { py[i] = 7; vy[i] *= -0.5; }
    
    leds[XY((int)px[i], (int)py[i])] = ColorFromPalette(currentPalette, i * 30 + millis() / 20);
  }
}

void motionNoise() {
  static uint16_t t = 0;
  t += 3;
  
  int offsetX = accelX * 50;
  int offsetY = accelY * 50;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t n = inoise8((x * 50) + offsetX, (y * 50) + offsetY, t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, n);
    }
  }
}

void tiltRipple() {
  static uint8_t t = 0;
  t++;
  
  float cx = 3.5 + accelX * 2;
  float cy = 3.5 - accelY * 2;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      float dx = x - cx;
      float dy = y - cy;
      float dist = sqrt(dx * dx + dy * dy);
      uint8_t bright = sin8((dist * 50) - t * 4);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, dist * 30 + t, bright);
    }
  }
}

void gyroSwirl() {
  static uint16_t t = 0;
  t += 2 + abs(gyroZ) / 100;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      float dx = x - 3.5;
      float dy = y - 3.5;
      float angle = atan2(dy, dx);
      float dist = sqrt(dx * dx + dy * dy);
      uint8_t hue = (angle * 40) + (dist * 20) + t;
      leds[XY(x, y)] = ColorFromPalette(currentPalette, hue);
    }
  }
}

void shakeExplode() {
  static uint8_t explodeFrame = 255;
  static uint8_t explodeHue = 0;
  
  float shake = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  
  if (shake > 2.5 && explodeFrame >= 50) {
    explodeFrame = 0;
    explodeHue = random8();
  }
  
  if (explodeFrame < 50) {
    float radius = explodeFrame / 5.0;
    FastLED.clear();
    
    for (uint8_t x = 0; x < WIDTH; x++) {
      for (uint8_t y = 0; y < HEIGHT; y++) {
        float dx = x - 3.5;
        float dy = y - 3.5;
        float dist = sqrt(dx * dx + dy * dy);
        if (abs(dist - radius) < 1.5) {
          uint8_t bright = 255 - abs(dist - radius) * 170;
          leds[XY(x, y)] = ColorFromPalette(currentPalette, explodeHue + dist * 20, bright);
        }
      }
    }
    explodeFrame++;
  } else {
    fadeToBlackBy(leds, NUM_LEDS, 30);
  }
}

void tiltFire() {
  static uint8_t heat[64];
  
  int hotSpot = constrain(3.5 + accelX * 2, 0, 7);
  
  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8(heat[i], random8(0, 20));
  }
  
  for (int x = 0; x < WIDTH; x++) {
    int intensity = 255 - abs(x - hotSpot) * 50;
    if (random8() < 180 && intensity > 0) {
      heat[XY(x, HEIGHT - 1)] = qadd8(heat[XY(x, HEIGHT - 1)], random8(intensity / 2, intensity));
    }
  }
  
  for (int y = 0; y < HEIGHT - 1; y++) {
    for (int x = 0; x < WIDTH; x++) {
      heat[XY(x, y)] = (heat[XY(x, y)] + heat[XY(x, y + 1)] + heat[XY(x, y + 1)]) / 3;
    }
  }
  
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(currentPalette, heat[i]);
  }
}

void motionRainbow() {
  static int16_t hueOffset = 0;
  
  hueOffset += gyroZ / 100;
  float spd = 1 + sqrt(accelX * accelX + accelY * accelY) * 2;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t hue = hueOffset + (x * 8) + (y * 8);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, hue);
    }
  }
  
  hueOffset += spd;
}

// ==================== AMBIENT EFFECTS ====================

void ambientPlasma() {
  static uint16_t t = 0;
  t += 2;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t value = sin8(x * 32 + t) + sin8(y * 32 + t) + sin8((x + y) * 16 + t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, value);
    }
  }
}

void ambientRainbow() {
  static uint8_t hue = 0;
  hue++;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      leds[XY(x, y)] = ColorFromPalette(currentPalette, hue + (x * 8) + (y * 8));
    }
  }
}

void ambientFire() {
  static uint8_t heat[64];
  
  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8(heat[i], random8(0, 20));
  }
  
  for (int x = 0; x < WIDTH; x++) {
    if (random8() < 180) {
      heat[XY(x, HEIGHT - 1)] = qadd8(heat[XY(x, HEIGHT - 1)], random8(150, 255));
    }
  }
  
  for (int y = 0; y < HEIGHT - 1; y++) {
    for (int x = 0; x < WIDTH; x++) {
      heat[XY(x, y)] = (heat[XY(x, y)] + heat[XY(x, y + 1)] + heat[XY(x, y + 1)]) / 3;
    }
  }
  
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(currentPalette, heat[i]);
  }
}

void ambientOcean() {
  static uint16_t t = 0;
  t += 3;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t n = inoise8(x * 50, y * 50, t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, n);
    }
  }
}

void ambientSparkle() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  int pos = random16(NUM_LEDS);
  leds[pos] = ColorFromPalette(currentPalette, random8(), 255);
}

void ambientWave() {
  static uint8_t t = 0;
  t++;
  
  FastLED.clear();
  for (uint8_t x = 0; x < WIDTH; x++) {
    uint8_t y = (sin8(x * 32 + t) * (HEIGHT - 1)) / 255;
    leds[XY(x, y)] = ColorFromPalette(currentPalette, t + x * 10);
  }
}

void ambientSpiral() {
  static uint8_t hue = 0;
  hue++;
  fadeToBlackBy(leds, NUM_LEDS, 15);
  
  float angle = millis() / 200.0;
  float radius = (sin8(millis() / 10) / 255.0) * 3.5;
  int x = 3.5 + cos(angle) * radius;
  int y = 3.5 + sin(angle) * radius;
  
  if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
    leds[XY(x, y)] = ColorFromPalette(currentPalette, hue);
  }
}

void ambientBreathe() {
  uint8_t bright = sin8(millis() / 10);
  fill_solid(leds, NUM_LEDS, ColorFromPalette(currentPalette, millis() / 50, bright));
}

void ambientMatrix() {
  static uint8_t drops[WIDTH];
  static bool init = false;
  
  if (!init) {
    for (int i = 0; i < WIDTH; i++) drops[i] = random8(HEIGHT);
    init = true;
  }
  
  fadeToBlackBy(leds, NUM_LEDS, 40);
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    drops[x] = (drops[x] + 1) % (HEIGHT + random8(3));
    if (drops[x] < HEIGHT) {
      leds[XY(x, drops[x])] = ColorFromPalette(currentPalette, 100, 255);
      if (drops[x] > 0) {
        leds[XY(x, drops[x] - 1)] = ColorFromPalette(currentPalette, 100, 150);
      }
    }
  }
}

void ambientLava() {
  static uint16_t t = 0;
  t += 3;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t n = inoise8(x * 60, y * 60, t);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, n);
    }
  }
}

void ambientAurora() {
  static uint16_t t = 0;
  t += 2;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      uint8_t n = inoise8(x * 40, y * 30 + t, t / 2);
      leds[XY(x, y)] = ColorFromPalette(currentPalette, n);
    }
  }
}

void ambientConfetti() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += ColorFromPalette(currentPalette, random8(64) + millis() / 50, 255);
}

void ambientComet() {
  static uint8_t pos = 0;
  static uint8_t hue = 0;
  
  fadeToBlackBy(leds, NUM_LEDS, 40);
  
  EVERY_N_MILLISECONDS(50) {
    pos = (pos + 1) % NUM_LEDS;
    hue++;
  }
  
  leds[pos] = ColorFromPalette(currentPalette, hue);
}

void ambientGalaxy() {
  static uint16_t t = 0;
  t++;
  
  for (uint8_t x = 0; x < WIDTH; x++) {
    for (uint8_t y = 0; y < HEIGHT; y++) {
      float dx = x - 3.5;
      float dy = y - 3.5;
      float angle = atan2(dy, dx);
      float dist = sqrt(dx * dx + dy * dy);
      uint8_t hue = (angle * 40) + (dist * 20) + t;
      uint8_t val = 255 - dist * 20;
      leds[XY(x, y)] = ColorFromPalette(currentPalette, hue, val);
    }
  }
}
