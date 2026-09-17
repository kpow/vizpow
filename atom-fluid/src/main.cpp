// ============================================================================
// Atom Matrix — FLIP Fluid (5x5 port)
// ============================================================================
// Port of the fluid-pendant FLIP/PIC solver (grid.cpp / particle.h, unchanged)
// onto the M5 Atom Matrix: 5x5 WS2812, Bosch BMI270 IMU via M5Unified, one
// face button. Hold it and tilt — the liquid sloshes toward the low corner.
//
// Two more modes run the vendored vizpow engines on the same 5x5 FastLED
// buffer: AMBIENT (effects_ambient.h, 16 effects, auto-slideshow 8s each) and
// MOTION (effects_motion.h, 7 IMU-driven effects).
//
// Controls (single face button):
//   double-click -> next mode: fluid -> ambient -> motion -> fluid
//   single-click -> fluid: cycle color | ambient: skip effect | motion: next
//   hold         -> fluid: refill | ambient: pause/resume slideshow
// ============================================================================

#include "config.h"
#include "grid.h"
#include "particle.h"
#include "utils.h"
#include "effects_ambient.h"     // vendored vizpow engine (16 ambient effects)
#include "effects_motion.h"      // vendored vizpow IMU-driven effects (7)

#include <M5Unified.h>

// global instances
CRGB leds[NUM_LEDS];             // effects_ambient.h refers to this via extern
Grid grid;

// Globals the vizpow motion effects expect (extern'd in effects_motion.h).
CRGBPalette16 currentPalette = RainbowColors_p;
float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;
Particle particles[NUM_PARTICLES];

// gravity vector (m/s^2), refreshed from the accelerometer each frame
float gravityX = 0.0f;
float gravityY = 0.0f;

// particle radius, computed in initializeParticles()
float particleRadius = 0.0f;

// LED intensity buffers — particles splat with bilinear weights, then decay
float ledIntensity[NUM_LEDS] = {0.0f};
float ledNextIntensity[NUM_LEDS] = {0.0f};

// fixed-timestep bookkeeping
unsigned long lastStepTime = 0;

// color palette, cycled by the button
struct PalColor { uint8_t r, g, b; };
const PalColor PALETTE[] = {
  {  40, 120, 255 },  // water blue
  {   0, 220, 200 },  // cyan
  {  60, 230,  90 },  // green
  { 180,  70, 255 },  // purple
  { 255, 130,  20 },  // amber
  { 220, 220, 255 },  // pale white
};
const int NUM_COLORS = sizeof(PALETTE) / sizeof(PALETTE[0]);
int colorIndex = 0;

// ---------------------------------------------------------------------------
// Display modes and controls.
//   double-click -> next mode: fluid -> ambient -> motion -> fluid
//   single-click -> fluid: cycle color | ambient: skip effect | motion: next
//   hold         -> fluid: refill
// AMBIENT auto-advances through all effects (slideshow, AMBIENT_DWELL_MS each);
// a single-click skips ahead and resets the dwell timer.
// ---------------------------------------------------------------------------
enum Mode { MODE_FLUID = 0, MODE_AMBIENT, MODE_MOTION, MODE_COUNT };
int mode = MODE_FLUID;
int effectIndex = 0;             // 0..NUM_AMBIENT_EFFECTS-1 in MODE_AMBIENT
int motionIndex = 0;             // 0..NUM_MOTION_EFFECTS-1 in MODE_MOTION

#define NUM_MOTION_EFFECTS 7
#define AMBIENT_DWELL_MS   8000  // slideshow dwell per ambient effect
uint32_t lastEffectChange = 0;
bool ambientAutoplay = true;     // hold toggles the ambient slideshow on/off

// ---------------------------------------------------------------------------
// Particle seeding — hexagonal block, sized to the physical domain (unchanged
// from the reference; independent of render resolution).
// ---------------------------------------------------------------------------
void initializeParticles() {
  float cell_size_x = PHYSICAL_WIDTH / GRID_SIZE_X;
  float cell_size_y = PHYSICAL_HEIGHT / GRID_SIZE_Y;
  float cell_size = (cell_size_x > cell_size_y) ? cell_size_x : cell_size_y;

  particleRadius = 0.3f * cell_size;

  float dx = 1.8f * particleRadius;
  float dy = sqrtf(3.0f) / 2.0f * dx;

  float available_width = PHYSICAL_WIDTH - 2.0f * cell_size - 2.0f * particleRadius;
  float available_height = PHYSICAL_HEIGHT - 2.0f * cell_size - 2.0f * particleRadius;

  int particles_per_row = (int)(available_width / dx);
  int particles_per_col = (int)(available_height / dy);

  while (particles_per_row * particles_per_col > NUM_PARTICLES) {
    if (particles_per_row > particles_per_col) particles_per_row--;
    else                                       particles_per_col--;
  }

  float start_x = cell_size + particleRadius;
  float start_y = cell_size + particleRadius;

  int idx = 0;
  for (int j = 0; j < particles_per_col && idx < NUM_PARTICLES; j++) {
    for (int i = 0; i < particles_per_row && idx < NUM_PARTICLES; i++) {
      float offset = (j % 2 == 0) ? 0.0f : particleRadius;
      particles[idx].x_pos = start_x + i * dx + offset;
      particles[idx].y_pos = start_y + j * dy;
      particles[idx].vx = 0.0f;
      particles[idx].vy = 0.0f;
      idx++;
    }
  }
  while (idx < NUM_PARTICLES) {
    particles[idx].x_pos = start_x;
    particles[idx].y_pos = start_y;
    particles[idx].vx = 0.0f;
    particles[idx].vy = 0.0f;
    idx++;
  }
}

