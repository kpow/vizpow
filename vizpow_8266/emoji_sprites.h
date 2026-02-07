/**
 * 8x8 Multicolor Icons for vizPow
 * For use with FastLED / ESP8266
 *
 * Each icon is 64 CRGB values (8x8 grid, row-major order)
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
#define SHY CRGB(244,59,2)   // Shy Guy red
#define SHD CRGB(175,6,0)    // Shy Guy dark

// ==================== SYMBOLS ====================

// Red heart
const CRGB ICON_HEART[64] PROGMEM = {
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
const CRGB ICON_STAR[64] PROGMEM = {
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
const CRGB ICON_SMILEY[64] PROGMEM = {
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
const CRGB ICON_CHECK[64] PROGMEM = {
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
const CRGB ICON_X[64] PROGMEM = {
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
const CRGB ICON_QUESTION[64] PROGMEM = {
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
const CRGB ICON_EXCLAIM[64] PROGMEM = {
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
const CRGB ICON_SUN[64] PROGMEM = {
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
const CRGB ICON_MOON[64] PROGMEM = {
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
const CRGB ICON_CLOUD[64] PROGMEM = {
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
const CRGB ICON_RAIN[64] PROGMEM = {
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
const CRGB ICON_LIGHTNING[64] PROGMEM = {
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
const CRGB ICON_FIRE[64] PROGMEM = {
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
const CRGB ICON_SNOW[64] PROGMEM = {
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
const CRGB ICON_TREE[64] PROGMEM = {
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
const CRGB ICON_COIN[64] PROGMEM = {
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
const CRGB ICON_KEY[64] PROGMEM = {
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
const CRGB ICON_GEM[64] PROGMEM = {
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
const CRGB ICON_POTION[64] PROGMEM = {
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
const CRGB ICON_SWORD[64] PROGMEM = {
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
const CRGB ICON_SHIELD[64] PROGMEM = {
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

const CRGB ICON_ARROW_UP[64] PROGMEM = {
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, WHT, WHT, WHT, WHT, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___
};

const CRGB ICON_ARROW_DOWN[64] PROGMEM = {
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___,
  WHT, WHT, ___, WHT, WHT, ___, WHT, WHT,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, ___,
  ___, ___, WHT, WHT, WHT, WHT, ___, ___,
  ___, ___, ___, WHT, WHT, ___, ___, ___
};

const CRGB ICON_ARROW_LEFT[64] PROGMEM = {
  ___, ___, ___, WHT, ___, ___, ___, ___,
  ___, ___, WHT, WHT, ___, ___, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  WHT, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, ___, WHT, WHT, ___, ___, ___, ___,
  ___, ___, ___, WHT, ___, ___, ___, ___
};

const CRGB ICON_ARROW_RIGHT[64] PROGMEM = {
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
const CRGB ICON_SKULL[64] PROGMEM = {
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
const CRGB ICON_GHOST[64] PROGMEM = {
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
const CRGB ICON_ALIEN[64] PROGMEM = {
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
const CRGB ICON_PACMAN[64] PROGMEM = {
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
const CRGB ICON_PACMAN_GHOST[64] PROGMEM = {
  ___, GHO, GHO, GHO, GHO, GHO, GHO, ___,
  WHT, WHT, GHO, GHO, WHT, WHT, GHO, GHO,
  EYE, EYE, WHT, GHO, EYE, EYE, WHT, GHO,
  EYE, EYE, WHT, GHO, EYE, EYE, WHT, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, GHO, GHO, GHO, GHO, GHO, GHO, GHO,
  GHO, ___, GHO, ___, GHO, ___, GHO, ___
};

// Shy Guy (Mario enemy)
const CRGB ICON_SHY_GUY[64] PROGMEM = {
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, WHT, WHT, WHT, WHT, SHY, SHD, ___,
  WHT, ___, WHT, ___, WHT, WHT, SHY, SHD,
  WHT, ___, WHT, ___, WHT, WHT, SHY, ___,
  WHT, WHT, ___, WHT, WHT, WHT, SHY, SHD,
  ___, WHT, WHT, WHT, WHT, SHY, SHY, SHD,
  ___, ___, SHY, SHY, SHY, SHY, SHD, ___,
  ___, SHD, SHD, ___, SHD, SHD, ___, ___
};


// ==================== MUSIC ====================

// Music note
const CRGB ICON_MUSIC[64] PROGMEM = {
  ___, ___, ___, ___, WHT, WHT, WHT, WHT,
  ___, ___, ___, ___, WHT, ___, ___, WHT,
  ___, ___, ___, ___, WHT, ___, ___, WHT,
  ___, ___, ___, ___, WHT, ___, ___, ___,
  ___, ___, ___, ___, WHT, ___, ___, ___,
  ___, WHT, WHT, ___, WHT, ___, ___, ___,
  WHT, WHT, WHT, WHT, WHT, ___, ___, ___,
  ___, WHT, WHT, ___, ___, ___, ___, ___
};

// ==================== TECH ====================

// WiFi signal
const CRGB ICON_WIFI[64] PROGMEM = {
  ___, GRN, GRN, GRN, GRN, GRN, GRN, ___,
  GRN, ___, ___, ___, ___, ___, ___, GRN,
  ___, ___, GRN, GRN, GRN, GRN, ___, ___,
  ___, GRN, ___, ___, ___, ___, GRN, ___,
  ___, ___, ___, GRN, GRN, ___, ___, ___,
  ___, ___, GRN, ___, ___, GRN, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, ___, ___, GRN, GRN, ___, ___, ___
};

// ==================== RAINBOW ====================

// Full color rainbow circle
const CRGB ICON_RAINBOW[64] PROGMEM = {
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
const CRGB ICON_MUSHROOM[64] PROGMEM = {
  ___, ___, RED, RED, RED, RED, ___, ___,
  ___, RED, RED, RED, RED, CRGB(255,208,122), RED, ___,
  RED, RED, RED, CRGB(255,208,122), RED, RED, RED, RED,
  RED, RED, RED, RED, RED, CRGB(255,208,122), RED, RED,
  ___, RED, RED, RED, RED, RED, RED, ___,
  ___, ___, CRGB(240,255,255), CRGB(240,255,255), CRGB(240,255,255), CRGB(255,208,122), ___, ___,
  ___, ___, CRGB(240,255,255), CRGB(240,255,255), CRGB(240,255,255), CRGB(255,208,122), ___, ___,
  ___, ___, ___, CRGB(240,255,255), CRGB(255,208,122), ___, ___, ___
};

// Skelly
const CRGB ICON_SKELLY[64] PROGMEM = {
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, WHT, WHT, WHT, WHT, CRGB(188,189,190), ___, ___,
  WHT, ___, ___, WHT, ___, ___, CRGB(188,189,190), ___,
  WHT, ___, RED, WHT, RED, ___, CRGB(188,189,190), ___,
  WHT, WHT, WHT, WHT, WHT, WHT, CRGB(188,189,190), ___,
  WHT, WHT, WHT, ___, WHT, WHT, CRGB(188,189,190), ___,
  ___, WHT, WHT, WHT, WHT, CRGB(188,189,190), ___, ___,
  ___, WHT, ___, WHT, ___, CRGB(188,189,190), ___, ___
};

// chicken
const CRGB ICON_CHICKEN[64] PROGMEM = {
  ___, ___, RED, RED, RED, ___, ___, ___,
  ___, ___, WHT, WHT, WHT, ___, ___, ___,
  ___, WHT, WHT, WHT, WHT, WHT, ___, ___,
  ___, WHT, ___, WHT, WHT, WHT, WHT, ___,
  CRGB(254,186,0), WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, WHT, WHT, WHT, WHT, WHT, WHT, WHT,
  ___, ___, WHT, WHT, WHT, WHT, WHT, ___,
  ___, ___, ___, CRGB(254,186,0), CRGB(254,186,0), ___, ___, ___
};

// invader
const CRGB ICON_INVADER[64] PROGMEM = {
  CRGB(48,228,215), ___, CRGB(48,228,215), CRGB(48,228,215), CRGB(48,228,215), CRGB(17,17,17), CRGB(48,228,215), CRGB(17,17,17),
  ___, CRGB(48,228,215), CRGB(48,228,215), CRGB(48,228,215), CRGB(48,228,215), CRGB(48,228,215), CRGB(17,17,17), ___,
  CRGB(48,228,215), CRGB(48,228,215), ___, CRGB(48,228,215), ___, CRGB(48,228,215), CRGB(48,228,215), CRGB(17,17,17),
  CRGB(33,131,129), CRGB(33,131,129), ___, CRGB(33,131,129), ___, CRGB(33,131,129), CRGB(33,131,129), CRGB(17,17,17),
  CRGB(33,131,129), CRGB(33,131,129), CRGB(33,131,129), CRGB(33,131,129), CRGB(33,131,129), CRGB(33,131,129), CRGB(33,131,129), CRGB(17,17,17),
  CRGB(6,59,88), ___, CRGB(6,59,88), ___, CRGB(6,59,88), ___, CRGB(6,59,88), ___,
  CRGB(6,59,88), ___, CRGB(6,59,88), ___, CRGB(6,59,88), ___, CRGB(6,59,88), ___,
  CRGB(6,59,88), ___, CRGB(6,59,88), ___, CRGB(6,59,88), ___, CRGB(6,59,88), ___
};

// dragon
const CRGB ICON_DRAGON[64] PROGMEM = {
  ___, ___, CRGB(223,2,83), ___, CRGB(223,2,83), ___, ___, ___,
  ___, CRGB(148,123,232), CRGB(148,123,232), CRGB(148,123,232), CRGB(148,123,232), ___, ___, ___,
  CRGB(148,123,232), ___, CRGB(148,123,232), ___, CRGB(148,123,232), CRGB(216,29,85), ___, ___,
  CRGB(148,123,232), CRGB(148,123,232), CRGB(148,123,232), CRGB(148,123,232), CRGB(148,123,232), ___, ___, ___,
  ___, ___, CRGB(217,185,252), CRGB(217,185,252), CRGB(148,123,232), CRGB(216,29,85), ___, ___,
  ___, CRGB(148,123,232), CRGB(217,185,252), CRGB(148,123,232), CRGB(148,123,232), ___, ___, CRGB(148,123,232),
  ___, ___, CRGB(217,185,252), CRGB(217,185,252), CRGB(148,123,232), CRGB(148,123,232), CRGB(148,123,232), ___,
  ___, ___, CRGB(148,123,232), ___, CRGB(148,123,232), ___, ___, ___
};

// twinkle heart
const CRGB ICON_TWINKLE_HEART[64] PROGMEM = {
  ___, RED, RED, ___, RED, RED, ___, ___,
  RED, RED, RED, RED, RED, RED, RED, ___,
  RED, RED, RED, RED, RED, ___, RED, ___,
  ___, RED, RED, RED, ___, CRGB(248,183,7), ___, ___,
  ___, ___, RED, ___, CRGB(248,183,7), CRGB(241,221,116), CRGB(248,183,7), ___,
  ___, ___, ___, RED, ___, CRGB(248,183,7), ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___,
  ___, ___, ___, ___, ___, ___, ___, ___
};

// popsicle
const CRGB ICON_POPSICLE[64] PROGMEM = {
  ___, ___, ___, CRGB(248,121,247), CRGB(248,121,247), ___, ___, ___,
  ___, ___, CRGB(248,121,247), CRGB(255,243,255), CRGB(248,121,247), CRGB(248,121,247), ___, ___,
  ___, ___, CRGB(255,243,255), CRGB(248,121,247), CRGB(248,121,247), CRGB(248,121,247), ___, ___,
  ___, ___, CRGB(248,121,247), CRGB(248,121,247), CRGB(248,121,247), CRGB(248,121,247), ___, ___,
  ___, ___, CRGB(248,121,247), CRGB(248,121,247), CRGB(248,121,247), CRGB(248,121,247), ___, ___,
  ___, ___, CRGB(248,121,247), CRGB(248,121,247), CRGB(248,121,247), CRGB(248,121,247), ___, ___,
  ___, ___, ___, CRGB(169,124,26), CRGB(169,124,26), ___, ___, ___,
  ___, ___, ___, CRGB(169,124,26), CRGB(169,124,26), ___, ___, ___
};

// ==================== ICON COUNT ====================
#define ICON_COUNT 41

// Array of all icons for easy iteration
const CRGB* const ALL_ICONS[ICON_COUNT] PROGMEM = {
  ICON_HEART, ICON_STAR, ICON_SMILEY, ICON_CHECK,
  ICON_X, ICON_QUESTION, ICON_EXCLAIM, ICON_SUN,
  ICON_MOON, ICON_CLOUD, ICON_RAIN, ICON_LIGHTNING,
  ICON_FIRE, ICON_SNOW, ICON_TREE, ICON_COIN,
  ICON_KEY, ICON_GEM, ICON_POTION, ICON_SWORD,
  ICON_SHIELD, ICON_ARROW_UP, ICON_ARROW_DOWN, ICON_ARROW_LEFT,
  ICON_ARROW_RIGHT, ICON_SKULL, ICON_GHOST, ICON_ALIEN,
  ICON_PACMAN, ICON_PACMAN_GHOST, ICON_SHY_GUY, ICON_MUSIC, ICON_WIFI, ICON_RAINBOW, ICON_MUSHROOM, ICON_SKELLY, ICON_CHICKEN, ICON_INVADER, ICON_DRAGON, ICON_TWINKLE_HEART, ICON_POPSICLE
};

// Icon names for debugging
const char* const ICON_NAMES[ICON_COUNT] PROGMEM = {
  "Heart", "Star", "Smiley", "Check",
  "X", "Question", "Exclaim", "Sun",
  "Moon", "Cloud", "Rain", "Lightning",
  "Fire", "Snow", "Tree", "Coin",
  "Key", "Gem", "Potion", "Sword",
  "Shield", "ArrowUp", "ArrowDown", "ArrowLeft",
  "ArrowRight", "Skull", "Ghost", "Alien",
  "Pacman", "PacGhost", "ShyGuy", "Music", "WiFi", "Rainbow", "Mushroom", "Skelly", "chicken", "invader", "dragon", "twinkleheart", "popsicle"
};

// Compatibility aliases for emoji system
#define NUM_EMOJI_SPRITES ICON_COUNT
#define emojiSprites ALL_ICONS
#define emojiNames ICON_NAMES

#endif // ICONS_8X8_H
