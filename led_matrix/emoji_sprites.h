/**
 * 8x8 Multicolor Icons for LED Matrix
 * For use with FastLED / ESP32-S3-Matrix
 * 
 * Each icon is 64 CRGB values (8x8 grid, row-major order)
 * Compatible with your 11x6 matrix - center or offset as needed
 * 
 * Usage:
 *   #include "icons_8x8.h"
 *   
 *   // Draw icon at position (x,y)
 *   for(int row = 0; row < 8; row++) {
 *     for(int col = 0; col < 8; col++) {
 *       int ledIndex = XY(x + col, y + row);
 *       leds[ledIndex] = ICON_HEART[row * 8 + col];
 *     }
 *   }
 */

#ifndef ICONS_8X8_H
#define ICONS_8X8_H

#include <FastLED.h>

// Color shortcuts
#define ___ CRGB::Black
#define RED CRGB(255,0,0)
#define ORG CRGB(255,128,0)
#define YEL CRGB(255,255,0)
#define GRN CRGB(0,255,0)
#define CYN CRGB(0,255,255)
#define BLU CRGB(0,0,255)
#define PUR CRGB(128,0,255)
#define PNK CRGB(255,0,128)
#define WHT CRGB(255,255,255)
#define GRY CRGB(128,128,128)
#define DGR CRGB(64,64,64)
#define BRN CRGB(139,69,19)
#define SKN CRGB(255,200,150)
#define DRD CRGB(180,0,0)
#define LGR CRGB(0,180,0)
#define DBL CRGB(0,0,180)
#define GHO CRGB(249,56,1)   // Ghost orange
#define EYE CRGB(1,119,251)  // Eye blue

// ==================== SYMBOLS ====================

