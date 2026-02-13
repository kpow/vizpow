/**
 * 8x8 Multicolor Icons for vizPow — Palette-Indexed Format
 * For use with FastLED / ESP32-S3-Matrix
 *
 * Each icon is 64 uint8_t palette indices (8x8 grid, row-major order)
 * Use decodeIcon() to expand to CRGB[64] at runtime.
 *
 * Usage:
 *   #include "emoji_sprites.h"
 *
 *   CRGB pixels[64];
 *   decodeIcon(ALL_ICONS[index], pixels);
 *   // pixels now contains 64 CRGB values
 */

#ifndef ICONS_8X8_H
#define ICONS_8X8_H

#include <FastLED.h>

// ==================== GLOBAL COLOR PALETTE ====================
// All unique colors used across all icons. 38 entries.

#define ICON_PALETTE_SIZE 38

const CRGB iconPalette[ICON_PALETTE_SIZE] PROGMEM = {
  CRGB(  0,   0,   0),   //  0: Black (___)
  CRGB(255,   0,   0),   //  1: RED
  CRGB(255, 128,   0),   //  2: ORG
  CRGB(255, 255,   0),   //  3: YEL
  CRGB(  0, 255,   0),   //  4: GRN
  CRGB(  0, 255, 255),   //  5: CYN
  CRGB(  0,   0, 255),   //  6: BLU
  CRGB(128,   0, 255),   //  7: PUR
  CRGB(255,   0, 128),   //  8: PNK
  CRGB(255, 255, 255),   //  9: WHT
  CRGB(128, 128, 128),   // 10: GRY
  CRGB( 64,  64,  64),   // 11: DGR
  CRGB(139,  69,  19),   // 12: BRN
  CRGB(255, 200, 150),   // 13: SKN
  CRGB(180,   0,   0),   // 14: DRD
  CRGB(  0, 180,   0),   // 15: LGR
  CRGB(  0,   0, 180),   // 16: DBL
  CRGB(249,  56,   1),   // 17: GHO (ghost orange)
  CRGB(  1, 119, 251),   // 18: EYE (eye blue)
  CRGB(244,  59,   2),   // 19: SHY (shy guy red)
  CRGB(175,   6,   0),   // 20: SHD (shy guy dark)
  CRGB(255, 208, 122),   // 21: mushroom spots
  CRGB(240, 255, 255),   // 22: mushroom stem
  CRGB(188, 189, 190),   // 23: skelly shadow
  CRGB(254, 186,   0),   // 24: chicken beak
  CRGB( 48, 228, 215),   // 25: invader teal
  CRGB( 33, 131, 129),   // 26: invader dark teal
  CRGB( 17,  17,  17),   // 27: invader shadow
  CRGB(  6,  59,  88),   // 28: invader dark blue
  CRGB(223,   2,  83),   // 29: dragon horns
  CRGB(148, 123, 232),   // 30: dragon body
  CRGB(217, 185, 252),   // 31: dragon belly
  CRGB(216,  29,  85),   // 32: dragon fire
  CRGB(248, 183,   7),   // 33: twinkle star
  CRGB(241, 221, 116),   // 34: twinkle star center
  CRGB(248, 121, 247),   // 35: popsicle pink
  CRGB(255, 243, 255),   // 36: popsicle highlight
  CRGB(169, 124,  26),   // 37: popsicle stick
};

// Decode a palette-indexed icon into CRGB pixel array
inline void decodeIcon(const uint8_t* iconData, CRGB* outPixels) {
  for (uint8_t i = 0; i < 64; i++) {
    uint8_t idx = pgm_read_byte(&iconData[i]);
    if (idx < ICON_PALETTE_SIZE) {
      memcpy_P(&outPixels[i], &iconPalette[idx], sizeof(CRGB));
    } else {
      outPixels[i] = CRGB::Black;
    }
  }
}