// ---------------------------------------------------------------------------
// Render — splat particles into the 5x5 matrix with bilinear weights.
// ---------------------------------------------------------------------------
void visualizeParticles() {
  const float cell_size_x = PHYSICAL_WIDTH / GRID_SIZE_X;
  const float cell_size_y = PHYSICAL_HEIGHT / GRID_SIZE_Y;
  const float render_min_x = cell_size_x + particleRadius;
  const float render_min_y = cell_size_y + particleRadius;
  const float render_max_x = PHYSICAL_WIDTH - cell_size_x - particleRadius;
  const float render_max_y = PHYSICAL_HEIGHT - cell_size_y - particleRadius;
  const float led_scale_x = (RENDER_W - 1) / (render_max_x - render_min_x);
  const float led_scale_y = (RENDER_H - 1) / (render_max_y - render_min_y);

  for (int i = 0; i < NUM_LEDS; i++) ledNextIntensity[i] = 0.0f;

  for (int i = 0; i < NUM_PARTICLES; i++) {
    float fx = (particles[i].x_pos - render_min_x) * led_scale_x;
    float fy = (particles[i].y_pos - render_min_y) * led_scale_y;
    fx = utils::clamp(fx, 0.0f, (float)(RENDER_W - 1));
    fy = utils::clamp(fy, 0.0f, (float)(RENDER_H - 1));

    int x0 = (int)fx, y0 = (int)fy;
    int x1 = (x0 < RENDER_W - 1) ? x0 + 1 : x0;
    int y1 = (y0 < RENDER_H - 1) ? y0 + 1 : y0;
    float tx = fx - x0, ty = fy - y0;
    float sx = 1.0f - tx, sy = 1.0f - ty;

    ledNextIntensity[y0 * RENDER_W + x0] += sx * sy;
    ledNextIntensity[y0 * RENDER_W + x1] += tx * sy;
    ledNextIntensity[y1 * RENDER_W + x1] += tx * ty;
    ledNextIntensity[y1 * RENDER_W + x0] += sx * ty;
  }

  const PalColor c = PALETTE[colorIndex];   // full-scale; global FastLED brightness dims

  for (int i = 0; i < NUM_LEDS; i++) {
    float previous = ledIntensity[i] * 0.72f;      // decay hides 5x5 quantization
    float current = ledNextIntensity[i];
    ledIntensity[i] = (current > previous) ? current : previous;

    if (ledIntensity[i] > PARTICLE_THRESHOLD) {
      float amount = utils::clamp(ledIntensity[i] / (PARTICLE_THRESHOLD * 2.5f), 0.25f, 1.0f);
      leds[i] = CRGB((uint8_t)(c.r * amount), (uint8_t)(c.g * amount), (uint8_t)(c.b * amount));
    } else if (ledIntensity[i] >= FOAM_THRESHOLD) {
      leds[i] = CRGB(60, 60, 60);                  // dim foam
    } else {
      leds[i] = CRGB::Black;
    }
  }
  FastLED.show();
}

// ---------------------------------------------------------------------------
// Gravity from the IMU (BMI270 via M5Unified). Tilt drives the in-plane
// gravity component, so the fluid rests when the board is flat.
// ---------------------------------------------------------------------------
void updateGravityFromSensor() {
  float ax = 0, ay = 0, az = 0;
  M5.Imu.getAccel(&ax, &ay, &az);          // g
  gravityX = GRAV_X_FROM_AX * ax * GRAVITY_MAGNITUDE;
  gravityY = GRAV_Y_FROM_AY * ay * GRAVITY_MAGNITUDE;
}