// Red heart
const CRGB ICON_HEART[64] = {
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
const CRGB ICON_STAR[64] = {
  ___, ___, ___, YEL, YEL, ___, ___, ___,
  ___, ___, ___, YEL, YEL, ___, ___, ___,
  ___, ___, YEL, YEL, YEL, YEL, ___, ___,
  YEL, YEL, YEL, YEL, YEL, YEL, YEL, YEL,
  ___, YEL, YEL, YEL, YEL, YEL, YEL, ___,
  ___, ___, YEL, YEL, YEL, YEL, ___, ___,
  ___, YEL, YEL, ___, ___, YEL, YEL, ___,
  YEL, YEL, ___, ___, ___, ___, YEL, YEL
};

// Smiley face
const CRGB ICON_SMILEY[64] = {
  ___, ___, YEL, YEL, YEL, YEL, ___, ___,
  ___, YEL, YEL, YEL, YEL, YEL, YEL, ___,
  YEL, YEL, ___, YEL, YEL, ___, YEL, YEL,
  YEL, YEL, YEL, YEL, YEL, YEL, YEL, YEL,
  YEL, YEL, YEL, YEL, YEL, YEL, YEL, YEL,
  YEL, ___, YEL, YEL, YEL, YEL, ___, YEL,
  ___, YEL, ___, ___, ___, ___, YEL, ___,
  ___, ___, YEL, YEL, YEL, YEL, ___, ___
};

// Checkmark (green)
const CRGB ICON_CHECK[64] = {
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
const CRGB ICON_X[64] = {
  RED, RED, ___, ___, ___, ___, RED, RED,
  RED, RED, RED, ___, ___, RED, RED, RED,
  ___, RED, RED, RED, RED, RED, RED, ___,
  ___, ___, RED, RED, RED, RED, ___, ___,
  ___, ___, RED, RED, RED, RED, ___, ___,
  ___, RED, RED, RED, RED, RED, RED, ___,
  RED, RED, RED, ___, ___, RED, RED, RED,
  RED, RED, ___, ___, ___, ___, RED, RED
};

// Question mark (blue)
const CRGB ICON_QUESTION[64] = {
  ___, ___, BLU, BLU, BLU, BLU, ___, ___,
  ___, BLU, BLU, ___, ___, BLU, BLU, ___,
  ___, ___, ___, ___, ___, BLU, BLU, ___,
  ___, ___, ___, ___, BLU, BLU, ___, ___,
  ___, ___, ___, BLU, BLU, ___, ___, ___,
  ___, ___, ___, BLU, BLU, ___, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, ___, ___, BLU, BLU, ___, ___, ___
};

// Exclamation mark (orange)
const CRGB ICON_EXCLAIM[64] = {
  ___, ___, ___, ORG, ORG, ___, ___, ___,
  ___, ___, ___, ORG, ORG, ___, ___, ___,
  ___, ___, ___, ORG, ORG, ___, ___, ___,
  ___, ___, ___, ORG, ORG, ___, ___, ___,
  ___, ___, ___, ORG, ORG, ___, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, ___, ___, ORG, ORG, ___, ___, ___,
  ___, ___, ___, ORG, ORG, ___, ___, ___
};

// ==================== NATURE ====================

// Sun
const CRGB ICON_SUN[64] = {
  ___, ___, ___, YEL, YEL, ___, ___, ___,
  ___, YEL, ___, ___, ___, ___, YEL, ___,
  ___, ___, YEL, YEL, YEL, YEL, ___, ___,
  YEL, ___, YEL, YEL, YEL, YEL, ___, YEL,
  YEL, ___, YEL, YEL, YEL, YEL, ___, YEL,
  ___, ___, YEL, YEL, YEL, YEL, ___, ___,
  ___, YEL, ___, ___, ___, ___, YEL, ___,
  ___, ___, ___, YEL, YEL, ___, ___, ___
};

// Moon (crescent)
const CRGB ICON_MOON[64] = {
  ___, ___, ___, WHT, WHT, WHT, ___, ___,
  ___, ___, WHT, WHT, WHT, ___, ___, ___,
  ___, WHT, WHT, WHT, ___, ___, ___, ___,
  ___, WHT, WHT, WHT, ___, ___, ___, ___,
  ___, WHT, WHT, WHT, ___, ___, ___, ___,
  ___, WHT, WHT, WHT, ___, ___, ___, ___,
  ___, ___, WHT, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, WHT, ___, ___
};

// Cloud
const CRGB ICON_CLOUD[64] = {
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, ___, WHT, WHT, WHT, ___, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, ___, ___,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___
};

// Rain cloud
const CRGB ICON_RAIN[64] = {
  ___, ___, GRY, GRY, GRY, ___, ___, ___,
  ___, GRY, GRY, GRY, GRY, GRY, ___, ___,
  GRY, GRY, GRY, GRY, GRY, GRY, GRY, ___,
  GRY, GRY, GRY, GRY, GRY, GRY, GRY, GRY,
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, BLU, ___, BLU, ___, BLU, ___, ___,
  BLU, ___, BLU, ___, BLU, ___, BLU, ___,
  ___, BLU, ___, BLU, ___, BLU, ___, ___
};

// Lightning bolt
const CRGB ICON_LIGHTNING[64] = {
  ___, ___, ___, ___, YEL, YEL, ___, ___,
  ___, ___, ___, YEL, YEL, ___, ___, ___,
  ___, ___, YEL, YEL, ___, ___, ___, ___,
  ___, YEL, YEL, YEL, YEL, YEL, ___, ___,
  ___, ___, ___, YEL, YEL, ___, ___, ___,
  ___, ___, YEL, YEL, ___, ___, ___, ___,
  ___, YEL, YEL, ___, ___, ___, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___
};

// Fire
const CRGB ICON_FIRE[64] = {
  ___, ___, ___, RED, ___, ___, ___, ___,
  ___, ___, RED, RED, RED, ___, ___, ___,
  ___, ___, RED, ORG, RED, ___, ___, ___,
  ___, RED, ORG, YEL, ORG, RED, ___, ___,
  ___, RED, ORG, YEL, ORG, RED, ___, ___,
  RED, ORG, YEL, YEL, YEL, ORG, RED, ___,
  RED, ORG, YEL, YEL, YEL, ORG, RED, ___,
  ___, RED, ORG, ORG, ORG, RED, ___, ___
};

// Snowflake
const CRGB ICON_SNOW[64] = {
  ___, ___, ___, CYN, CYN, ___, ___, ___,
  ___, CYN, ___, CYN, CYN, ___, CYN, ___,
  ___, ___, CYN, CYN, CYN, CYN, ___, ___,
  CYN, CYN, CYN, CYN, CYN, CYN, CYN, CYN,
  CYN, CYN, CYN, CYN, CYN, CYN, CYN, CYN,
  ___, ___, CYN, CYN, CYN, CYN, ___, ___,
  ___, CYN, ___, CYN, CYN, ___, CYN, ___,
  ___, ___, ___, CYN, CYN, ___, ___, ___
};

// Tree
const CRGB ICON_TREE[64] = {
  ___, ___, ___, GRN, GRN, ___, ___, ___,
  ___, ___, GRN, GRN, GRN, GRN, ___, ___,
  ___, GRN, GRN, GRN, GRN, GRN, GRN, ___,
  GRN, GRN, GRN, GRN, GRN, GRN, GRN, GRN,
  GRN, GRN, GRN, GRN, GRN, GRN, GRN, GRN,
  ___, ___, ___, BRN, BRN, ___, ___, ___,
  ___, ___, ___, BRN, BRN, ___, ___, ___,
  ___, ___, ___, BRN, BRN, ___, ___, ___
};

// ==================== OBJECTS ====================

// Coin (gold)
const CRGB ICON_COIN[64] = {
  ___, ___, YEL, YEL, YEL, YEL, ___, ___,
  ___, YEL, ORG, ORG, ORG, ORG, YEL, ___,
  YEL, ORG, YEL, ORG, ORG, YEL, ORG, YEL,
  YEL, ORG, ORG, YEL, YEL, ORG, ORG, YEL,
  YEL, ORG, ORG, YEL, YEL, ORG, ORG, YEL,
  YEL, ORG, YEL, ORG, ORG, YEL, ORG, YEL,
  ___, YEL, ORG, ORG, ORG, ORG, YEL, ___,
  ___, ___, YEL, YEL, YEL, YEL, ___, ___
};

// Key
const CRGB ICON_KEY[64] = {
  ___, ___, YEL, YEL, YEL, ___, ___, ___,
  ___, YEL, ___, ___, ___, YEL, ___, ___,
  ___, YEL, ___, ___, ___, YEL, ___, ___,
  ___, ___, YEL, YEL, YEL, ___, ___, ___,
  ___, ___, ___, YEL, ___, ___, ___, ___,
  ___, ___, ___, YEL, YEL, ___, ___, ___,
  ___, ___, ___, YEL, ___, ___, ___, ___,
  ___, ___, ___, YEL, YEL, ___, ___, ___
};

// Gem (diamond)
const CRGB ICON_GEM[64] = {
  ___, ___, CYN, CYN, CYN, CYN, ___, ___,
  ___, CYN, WHT, CYN, CYN, WHT, CYN, ___,
  CYN, WHT, CYN, CYN, CYN, CYN, WHT, CYN,
  ___, CYN, CYN, CYN, CYN, CYN, CYN, ___,
  ___, ___, CYN, CYN, CYN, CYN, ___, ___,
  ___, ___, ___, CYN, CYN, ___, ___, ___,
  ___, ___, ___, CYN, CYN, ___, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___
};

// Potion (red)
const CRGB ICON_POTION[64] = {
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
const CRGB ICON_SWORD[64] = {
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
const CRGB ICON_SHIELD[64] = {
  ___, BLU, BLU, BLU, BLU, BLU, BLU, ___,
  BLU, BLU, BLU, YEL, YEL, BLU, BLU, BLU,
  BLU, BLU, YEL, YEL, YEL, YEL, BLU, BLU,
  BLU, BLU, YEL, YEL, YEL, YEL, BLU, BLU,
  BLU, BLU, BLU, YEL, YEL, BLU, BLU, BLU,
  ___, BLU, BLU, BLU, BLU, BLU, BLU, ___,
  ___, ___, BLU, BLU, BLU, BLU, ___, ___,
  ___, ___, ___, BLU, BLU, ___, ___, ___
};

// ==================== ARROWS ====================

const CRGB ICON_ARROW_UP[64] = {
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, WHT, WHT, WHT, WHT, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___
};

const CRGB ICON_ARROW_DOWN[64] = {
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  ___, ___, WHT, WHT, WHT, WHT, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___
};

const CRGB ICON_ARROW_LEFT[64] = {
  ___, ___, ___, WHT, ___, ___, ___, ___,
  ___, ___, WHT, WHT, ___, ___, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, ___, WHT, WHT, ___, ___, ___, ___,
  ___, ___, ___, WHT, ___, ___, ___, ___
};

const CRGB ICON_ARROW_RIGHT[64] = {
  ___, ___, ___, ___, ___, WHT, ___, ___,
  ___, ___, ___, ___, ___, WHT, WHT, ___,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  ___, ___, ___, ___, ___, WHT, WHT, ___,
  ___, ___, ___, ___, ___, WHT, ___, ___
};

// ==================== CHARACTERS ====================

// Skull
const CRGB ICON_SKULL[64] = {
  ___, ___, WHT, WHT, WHT, WHT, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, WHT, WHT, ___, ___, WHT, WHT, ___,
  ___, ___, WHT, ___, ___, WHT, ___,___,
  ___, ___, ___, WHT, WHT, ___, ___, ___
};

// Ghost
const CRGB ICON_GHOST[64] = {
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
const CRGB ICON_ALIEN[64] = {
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
const CRGB ICON_PACMAN[64] = {
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
const CRGB ICON_PACMAN_GHOST[64] = {
  ___, GHO, GHO, GHO, GHO, GHO, GHO, ___,
  WHT, WHT, GHO, GHO, WHT, WHT, GHO, GHO,
  EYE, EYE, WHT, GHO, EYE, EYE, WHT, GHO,
  EYE, EYE, WHT, GHO, EYE, EYE, WHT, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, ___, GHO, ___, GHO, ___, GHO, ___
};


// ==================== MUSIC ====================

// Music note
const CRGB ICON_MUSIC[64] = {
  ___, ___, ___, ___, WHT, WHT, WHT, WHT,
  ___, ___, ___, ___, WHT, ___, ___, WHT,
  ___, ___, ___, ___, WHT, ___, ___, WHT,
  ___, ___, ___, ___, WHT, ___, ___, ___,
  ___, ___, ___, ___, WHT, ___, ___, ___,
  ___, WHT, WHT, ___, WHT, ___, ___, ___,
  WHT, WHT, WHT, WHT, WHT, ___, ___, ___,
  ___, WHT, WHT, ___, ___, ___, ___, ___
};

// Speaker
const CRGB ICON_SPEAKER[64] = {
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, WHT, WHT, WHT, ___, GRY, ___,
  ___, WHT, WHT, WHT, WHT, ___, ___, GRY,
  WHT, WHT, WHT, WHT, WHT, ___, GRY, GRY,
  WHT, WHT, WHT, WHT, WHT, ___, GRY, GRY,
  ___, WHT, WHT, WHT, WHT, ___, ___, GRY,
  ___, ___, WHT, WHT, WHT, ___, GRY, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___
};

// ==================== TECH ====================

// WiFi signal
const CRGB ICON_WIFI[64] = {
  ___, GRN, GRN, GRN, GRN, GRN, GRN, ___,
  GRN, ___, ___, ___, ___, ___, ___, GRN,
  ___, ___, GRN, GRN, GRN, GRN, ___, ___,
  ___, GRN, ___, ___, ___, ___, GRN, ___,
  ___, ___, ___, GRN, GRN, ___, ___, ___,
  ___, ___, GRN, ___, ___, GRN, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, ___, ___, GRN, GRN, ___, ___, ___
};

// Battery
const CRGB ICON_BATTERY[64] = {
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  WHT, WHT, GRN, GRN, GRN, GRN, WHT, WHT,
  WHT, WHT, GRN, GRN, GRN, GRN, WHT, WHT,
  WHT, WHT, GRN, GRN, GRN, GRN, WHT, WHT,
  WHT, WHT, GRN, GRN, GRN, GRN, WHT, WHT,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  ___, ___, ___, ___, ___, ___, ___, ___
};

// Power button
const CRGB ICON_POWER[64] = {
  ___, ___, ___, RED, RED, ___, ___, ___,
  ___, ___, RED, RED, RED, RED, ___, ___,
  ___, RED, ___, RED, RED, ___, RED, ___,
  RED, RED, ___, RED, RED, ___, RED, RED,
  RED, RED, ___, ___, ___, ___, RED, RED,
  ___, RED, ___, ___, ___, ___, RED, ___,
  ___, ___, RED, ___, ___, RED, ___, ___,
  ___, ___, ___, RED, RED, ___, ___, ___
};

// ==================== RAINBOW ====================

// Full color rainbow circle
const CRGB ICON_RAINBOW[64] = {
  ___, ___, RED, ORG, YEL, GRN, ___, ___,
  ___, RED, RED, ORG, YEL, GRN, BLU, ___,
  RED, RED, ___, ___, ___, ___, BLU, PUR,
  ORG, ORG, ___, ___, ___, ___, PUR, PUR,
  YEL, YEL, ___, ___, ___, ___, PNK, PNK,
  GRN, GRN, ___, ___, ___, ___, RED, RED,
  ___, BLU, BLU, PUR, PNK, RED, RED, ___,
  ___, ___, PUR, PUR, PNK, RED, ___, ___
};

// ==================== ICON COUNT ====================
#define ICON_COUNT 33

// Array of all icons for easy iteration
const CRGB* ALL_ICONS[ICON_COUNT] = {
  ICON_HEART, ICON_STAR, ICON_SMILEY, ICON_CHECK,
  ICON_X, ICON_QUESTION, ICON_EXCLAIM, ICON_SUN,
  ICON_MOON, ICON_CLOUD, ICON_RAIN, ICON_LIGHTNING,
  ICON_FIRE, ICON_SNOW, ICON_TREE, ICON_COIN,
  ICON_KEY, ICON_GEM, ICON_POTION, ICON_SWORD,
  ICON_SHIELD, ICON_ARROW_UP, ICON_ARROW_DOWN, ICON_ARROW_LEFT,
  ICON_ARROW_RIGHT, ICON_SKULL, ICON_GHOST, ICON_ALIEN,
  ICON_PACMAN, ICON_PACMAN_GHOST, ICON_MUSIC, ICON_WIFI, ICON_RAINBOW
};

// Icon names for debugging
const char* ICON_NAMES[ICON_COUNT] = {
  "Heart", "Star", "Smiley", "Check",
  "X", "Question", "Exclaim", "Sun",
  "Moon", "Cloud", "Rain", "Lightning",
  "Fire", "Snow", "Tree", "Coin",
  "Key", "Gem", "Potion", "Sword",
  "Shield", "ArrowUp", "ArrowDown", "ArrowLeft",
  "ArrowRight", "Skull", "Ghost", "Alien",
  "Pacman", "PacGhost", "Music", "WiFi", "Rainbow"
};

// Compatibility aliases for emoji system
#define NUM_EMOJI_SPRITES ICON_COUNT
#define emojiSprites ALL_ICONS
#define emojiNames ICON_NAMES

#endif // ICONS_8X8_H
