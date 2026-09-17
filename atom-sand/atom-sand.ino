// ============================================================================
// Atom Matrix — Gravity Sand Toy
// ============================================================================
// Standalone firmware for the M5 Atom Matrix (ESP32-PICO-D4, 5x5 WS2812 RGB,
// Bosch BMI270 IMU, one face button, battery pack).
//
// Lit "grains" tumble toward whichever way the board is tilted. Falling-sand
// physics driven by the IMU accelerometer.
//
// Controls:
//   short button press  -> cycle color palette
//   long  button press  -> reset (re-drop the grains from the top)
//   shake the board     -> scramble grain positions + colors
//
// The IMU on this hardware revision is a BMI270 (not the older MPU6886). It
// needs an ~8KB config blob uploaded before it returns data, so we let
// M5Unified own the IMU + face button. FastLED still drives the 5x5 matrix
// directly — M5GFX has no panel driver for the WS2812 array, so nothing
// contends for GPIO27.
//
// Bring-up calibration lives in the CALIBRATION block below — if the sand flows
// the wrong way or the grid is mirrored, flip one constant there.
// ============================================================================

#include <M5Unified.h>
#include <FastLED.h>

// ---------------------------------------------------------------------------
// Hardware (M5 Atom Matrix)
// ---------------------------------------------------------------------------
#define LED_PIN      27          // WS2812 data
#define NUM_LEDS     25          // 5x5
#define GRID         5

// ---------------------------------------------------------------------------
// CALIBRATION — adjust only these if orientation/tilt is wrong at first flash
// ---------------------------------------------------------------------------
// Grid orientation: how (x,y) maps onto the physical LED index. The Atom's
// "up" depends on which way you hold it (USB-C port side). Flip these until
// the picture is upright.
#define FLIP_X   false
#define FLIP_Y   false
#define SWAP_XY  false
// Gravity mapping: which accel axis pushes grains along grid-x / grid-y, and
// the sign. If sand flows the opposite way you tilt, negate the matching gain.
// (On this board, left/right tilt lives on accel-X, up/down on accel-Y.)
#define GRAV_X_FROM_AX   1.0f    // grid-x (right) gravity = GRAV_X_FROM_AX * accelX
#define GRAV_Y_FROM_AY   1.0f    // grid-y (down)  gravity = GRAV_Y_FROM_AY * accelY

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
#define NUM_GRAINS       10      // of 25 cells — leaves room to flow
#define BRIGHTNESS       20
#define MAX_MILLIAMPS    250     // real power cap (battery + USB safety)
#define STEP_MS          50      // physics tick ~20 FPS
#define REST_THRESH      0.18f   // in-plane |g| below this = grains rest
#define SHAKE_THRESH     2.0f    // total accel (g) to count as a shake
#define SHAKE_COOLDOWN   600     // ms between shake triggers
#define ACCEL_EMA        0.30f   // low-pass weight on new samples

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
CRGB leds[NUM_LEDS];

struct Grain { int8_t x, y; uint8_t hue; };
Grain grains[NUM_GRAINS];
bool  occ[GRID][GRID];           // occupancy grid, rebuilt each tick

float ax = 0, ay = 0, az = 1;    // EMA-smoothed accel, in g
uint8_t palette = 0;
const uint8_t NUM_PALETTES = 4;

uint32_t lastStep = 0;
uint32_t lastShake = 0;

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------
static uint16_t ledIndex(int x, int y) {
  if (SWAP_XY) { int t = x; x = y; y = t; }
  if (FLIP_X)  x = GRID - 1 - x;
  if (FLIP_Y)  y = GRID - 1 - y;
  return (uint16_t)(y * GRID + x);
}

static void rebuildOcc() {
  for (int y = 0; y < GRID; y++)
    for (int x = 0; x < GRID; x++) occ[y][x] = false;
  for (int i = 0; i < NUM_GRAINS; i++) occ[grains[i].y][grains[i].x] = true;
}

// Assign grain colors for the current palette.
static void reHue() {
  for (int i = 0; i < NUM_GRAINS; i++) {
    switch (palette) {
      case 0: grains[i].hue = (uint8_t)(i * (256 / NUM_GRAINS)); break;   // rainbow spread
      case 1: grains[i].hue = random(0, 42);                     break;   // warm (reds/oranges)
      case 2: grains[i].hue = random(96, 160);                   break;   // cool (green/blue)
      default: grains[i].hue = random(0, 256);                   break;   // candy
    }
  }
}

// Drop all grains into the top rows, packed left-to-right.
static void resetGrains() {
  for (int i = 0; i < NUM_GRAINS; i++) {
    grains[i].x = i % GRID;
    grains[i].y = i / GRID;
  }
  reHue();
}

// Random splat of grains across the grid (no overlaps).
static void scrambleGrains() {
  bool used[GRID][GRID];
  for (int y = 0; y < GRID; y++)
    for (int x = 0; x < GRID; x++) used[y][x] = false;
  for (int i = 0; i < NUM_GRAINS; i++) {
    int x, y;
    do { x = random(GRID); y = random(GRID); } while (used[y][x]);
    used[y][x] = true;
    grains[i].x = x;
    grains[i].y = y;
  }
  reHue();
}