// Palette index shortcuts for icon definitions
#define ___ 0
#define RED 1
#define ORG 2
#define YEL 3
#define GRN 4
#define CYN 5
#define BLU 6
#define PUR 7
#define PNK 8
#define WHT 9
#define GRY 10
#define DGR 11
#define BRN 12
#define SKN 13
#define DRD 14
#define LGR 15
#define DBL 16
#define GHO 17
#define EYE 18
#define SHY 19
#define SHD 20
#define MSP 21
#define MST 22
#define SKS 23
#define CBK 24
#define ITL 25
#define IDT 26
#define ISH 27
#define IDB 28
#define DRH 29
#define DRB 30
#define DRL 31
#define DRF 32
#define TWS 33
#define TWC 34
#define PPK 35
#define PHL 36
#define PST 37

// ==================== SYMBOLS ====================

// Red heart
const uint8_t ICON_HEART[64] PROGMEM = {
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, RED, RED, ___, ___, RED, RED, ___,
  RED, RED, RED, RED, RED, RED, RED, RED,
  RED, RED, RED, RED, RED, RED, RED, RED,
  RED, RED, RED, RED, RED, RED, RED, RED,
  ___, RED, RED, RED, RED, RED, RED, ___,
  ___, ___, RED, RED, RED, RED, ___, ___,
  ___, ___, ___, RED, RED, ___, ___, ___
};

// Yellow star
const uint8_t ICON_STAR[64] PROGMEM = {
  ___, ___, ___, YEL, YEL, ___, ___, ___,
  ___, ___, ___, YEL, YEL, ___, ___, ___,
  ___, ___, YEL, YEL, YEL, YEL, ___, ___,
  YEL, YEL, YEL, YEL, YEL, YEL, YEL, YEL,
  ___, YEL, YEL, YEL, YEL, YEL, YEL, ___,
  ___, ___, YEL, YEL, YEL, YEL, ___, ___,
  ___, YEL, YEL, ___, ___, YEL, YEL, ___,
  YEL, YEL, ___, ___, ___, ___, YEL, YEL
};

// Checkmark (green)
const uint8_t ICON_CHECK[64] PROGMEM = {
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, GRN,
  ___, ___, ___, ___, ___, ___, GRN, GRN,
  ___, ___, ___, ___, ___, GRN, GRN, ___,
  GRN, ___, ___, ___, GRN, GRN, ___, ___,
  GRN, GRN, ___, GRN, GRN, ___, ___, ___,
  ___, GRN, GRN, GRN, ___, ___, ___, ___,
  ___, ___, GRN, ___, ___, ___, ___, ___
};

// X mark (red)
const uint8_t ICON_X[64] PROGMEM = {
  RED, RED, ___, ___, ___, ___, RED, RED,
  RED, RED, RED, ___, ___, RED, RED, RED,
  ___, RED, RED, RED, RED, RED, RED, ___,
  ___, ___, RED, RED, RED, RED, ___, ___,
  ___, ___, RED, RED, RED, RED, ___, ___,
  ___, RED, RED, RED, RED, RED, RED, ___,
  RED, RED, RED, ___, ___, RED, RED, RED,
  RED, RED, ___, ___, ___, ___, RED, RED
};

// ==================== NATURE ====================

// Fire
const uint8_t ICON_FIRE[64] PROGMEM = {
  ___, ___, ___, RED, ___, ___, ___, ___,
  ___, ___, RED, RED, RED, ___, ___, ___,
  ___, ___, RED, ORG, RED, ___, ___, ___,
  ___, RED, ORG, YEL, ORG, RED, ___, ___,
  ___, RED, ORG, YEL, ORG, RED, ___, ___,
  RED, ORG, YEL, YEL, YEL, ORG, RED, ___,
  RED, ORG, YEL, YEL, YEL, ORG, RED, ___,
  ___, RED, ORG, ORG, ORG, RED, ___, ___
};

