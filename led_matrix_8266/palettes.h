#ifndef PALETTES_H
#define PALETTES_H

#include <FastLED.h>

// Custom color palettes - designed for high contrast

DEFINE_GRADIENT_PALETTE(sunset_gp) {
  0,      20,   0,  40,    // Deep purple (dark)
  60,    255,   0,  80,    // Hot pink
  120,   255, 100,   0,    // Orange
  180,   255, 220,  50,    // Bright yellow
  220,   255,  50,   0,    // Red-orange
  255,    40,   0,  20     // Dark maroon
};

DEFINE_GRADIENT_PALETTE(cyber_gp) {
  0,       0,   0,   0,    // Black
  50,      0, 255, 255,    // Cyan
  100,     0,   0,   0,    // Black
  150,   255,   0, 255,    // Magenta
  200,   255, 255, 255,    // White flash
  230,   255,   0, 255,    // Magenta
  255,     0,   0,   0     // Black
};

DEFINE_GRADIENT_PALETTE(toxic_gp) {
  0,       0,   0,   0,    // Black
  50,      0, 255,   0,    // Bright green
  100,   150, 255,   0,    // Yellow-green
  150,   255, 255,   0,    // Yellow
  200,     0, 255,   0,    // Green
  255,     0,  50,   0     // Dark green
};

DEFINE_GRADIENT_PALETTE(ice_gp) {
  0,       0,   0,  40,    // Deep blue
  60,      0, 100, 255,    // Blue
  120,   150, 220, 255,    // Light blue
  180,   255, 255, 255,    // White
  220,   100, 180, 255,    // Sky blue
  255,     0,  20,  80     // Dark blue
};

DEFINE_GRADIENT_PALETTE(blood_gp) {
  0,       0,   0,   0,    // Black
  60,    100,   0,   0,    // Dark red
  120,   255,   0,   0,    // Bright red
  160,   255, 100,  50,    // Orange-red highlight
  200,   255,   0,   0,    // Red
  255,    40,   0,   0     // Near black
};

DEFINE_GRADIENT_PALETTE(vaporwave_gp) {
  0,      20,   0,  40,    // Dark purple
  50,    255,   0, 150,    // Hot pink
  100,   255, 100, 200,    // Light pink
  150,    50, 200, 255,    // Cyan
  200,   150,  50, 255,    // Purple
  255,    20,   0,  40     // Dark purple
};

DEFINE_GRADIENT_PALETTE(forest_deep_gp) {
  0,       0,  20,   0,    // Near black
  50,      0, 100,  20,    // Dark green
  100,    80, 200,  20,    // Bright green
  150,   200, 255,  50,    // Yellow-green highlight
  200,    20, 150,  40,    // Forest green
  255,     0,  30,  10     // Very dark green
};

DEFINE_GRADIENT_PALETTE(gold_gp) {
  0,      30,  15,   0,    // Dark brown
  60,    150,  80,   0,    // Bronze
  120,   255, 180,   0,    // Gold
  160,   255, 255, 150,    // Bright gold/white
  200,   255, 150,   0,    // Orange-gold
  255,    50,  25,   0     // Dark bronze
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

#endif
