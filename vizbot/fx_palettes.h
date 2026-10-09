#pragma once

// ============================================================================
// fx_palettes.h — GENERATED, do not hand-edit. Source: palettes/palettes.json,
// edit with palettes/lab, regenerate with palettes/gen.py.
//
// 23 palettes x 16 entries x 3 bytes = 1104 bytes of flash.
//
// Data is WLED's own gradient definitions (see palettes/sources/
// wled_palettes_source.cpp, a verbatim copy of wled00/palettes.cpp) plus
// FastLED's RainbowColors_p, expanded to 16 evenly-spaced entries each. Those
// gradients largely originate from cpt-city under heterogeneous licences, which
// is why the source file is committed alongside this one.
//
// Deliberately NOT FastLED's built-in palettes at runtime: the satellites do
// not link FastLED, so using built-ins on the Console and gradients on the sats
// would make the same palette id resolve to different colours per surface. One
// table, one lookup, identical everywhere — the same reasoning that put the
// effects in ps_effects.h.
//
// DATA ONLY — no pixel type, no dependencies beyond stdint. That is deliberate:
// main.cpp wants the names and the count for the palette control but does not
// link FastLED and has no `Color`. The lookup lives in fx_palette_lookup.h,
// which does require one.
//
// PALETTE 0 IS THE REGRESSION BASELINE. Rainbow sampled at hue 0,16,…240
// reproduces CHSV(h,255,v) to within 2/255 per channel — verified exhaustively
// over all 65,536 (h,v) and (h,s) pairs by test/host/palette_parity.cpp. Every
// effect on palette 0 must look exactly as it did before palettes existed.
// ============================================================================

#include <stdint.h>

#define FX_NUM_PALETTES 23
#define FX_PAL_ENTRIES  16

// CRC32 of the resolved set (entries, names, tags, flags). Identical in every
// generated copy — palettes/sync.py compares it to tell a stale copy apart.
#define FX_PALETTE_SET_CRC 0xEEFCC249u