// ---------------------------------------------------------------------------
// Physics — one falling-sand tick
// ---------------------------------------------------------------------------
static void stepSand() {
  // In-plane gravity from the tilt (see CALIBRATION block for axis mapping).
  float gx = GRAV_X_FROM_AX * ax;
  float gy = GRAV_Y_FROM_AY * ay;
  float mag = sqrtf(gx * gx + gy * gy);
  if (mag < REST_THRESH) return;         // board near-flat: grains rest

  // Quantize to an 8-way direction (0.383 = sin 22.5deg sector boundary).
  float nx = gx / mag, ny = gy / mag;
  int dx = (nx > 0.383f) ? 1 : (nx < -0.383f) ? -1 : 0;
  int dy = (ny > 0.383f) ? 1 : (ny < -0.383f) ? -1 : 0;
  if (dx == 0 && dy == 0) return;

  // Two "slip" moves let piles spread and flow when blocked head-on.
  int sx1, sy1, sx2, sy2;
  if (dx != 0 && dy != 0) {              // diagonal gravity -> slip along each axis
    sx1 = dx; sy1 = 0;  sx2 = 0;  sy2 = dy;
  } else if (dx == 0) {                  // vertical gravity -> slip to the two diagonals
    sx1 = -1; sy1 = dy; sx2 = 1;  sy2 = dy;
  } else {                               // horizontal gravity -> slip to the two diagonals
    sx1 = dx; sy1 = -1; sx2 = dx; sy2 = 1;
  }
  if (random(2)) { int tx = sx1, ty = sy1; sx1 = sx2; sy1 = sy2; sx2 = tx; sy2 = ty; }

  rebuildOcc();

  // Process grains furthest downhill first so a grain never blocks one behind it.
  static uint8_t order[NUM_GRAINS];
  for (int i = 0; i < NUM_GRAINS; i++) order[i] = i;
  for (int i = 1; i < NUM_GRAINS; i++) {         // insertion sort by downhill projection
    uint8_t k = order[i];
    int pk = grains[k].x * dx + grains[k].y * dy;
    int j = i - 1;
    while (j >= 0 && (grains[order[j]].x * dx + grains[order[j]].y * dy) < pk) {
      order[j + 1] = order[j]; j--;
    }
    order[j + 1] = k;
  }

  const int cx[3] = { dx, sx1, sx2 };
  const int cy[3] = { dy, sy1, sy2 };
  for (int oi = 0; oi < NUM_GRAINS; oi++) {
    Grain &g = grains[order[oi]];
    occ[g.y][g.x] = false;                       // vacate before testing
    int nxp = g.x, nyp = g.y;
    for (int c = 0; c < 3; c++) {
      int tx = g.x + cx[c], ty = g.y + cy[c];
      if (tx < 0 || tx >= GRID || ty < 0 || ty >= GRID) continue;
      if (!occ[ty][tx]) { nxp = tx; nyp = ty; break; }
    }
    g.x = nxp; g.y = nyp;
    occ[g.y][g.x] = true;
  }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
static void render() {
  FastLED.clear();
  for (int i = 0; i < NUM_GRAINS; i++)
    leds[ledIndex(grains[i].x, grains[i].y)] = CHSV(grains[i].hue, 255, 255);
  FastLED.show();
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;               // BMI270 — M5Unified uploads its config blob
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.printf("Atom Matrix — Gravity Sand Toy | IMU enabled: %d\n",
                (int)M5.Imu.isEnabled());

  randomSeed(esp_random());

  // FastLED after M5.begin so it owns GPIO27's RMT config.
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  FastLED.clear(true);

  resetGrains();
}

void loop() {
  M5.update();                           // services BtnA + IMU

  float sx = 0, sy = 0, sz = 0;
  M5.Imu.getAccel(&sx, &sy, &sz);        // g
  ax += (sx - ax) * ACCEL_EMA;
  ay += (sy - ay) * ACCEL_EMA;
  az += (sz - az) * ACCEL_EMA;

  // Shake gesture (raw sample, not smoothed).
  float total = sqrtf(sx * sx + sy * sy + sz * sz);
  if (total > SHAKE_THRESH && millis() - lastShake > SHAKE_COOLDOWN) {
    lastShake = millis();
    scrambleGrains();
    Serial.println("shake -> scramble");
  }

  // Button: short click cycles palette, hold re-drops the sand.
  if (M5.BtnA.wasClicked()) {
    palette = (palette + 1) % NUM_PALETTES;
    reHue();
    Serial.printf("palette %u\n", palette);
  }
  if (M5.BtnA.wasHold()) {
    resetGrains();
    Serial.println("reset");
  }

  // Physics + render on a fixed tick.
  if (millis() - lastStep >= STEP_MS) {
    lastStep = millis();
    stepSand();
    render();
  }

  // Live IMU debug — remove once tilt is confirmed working.
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg > 500) {
    lastDbg = millis();
    Serial.printf("accel g: x=% .2f y=% .2f z=% .2f  |total|=% .2f\n",
                  sx, sy, sz, total);
  }
}
