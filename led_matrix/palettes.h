#ifndef PALETTES_H
#define PALETTES_H

#include <FastLED.h>

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

#endif