// ---------------------------------------------------------------------------
// One FLIP timestep (identical sequence to the reference).
// ---------------------------------------------------------------------------
void runFLIPStep() {
  for (int i = 0; i < NUM_PARTICLES; i++) {
    particles[i].addGravity(gravityX, gravityY);
    particles[i].updatePosition();
  }
  grid.pushParticlesApart(particles, NUM_PARTICLES, 2);
  for (int i = 0; i < NUM_PARTICLES; i++) grid.handleParticleCollision(&particles[i]);

  grid.resetCellTypesToAir();
  for (int i = 0; i < NUM_PARTICLES; i++) grid.markCellWithLiquid(&particles[i]);
  grid.savePreviousVelocities();
  grid.clearVelocitiesAndWeights();

  for (int i = 0; i < NUM_PARTICLES; i++) grid.transferVelocityfromParticleToGrid(&particles[i]);
  grid.normalizeGridVelocities();
  grid.restoreSolidCellVelocities();

  grid.updateParticleDensity(particles, NUM_PARTICLES);
  grid.savePreviousVelocities();
  grid.forcingIncompressibility();

  for (int i = 0; i < NUM_PARTICLES; i++) grid.transferVelocityfromGridToParticle(&particles[i]);
}

// Full-matrix flash for button feedback: pops brighter than the running
// brightness, then decays to black over ~320ms. Blocking — fine for a blip.
void flashMatrix(CRGB color) {
  const uint8_t peak = 180;                    // well above the ~40 run brightness
  fill_solid(leds, NUM_LEDS, color);
  for (int b = peak; b > 0; b -= 9) {          // 20 steps x 16ms ≈ 320ms decay
    FastLED.setBrightness((uint8_t)b);
    FastLED.show();
    delay(16);
  }
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);    // restore for the mode render
  FastLED.clear();
  FastLED.show();
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;                  // BMI270 — M5Unified uploads its config blob
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.printf("Atom Fluid | IMU enabled: %d | particles: %d\n",
                (int)M5.Imu.isEnabled(), NUM_PARTICLES);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(DEFAULT_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 250);
  FastLED.clear(true);

  initializeParticles();
  grid.initParticleSpatialHash(NUM_PARTICLES, particleRadius);
  grid.markCellWalls();

  lastStepTime = millis();
}

void loop() {
  M5.update();

  // Button:
  //   double-click -> next display mode
  //   single-click -> per-mode action (fluid: cycle color)
  //   hold         -> reset (fluid: refill)
  unsigned long now = millis();

  if (M5.BtnA.wasDoubleClicked()) {
    flashMatrix(CRGB::Red);                          // mode-change feedback
    mode = (mode + 1) % MODE_COUNT;
    if (mode == MODE_AMBIENT) lastEffectChange = now;   // start the slideshow clock
    const char* names[] = {"fluid", "ambient", "motion"};
    Serial.printf("mode -> %s\n", names[mode]);
  }
  if (M5.BtnA.wasSingleClicked()) {
    if (mode == MODE_FLUID) {
      colorIndex = (colorIndex + 1) % NUM_COLORS;
    } else if (mode == MODE_AMBIENT) {
      effectIndex = (effectIndex + 1) % NUM_AMBIENT_EFFECTS;
      lastEffectChange = now;                            // skip resets the dwell
      Serial.printf("effect -> %d\n", effectIndex);
    } else {  // MODE_MOTION
      motionIndex = (motionIndex + 1) % NUM_MOTION_EFFECTS;
      Serial.printf("motion -> %d\n", motionIndex);
    }
  }
  if (M5.BtnA.wasHold()) {
    flashMatrix(CRGB::White);                        // long-press feedback
    if (mode == MODE_FLUID) {
      initializeParticles();
    } else if (mode == MODE_AMBIENT) {
      ambientAutoplay = !ambientAutoplay;                // pause/resume slideshow
      lastEffectChange = now;
      Serial.printf("autoplay -> %d\n", ambientAutoplay);
    }
  }

  if (mode == MODE_FLUID) {
    // Fixed-timestep accumulator with catch-up clamp.
    const unsigned long stepInterval = (unsigned long)(FRAME_INTERVAL * 1000.0f);
    static unsigned long accumulated = 0;
    const unsigned long maxCatchup = stepInterval * 2;

    accumulated += now - lastStepTime;
    lastStepTime = now;
    if (accumulated > maxCatchup) accumulated = maxCatchup;

    if (accumulated >= stepInterval) {
      accumulated -= stepInterval;
      updateGravityFromSensor();
      runFLIPStep();
      visualizeParticles();
    }
  } else {
    // Ambient/motion render at ~30 FPS. Keep the fluid clock current so it
    // doesn't jump-catch-up when you switch back.
    lastStepTime = now;
    static unsigned long lastFrame = 0;
    if (now - lastFrame >= 33) {
      lastFrame = now;
      if (mode == MODE_AMBIENT) {
        if (ambientAutoplay && now - lastEffectChange >= AMBIENT_DWELL_MS) {
          effectIndex = (effectIndex + 1) % NUM_AMBIENT_EFFECTS;
          lastEffectChange = now;
        }
        runAmbientEffect(effectIndex);
      } else {  // MODE_MOTION — feed the IMU globals the effects read
        M5.Imu.getAccel(&accelX, &accelY, &accelZ);
        M5.Imu.getGyro(&gyroX, &gyroY, &gyroZ);
        runMotionEffect(motionIndex);
      }
      FastLED.show();
    }
  }
}