// ==================== OBJECTS ====================

// Potion (red)
const uint8_t ICON_POTION[64] PROGMEM = {
  ___, ___, ___, GRY, GRY, ___, ___, ___,
  ___, ___, ___, GRY, GRY, ___, ___, ___,
  ___, ___, GRY, GRY, GRY, GRY, ___, ___,
  ___, GRY, RED, RED, RED, RED, GRY, ___,
  GRY, RED, RED, WHT, RED, RED, RED, GRY,
  GRY, RED, RED, RED, RED, RED, RED, GRY,
  GRY, RED, RED, RED, RED, RED, RED, GRY,
  ___, GRY, GRY, GRY, GRY, GRY, GRY, ___
};

// Sword
const uint8_t ICON_SWORD[64] PROGMEM = {
  ___, ___, ___, ___, ___, ___, GRY, WHT,
  ___, ___, ___, ___, ___, GRY, WHT, ___,
  ___, ___, ___, ___, GRY, WHT, ___, ___,
  ___, ___, ___, GRY, WHT, ___, ___, ___,
  BRN, ___, GRY, WHT, ___, ___, ___, ___,
  ___, BRN, WHT, ___, ___, ___, ___, ___,
  ___, BRN, BRN, ___, ___, ___, ___, ___,
  BRN, ___, ___, ___, ___, ___, ___, ___
};

// Shield
const uint8_t ICON_SHIELD[64] PROGMEM = {
  ___, BLU, BLU, BLU, BLU, BLU, BLU, ___,
  BLU, BLU, BLU, YEL, YEL, BLU, BLU, BLU,
  BLU, BLU, YEL, YEL, YEL, YEL, BLU, BLU,
  BLU, BLU, YEL, YEL, YEL, YEL, BLU, BLU,
  BLU, BLU, BLU, YEL, YEL, BLU, BLU, BLU,
  ___, BLU, BLU, BLU, BLU, BLU, BLU, ___,
  ___, ___, BLU, BLU, BLU, BLU, ___, ___,
  ___, ___, ___, BLU, BLU, ___, ___, ___
};

// ==================== CHARACTERS ====================

// Skull
const uint8_t ICON_SKULL[64] PROGMEM = {
  ___, ___, WHT, WHT, WHT, WHT, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, WHT, WHT, ___, ___, WHT, WHT, ___,
  ___, ___, WHT, ___, ___, WHT, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___
};

// Ghost
const uint8_t ICON_GHOST[64] PROGMEM = {
  ___, ___, WHT, WHT, WHT, WHT, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, ___, WHT, ___, ___, WHT, ___, WHT
};

// Alien (space invader style)
const uint8_t ICON_ALIEN[64] PROGMEM = {
  ___, ___, GRN, ___, ___, GRN, ___, ___,
  ___, ___, ___, GRN, GRN, ___, ___, ___,
  ___, ___, GRN, GRN, GRN, GRN, ___, ___,
  ___, GRN, GRN, ___, ___, GRN, GRN, ___,
  GRN, GRN, GRN, GRN, GRN, GRN, GRN, GRN,
  GRN, ___, GRN, GRN, GRN, GRN, ___, GRN,
  GRN, ___, GRN, ___, ___, GRN, ___, GRN,
  ___, ___, ___, GRN, GRN, ___, ___, ___
};

// Pacman
const uint8_t ICON_PACMAN[64] PROGMEM = {
  ___, ___, ___, YEL, YEL, YEL, ___, ___,
  ___, ___, YEL, YEL, YEL, YEL, YEL, ___,
  ___, YEL, YEL, YEL, YEL, YEL, ___, ___,
  ___, YEL, YEL, YEL, YEL, ___, ___, ___,
  ___, YEL, YEL, YEL, YEL, ___, ___, ___,
  ___, YEL, YEL, YEL, YEL, YEL, ___, ___,
  ___, ___, YEL, YEL, YEL, YEL, YEL, ___,
  ___, ___, ___, YEL, YEL, YEL, ___, ___
};