static const uint8_t FX_PALETTES[FX_NUM_PALETTES][FX_PAL_ENTRIES][3] = {
    { //  0 Rainbow
      {255,  0,  0}, {213, 42,  0}, {171, 85,  0}, {171,127,  0},
      {171,171,  0}, { 86,213,  0}, {  0,255,  0}, {  0,213, 42},
      {  0,171, 85}, {  0, 86,170}, {  0,  0,255}, { 42,  0,213},
      { 85,  0,171}, {127,  0,129}, {171,  0, 85}, {213,  0, 43},
    },
    { //  1 Analogous
      { 38,  0,255}, { 50,  0,255}, { 63,  0,255}, { 76,  0,255},
      { 89,  0,255}, {104,  0,255}, {118,  0,255}, {132,  0,255},
      {146,  0,236}, {162,  0,199}, {177,  0,162}, {192,  0,125},
      {207,  0, 93}, {223,  0, 62}, {239,  0, 31}, {255,  0,  0},
    },
    { //  2 Electric
      {  0,  0,255}, {  0, 34,255}, {  0, 68,255}, {  0,103,255},
      {  0,137,255}, {  0,171,255}, {  0,205,255}, {  0,239,255},
      { 15,255,236}, { 45,255,199}, { 75,255,162}, {105,255,125},
      {141,255, 93}, {179,255, 62}, {217,255, 31}, {255,255,  0},
    },
    { //  3 Sunset
      {181,  0,  0}, {209, 65,  0}, {233,120,  0}, {255,170,  0},
      {233,127, 38}, {211, 85, 77}, {196, 56,108}, {181, 27,139},
      {165,  0,169}, {140,  0,174}, {115,  0,179}, { 89,  0,184},
      { 65,  0,189}, { 43,  0,195}, { 22,  0,201}, {  0,  0,207},
    },
    { //  4 Heat
      {  0,  0,  0}, { 51,  0,  0}, {102,  0,  0}, {153,  0,  0},
      {204,  0,  0}, {255,  0,  0}, {255, 51,  0}, {255,102,  0},
      {255,153,  0}, {255,204,  0}, {255,255,  0}, {255,255, 51},
      {255,255,102}, {255,255,153}, {255,255,204}, {255,255,255},
    },
    { //  5 Red Tide
      {251, 46,  0}, {253,102, 15}, {251,146, 40}, {246,188, 95},
      {244,169, 79}, {240, 93, 10}, {203, 76, 10}, {191, 98, 30},
      {242,213, 98}, {218,159, 66}, {182, 76, 21}, {226,178,110},
      {252,177, 97}, {238, 83,  5}, {182, 45,  4}, {126,  8,  4},
    },
    { //  6 Ember
      {  0,  0,  0}, { 45,  0,  0}, { 91,  0,  0}, {143,  0,  0},
      {200,  0,  0}, {255,  0,  2}, {255,  0, 48}, {255,  0, 94},
      {255,  0,145}, {255,  0,200}, {255,  0,255}, {255, 51,199},
      {255,103,143}, {255,154, 92}, {255,204, 46}, {255,255,  0},
    },
    { //  7 Tertiary
      {  0, 25,255}, { 10, 55,218}, { 20, 86,180}, { 30,117,143},
      { 41,148,108}, { 54,179, 77}, { 67,209, 45}, { 79,240, 14},
      { 97,239,  2}, {118,208,  7}, {140,177, 12}, {161,147, 17},
      {184,117, 23}, {208, 86, 29}, {231, 55, 35}, {255, 25, 41},
    },
    { //  8 Garnet
      {  0,  0,  0}, { 30,  0, 31}, { 60,  0, 62}, { 91,  0, 94},
      {123,  0,127}, {161,  0,164}, {199,  0,200}, {237,  0,237},
      {255,  0,236}, {255,  0,199}, {255,  0,162}, {255,  0,125},
      {255,  0, 93}, {255,  0, 62}, {255,  0, 31}, {255,  0,  0},
    },
    { //  9 Retro
      {222,191,  8}, {215,181,  7}, {208,172,  7}, {201,163,  6},
      {194,153,  6}, {187,144,  5}, {180,135,  5}, {173,126,  4},
      {166,116,  4}, {159,107,  3}, {152, 98,  3}, {145, 89,  2},
      {138, 79,  2}, {131, 70,  1}, {124, 61,  1}, {117, 52,  1},
    },
    { // 10 Temperature
      { 20, 92,171}, { 13,117,191}, {  4,150,217}, { 10,173,234},
      { 34,186,206}, { 89,205,198}, {148,221,165}, {187,229, 98},
      {224,236, 36}, {252,224, 28}, {251,199,  4}, {246,147, 10},
      {236, 96, 17}, {218, 45, 26}, {159, 36, 34}, {151, 38, 35},
    },
    { // 11 Clown
      {242,168, 38}, {239,154, 44}, {237,141, 50}, {235,128, 56},
      {232,115, 62}, {230,102, 68}, {228, 89, 74}, {225, 77, 81},
      {217, 74, 99}, {209, 71,117}, {201, 68,135}, {193, 65,153},
      {185, 62,170}, {177, 59,189}, {169, 57,206}, {161, 54,225},
    },
    { // 12 Drywet
      {119, 97, 33}, {165,138, 55}, {212,179, 77}, {221,207, 95},
      {194,223,110}, {166,238,126}, {114,238,168}, { 61,238,211},
      { 30,213,232}, { 18,166,234}, {  7,120,236}, { 15, 71,211},
      { 23, 23,186}, { 22, 11,159}, { 13, 31,130}, {  4, 51,101},
    },
    { // 13 Toxy Reaf
      {  2,239,126}, { 11,225,132}, { 21,211,138}, { 30,198,144},
      { 40,184,150}, { 49,171,156}, { 59,157,162}, { 68,143,168},
      { 78,130,174}, { 87,116,180}, { 97,103,186}, {106, 89,192},
      {116, 75,198}, {125, 62,204}, {135, 48,210}, {145, 35,217},
    },
    { // 14 Hult
      { 24,184,174}, { 19,178,167}, { 15,172,161}, { 11,167,155},
      { 13,160,142}, { 65,149, 78}, {117,138, 14}, {155,165, 15},
      {161,171, 17}, {117,138, 15}, { 77,144, 60}, { 38,150,106},
      {  5,153,141}, {  2,141,129}, {  0,128,117}, {  0,128,117},
    },
    { // 15 Yelblu Hot
      { 43, 30, 57}, { 51, 21, 74}, { 60, 12, 93}, { 69,  3,111},
      { 75,  0,112}, { 78,  0,100}, { 82,  0, 88}, { 86,  0, 76},
      {129, 22, 53}, {181, 48, 29}, {207, 85, 24}, {220,123, 27},
      {230,151, 29}, {239,180, 31}, {242,213, 29}, {246,247, 27},
    },
    { // 16 Party
      {155,  0,213}, {189,  0,184}, {218,  0,146}, {243,  0, 92},
      {244, 85,  0}, {220,143,  0}, {213,180,  0}, {213,213,  0},
      {213,155,  0}, {239,102,  0}, {249,  0, 68}, {225,  0,134},
      {196,  0,176}, {163,  0,207}, {118,  0,232}, {  0, 50,252},
    },
    { // 17 Aurora
      {  1,  5, 45}, {  0, 56, 39}, {  0,108, 33}, {  0,160, 27},
      {  0,203, 21}, {  0,217, 15}, {  0,232,  9}, {  0,247,  3},
      {  0,252,  8}, {  0,247, 26}, {  0,243, 45}, {  0,182, 23},
      {  0,125,  9}, {  0, 85, 21}, {  0, 45, 33}, {  1,  5, 45},
    },
    { // 18 Aurora 2
      { 17,177, 13}, { 44,194, 10}, { 72,211,  8}, { 99,228,  6},
      {115,237, 11}, { 89,219, 42}, { 64,201, 73}, { 38,182,104},
      { 52,161,121}, {112,135,123}, {172,110,124}, {232, 84,126},
      {235, 81,144}, {213, 88,170}, {192, 94,195}, {171,101,221},
    },
    { // 19 Splash
      {186, 63,255}, {191, 55,232}, {196, 48,209}, {202, 41,187},
      {207, 34,164}, {213, 27,141}, {218, 19,119}, {224, 12, 96},
      {228, 45,108}, {230,115,154}, {233,184,199}, {226,161,203},
      {215,100,189}, {205, 38,176}, {205, 38,176}, {205, 38,176},
    },
    { // 20 Light Pink
      { 79, 32,109}, { 86, 37,114}, { 94, 42,119}, {102, 48,124},
      {128,107,165}, {154,165,206}, {180,222,248}, {217,218,245},
      {190,173,230}, {178,146,219}, {183,137,210}, {187,128,201},
      {190,124,197}, {192,120,193}, {195,115,188}, {198,111,184},
    },
    { // 21 Tiamat
      {  1,  2, 14}, {108, 74, 77}, { 89,112, 95}, {254,152,124},
      {253,149,117}, {214,196,  5}, {218,188,  9}, {221,179, 13},
      {177,252,253}, {188,231,220}, {199,210,185}, {210,189,152},
      {239,249,250}, {242,191,157}, {246,134, 65}, {193,135,255},
    },
    { // 22 Aqua Flash
      {  0,  0,  0}, { 33, 61, 62}, { 66,124,125}, {100,186,189},
      {138,242,232}, {208,250,123}, {255,255, 95}, {255,255,218},
      {255,255,171}, {255,255, 53}, {194,248,145}, {133,242,238},
      { 99,185,187}, { 66,123,124}, { 33, 61, 62}, {  0,  0,  0},
    },
};

