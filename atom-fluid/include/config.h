// config.h — Atom Matrix (5x5) fluid port
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// LED matrix (M5 Atom Matrix: 5x5 WS2812 on GPIO27, GRB order)
// ---------------------------------------------------------------------------
#define LED_PIN    27
#define RENDER_W   5              // display columns
#define RENDER_H   5              // display rows
#define NUM_LEDS   (RENDER_W * RENDER_H)

// Vendored vizpow effects engine (effects_ambient.h) expects these names.
#define MATRIX_WIDTH   RENDER_W
#define MATRIX_HEIGHT  RENDER_H
#define NUM_AMBIENT_EFFECTS 16

// Row-major LED mapping, passed to EffectCtx.xy (must match the fluid render).
inline uint16_t XY(uint8_t x, uint8_t y) { return (uint16_t)(y * MATRIX_WIDTH + x); }

// ---------------------------------------------------------------------------
// CALIBRATION — accel axis -> gravity mapping (see updateGravityFromSensor).
// Mirrors the calibrated mapping from the atom-sand toy. If the fluid pools
// the wrong way when you tilt, negate the offending gain.
// ---------------------------------------------------------------------------
#define GRAV_X_FROM_AX   1.0f     // gravity toward +x (right)  = gain * accelX
#define GRAV_Y_FROM_AY   1.0f     // gravity toward +y (down)   = gain * accelY

// ---------------------------------------------------------------------------
// Simulation constants (carried from the 8x8 reference; solver is unchanged)
// ---------------------------------------------------------------------------
#define GRAVITY_MAGNITUDE 9.81f
#define FRAME_INTERVAL 0.023f                        // real time between frames (s)
#define SPEED_MULTIPLIER 1.2f
#define DELTA_T (FRAME_INTERVAL * SPEED_MULTIPLIER)  // physics timestep
#define FLIP_RATIO 0.9f
#define INCOMPRESSIBILITY_ITERATIONS 15
#define OVERRELAXATION 1.9f
#define K_FACTOR 1.0f

// particles — fewer than the 8x8 build (400): 5x5 needs less fill and the
// Atom's LX6 core is a touch slower. Tune up if the fluid looks sparse.
#define NUM_PARTICLES 220
#define RESTITUTION_FACTOR 0.2f
#define FRICTION_FACTOR 0.2f

// sim grid (kept at 20x20 — proven; it is downsampled to 5x5 at render time)
#define GRID_SIZE_X 20
#define GRID_SIZE_Y 20

// physical dimensions of the sim domain (meters)
#define PHYSICAL_WIDTH 0.5f
#define PHYSICAL_HEIGHT 0.5f

// visualization
#define PARTICLE_THRESHOLD 2.0f

// ---------------------------------------------------------------------------
// Appearance defaults (no NVS/button-config port; single button cycles color)
// ---------------------------------------------------------------------------
#define DEFAULT_BRIGHTNESS 40      // NeoPixel-scale (0-255); Atom LEDs are bright
#define FOAM_THRESHOLD 1            // intensity at/above which a dim "foam" pixel lights

#endif