// Pacman Ghost (Orange)
const uint8_t ICON_PACMAN_GHOST[64] PROGMEM = {
  ___, GHO, GHO, GHO, GHO, GHO, GHO, ___,
  WHT, WHT, GHO, GHO, WHT, WHT, GHO, GHO,
  EYE, EYE, WHT, GHO, EYE, EYE, WHT, GHO,
  EYE, EYE, WHT, GHO, EYE, EYE, WHT, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, ___, GHO, ___, GHO, ___, GHO, ___
};

// ==================== RAINBOW ====================

// Full color rainbow circle
const uint8_t ICON_RAINBOW[64] PROGMEM = {
  ___, ___, RED, ORG, YEL, GRN, ___, ___,
  ___, RED, RED, ORG, YEL, GRN, BLU, ___,
  RED, RED, ___, ___, ___, ___, BLU, PUR,
  ORG, ORG, ___, ___, ___, ___, PUR, PUR,
  YEL, YEL, ___, ___, ___, ___, PNK, PNK,
  GRN, GRN, ___, ___, ___, ___, RED, RED,
  ___, BLU, BLU, PUR, PNK, RED, RED, ___,
  ___, ___, PUR, PUR, PNK, RED, ___, ___
};

// Mushroom
const uint8_t ICON_MUSHROOM[64] PROGMEM = {
  ___, ___, RED, RED, RED, RED, ___, ___,
  ___, RED, RED, RED, RED, MSP, RED, ___,
  RED, RED, RED, MSP, RED, RED, RED, RED,
  RED, RED, RED, RED, RED, MSP, RED, RED,
  ___, RED, RED, RED, RED, RED, RED, ___,
  ___, ___, MST, MST, MST, MSP, ___, ___,
  ___, ___, MST, MST, MST, MSP, ___, ___,
  ___, ___, ___, MST, MSP, ___, ___, ___
};

// Clean up index macros to avoid polluting namespace
#undef ___
#undef RED
#undef ORG
#undef YEL
#undef GRN
#undef CYN
#undef BLU
#undef PUR
#undef PNK
#undef WHT
#undef GRY
#undef DGR
#undef BRN
#undef SKN
#undef DRD
#undef LGR
#undef DBL
#undef GHO
#undef EYE
#undef SHY
#undef SHD
#undef MSP
#undef MST
#undef SKS
#undef CBK
#undef ITL
#undef IDT
#undef ISH
#undef IDB
#undef DRH
#undef DRB
#undef DRL
#undef DRF
#undef TWS
#undef TWC
#undef PPK
#undef PHL
#undef PST

// ==================== ICON COUNT ====================
#define ICON_COUNT 15

// Array of all icons for easy iteration
const uint8_t* const ALL_ICONS[ICON_COUNT] PROGMEM = {
  ICON_HEART, ICON_STAR, ICON_CHECK, ICON_X, ICON_FIRE,
  ICON_POTION, ICON_SWORD, ICON_SHIELD,
  ICON_SKULL, ICON_GHOST, ICON_ALIEN,
  ICON_PACMAN, ICON_PACMAN_GHOST,
  ICON_RAINBOW, ICON_MUSHROOM
};

// Icon names for debugging
const char* const ICON_NAMES[ICON_COUNT] PROGMEM = {
  "Heart", "Star", "Check", "X", "Fire",
  "Potion", "Sword", "Shield",
  "Skull", "Ghost", "Alien",
  "Pacman", "PacGhost",
  "Rainbow", "Mushroom"
};

// Compatibility aliases for emoji system
#define NUM_EMOJI_SPRITES ICON_COUNT
#define emojiSprites ALL_ICONS
#define emojiNames ICON_NAMES

#endif // ICONS_8X8_H