static const char* const FX_PALETTE_NAMES[FX_NUM_PALETTES] = {
    "Rainbow", "Analogous", "Electric", "Sunset"
  , "Heat", "Red Tide", "Ember", "Tertiary"
  , "Garnet", "Retro", "Temperature", "Clown"
  , "Drywet", "Toxy Reaf", "Hult", "Yelblu Hot"
  , "Party", "Aurora", "Aurora 2", "Splash"
  , "Light Pink", "Tiamat", "Aqua Flash"
};

// 3-char tags for the Console's Live Control bar.
static const char* const FX_PALETTE_TAGS[FX_NUM_PALETTES] = {
    "RNB", "ANL", "ELC", "SUN", "HET", "RTD", "EMB", "TRT", "GRN", "RTR", "TMP", "CLW", "DRY", "TXY", "HLT", "YBH", "PTY", "AUR", "AU2", "SPL", "LPK", "TMT", "AQF"
};

// Whether entry 15 meets entry 0. The hue wheel has no seam; most WLED
// gradients are NOT cyclic, so wrapping their interpolation from the last entry
// back to the first shows a hard band wherever an effect sweeps the full index
// range (Vortex's hueStep*i, Soap's ~noise*3, Perlin's hf*255). Non-cyclic
// palettes clamp at the last entry instead.
static const uint8_t FX_PALETTE_CYCLIC[FX_NUM_PALETTES] = {
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
};

// Per-palette ceiling on the beat-driven index shift, 0..255. A hue rotation
// never changes luma; a palette index shift does, so on a palette with a dark
// region a large shift can black out much of the frame on the beat. Derived
// from each palette's luma spread.
static const uint8_t FX_PALETTE_BEAT_SCALE[FX_NUM_PALETTES] = {
    131, 234, 91, 135, 63, 121, 78, 138, 201, 163, 137, 192, 112, 173, 212, 99, 138, 125, 198, 155, 126, 72, 65
};
