#ifndef TOUCH_UI_H
#define TOUCH_UI_H
// ============================================================================
// touch_ui.h — face-first touch UI (TOUCH_UI_V2: 1.69 portrait, CoreS3 landscape)
// ============================================================================
// The face owns the screen. Tap = poke, swipe up (or long-press) = quick dock,
// swipe left/right on the face = step the scene. Sheets slide over the bottom
// half while the face shrinks into the top as a live preview. Layout numbers
// are native pixels from the approved comps (P169_* artboards).
//
// Portrait (240x280) uses bottom sheets and a settings tile page; landscape
// (CoreS3 family, 320x240) keeps the full face with side rails and a
// master-detail settings page (PS3_* artboards).
//
// Included from touch_control.h, which supplies readTouch() and the effect and
// palette name tables.
// ============================================================================

#ifdef TOUCH_UI_V2

#include <Preferences.h>

// ---- Colors (RGB565, from the comps' token table) ------------------------
#define UI_C_BG       0x0000
#define UI_C_SURFACE  0x10A3  // #12171D
#define UI_C_TILE     0x1925  // #1E252E
#define UI_C_STROKE   0x29C8  // #2E3843
#define UI_C_TEXT     0xFFFF
#define UI_C_DIM      0x8CD5  // #8C99A8
#define UI_C_TRACK    0x3A2A  // #3A4450
#define UI_C_HANDLE   0x5B2E  // #5A6573
#define UI_C_MOOD     0xFE87  // #FFD23F
#define UI_C_LOOK     0xFAFA  // #FF5FD2
#define UI_C_INFO     0x3EDF  // #3FD8FF
#define UI_C_LIGHT    0xFCC7  // #FF9A3C
#define UI_C_CONNECT  0x3EF0  // #3DDC84
#define UI_C_SYSTEM   0xC67B  // #C3CEDB
#define UI_C_OFF      0xFACB  // #FF5A5F

// ---- Gesture tuning -------------------------------------------------------
#define UI_TAP_SLOP_PX     14   // Movement still counted as a tap / long-press
#define UI_SWIPE_MIN_PX    30
#define UI_SWIPE_MAX_MS    800
#define UI_LONG_PRESS_MS   600
#define UI_LIFT_DEBOUNCE_MS 60  // CST816T drops single samples mid-touch
#define UI_SHEET_IDLE_MS   10000
#define UI_SCENE_IDLE_MS   15000
#define UI_PAGE_IDLE_MS    30000
#define UI_HOLD_MS         1500
#define UI_HANDLE_SHOW_MS  4000
#define UI_FACE_ANIM_MS    180
#define UI_HINT_BOOT_MS    12000
#define UI_HINT_IDLE_MS    600000UL
#define UI_HINT_BOOTS      3

#define UI_LANDSCAPE (LCD_WIDTH > LCD_HEIGHT)
#define UI_CX (LCD_WIDTH / 2)

// Mini face while a sheet is open, and where sheets start
#if UI_LANDSCAPE
#define UI_MINI_CY    56
#define UI_MINI_SCALE 0.5f
#define UI_SHEET_TOP  112
#else
#define UI_MINI_CY    74
#define UI_MINI_SCALE 0.62f
#define UI_SHEET_TOP  124
#endif

enum UiScreen : uint8_t { UI_HOME, UI_DOCK, UI_MOOD, UI_SCENE, UI_LIGHT, UI_SETTINGS, UI_CAT };
// Settings categories (same hues as the dock)
enum UiCat : uint8_t { CAT_LOOK, CAT_MOOD, CAT_INFO, CAT_LIGHT, CAT_CONNECT, CAT_SYSTEM, CAT_COUNT,
                       CAT_HEAD = CAT_COUNT };  // Stackchan head page (dock tile, not in the rail)
enum UiSwipe : uint8_t { UI_SWIPE_UP = 1, UI_SWIPE_DOWN, UI_SWIPE_LEFT, UI_SWIPE_RIGHT };

// Hit-target ids (0 = nothing)
enum UiId : uint8_t {
  UI_ID_NONE = 0,
  UI_ID_TILE0 = 1,                 // dock tiles 1..8
  UI_ID_PILL = 10, UI_ID_BACK = 11,
  UI_ID_SCENE_PREV = 20, UI_ID_SCENE_NEXT, UI_ID_PALETTE, UI_ID_PIXEL, UI_ID_HIRES, UI_ID_AUTO, UI_ID_KSCOPE,
  UI_ID_MINUS = 30, UI_ID_PLUS, UI_ID_TRACK, UI_ID_PRESET0,  // presets 33..35
  UI_ID_PERS0 = 40,                // personality segments 40..42
  UI_ID_CHIP0 = 50,                // expression chips 50..57
  UI_ID_CAT0 = 60,                 // settings category tiles 60..65
  UI_ID_ROW0 = 70,                 // category page rows 70..
};

struct UiRect {
  int16_t x, y, w, h;
  bool hit(int16_t px, int16_t py, int16_t slop = 3) const {
    return px >= x - slop && px < x + w + slop && py >= y - slop && py < y + h + slop;
  }
};

// ---- Layout (native px, from the comps) -----------------------------------
static const UiRect UI_R_PILL     = {38, 6, 164, 24};
static const int16_t UI_TILE_X[3] = {12, 86, 160};
static const int16_t UI_TILE_Y[2] = {142, 200};
static const UiRect UI_R_PREV     = {12, 132, 44, 44};
static const UiRect UI_R_SCENE    = {62, 132, 116, 44};
static const UiRect UI_R_NEXT     = {184, 132, 44, 44};
static const UiRect UI_R_PALETTE  = {12, 184, 216, 34};
static const UiRect UI_R_SEG      = {12, 224, 104, 34};
static const UiRect UI_R_AUTO     = {124, 224, 104, 34};
static const UiRect UI_R_MINUS    = {12, 168, 44, 44};
static const UiRect UI_R_PLUS     = {184, 168, 44, 44};
static const UiRect UI_R_TRACKROW = {60, 168, 120, 44};  // generous drag row
static const int16_t UI_TRACK_X = 66, UI_TRACK_W = 108, UI_TRACK_Y = 186;
#if UI_LANDSCAPE
static const UiRect UI_R_PERS     = {8, 120, 304, 28};
static const int16_t UI_CHIP_X[4] = {8, 86, 164, 242};
static const int16_t UI_CHIP_Y[2] = {166, 204};
#define UI_CHIP_W 70
#define UI_CHIP_H 32
#define UI_MOOD_INFO_Y 157
#else
static const UiRect UI_R_PERS     = {12, 132, 216, 32};
static const int16_t UI_CHIP_X[4] = {12, 67, 122, 177};
static const int16_t UI_CHIP_Y[2] = {188, 228};
#define UI_CHIP_W 51
#define UI_CHIP_H 36
#define UI_MOOD_INFO_Y 176
#endif

// Landscape dock rails and scene chips (PS3_Dock / PS3_Scene)
static const int16_t UI_RAIL_X[2] = {4, 260};
static const int16_t UI_RAIL_Y[4] = {6, 64, 122, 180};
static const UiRect UI_R3_PILL    = {78, 6, 164, 24};
static const UiRect UI_R3_AUTO    = {4, 60, 56, 48};
static const UiRect UI_R3_KSCOPE  = {4, 114, 56, 48};
static const UiRect UI_R3_PALETTE = {260, 60, 56, 48};
static const UiRect UI_R3_HIRES   = {260, 114, 56, 48};
static const UiRect UI_R3_PREV    = {50, 188, 44, 44};
static const UiRect UI_R3_SCENE   = {100, 188, 120, 44};
static const UiRect UI_R3_NEXT    = {226, 188, 44, 44};
#define UI_CHIPS_PER_PAGE 8
#define UI_MOOD_PAGES ((BOT_NUM_EXPRESSIONS + UI_CHIPS_PER_PAGE - 1) / UI_CHIPS_PER_PAGE)

static const char* const uiExprNames[BOT_NUM_EXPRESSIONS] = {
  "Neutral", "Happy", "Sad", "Surprised", "Chill", "Angry", "Love", "Dizzy",
  "Thinking", "Excited", "Mischief", "Skeptical", "Worried", "Confused", "Proud", "Shy",
  "Annoyed", "Focused", "Winking", "Devious", "Shocked", "Kissing", "Nervous", "Glitching", "Sassy"
};

// Injected gestures from the dev endpoint /debug/touch (web_server.h)
extern volatile uint8_t uiInjectGesture;   // 0 none, 1 tap, 2 long, 3..6 swipe up/down/left/right
extern volatile int16_t uiInjectX, uiInjectY;

// ---- State ----------------------------------------------------------------
struct UiState {
  UiScreen screen = UI_HOME;
  UiScreen prevScreen = UI_HOME;
  unsigned long screenSince = 0;
  unsigned long lastTouch = 0;

  // Face animation (0 = full face, 1 = mini face)
  float faceFrom = 0, faceTo = 0;
  unsigned long faceAnimStart = 0;

  // Pressed feedback
  uint8_t pressedId = UI_ID_NONE;

  // Toast (replaces the top pill for a moment)
  char toast[40] = "";
  uint16_t toastHue = UI_C_TEXT;
  unsigned long toastUntil = 0;

  uint8_t moodPage = 0;
  bool dragging = false;

  // Settings pages
  uint8_t cat = CAT_LOOK;
  UiScreen backTo = UI_DOCK;     // where a sheet's back pill returns
  int16_t scroll = 0, scrollStart = 0;
  bool scrolling = false;
  int8_t holdRow = -1;           // row being held (hold-to-confirm)
  unsigned long holdStart = 0;
  UiRect backRect = {78, 6, 84, 24};
  int8_t dragRow = -1;           // slider row being dragged

  // "Psst! Swipe up" hint
  uint8_t hintsShown = 0;
  bool bootHintDone = false;
  bool idleHintDone = false;
};
static UiState ui;

// Raw gesture tracking
struct UiTouchTrack {
  bool down = false;
  int16_t x0 = 0, y0 = 0, x = 0, y = 0;
  unsigned long t0 = 0, lastSeen = 0;
  bool longFired = false;
};
static UiTouchTrack uiTrack;

// ---- Small helpers --------------------------------------------------------
static inline uint16_t uiBlend(uint16_t a, uint16_t b, float t) {
  // Blend RGB565 a→b by t (0..1)
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

static inline float uiSmooth(float t) { return t * t * (3.0f - 2.0f * t); }

static void uiText(const char* s, int16_t x, int16_t y, const lgfx::IFont* font,
                   uint16_t color, uint8_t datum, bool bold = false) {
  gfx->setFont(font);
  gfx->setTextSize(1);
  gfx->setTextDatum(datum);
  gfx->setTextColor(color);
  gfx->drawString(s, x, y);
  if (bold) gfx->drawString(s, x + 1, y);
  // Leave the defaults the rest of the firmware assumes
  gfx->setTextDatum(0);
  gfx->setFont(&fonts::Font0);
}

static inline int16_t uiTextW(const char* s, const lgfx::IFont* font) {
  gfx->setFont(font);
  int16_t w = gfx->textWidth(s);
  gfx->setFont(&fonts::Font0);
  return w;
}

// Datums (LovyanGFX textdatum_t: bit 2 = middle, bits 0-1 = left/center/right)
#define UI_TL 0
#define UI_TC 1
#define UI_ML 4
#define UI_MC 5
#define UI_MR 6

// ---- Icons: 24-unit stroke icons scaled to any size -----------------------
enum UiIcon : uint8_t {
  IC_MOOD, IC_SCENE, IC_WEATHER, IC_CLOCK, IC_LIGHT, IC_MORE, IC_WIFI, IC_HOTSPOT,
  IC_BACK, IC_FWD, IC_AUTO, IC_MINUS, IC_PLUS, IC_MOON, IC_HIRES, IC_SYSTEM,
  IC_SOUND, IC_HEAD, IC_GAMES, IC_KALEIDO
};

static float _icS, _icW;
static int16_t _icX, _icY;
static uint16_t _icC, _icBg;
#define ICX(v) (_icX + (v) * _icS)
#define ICY(v) (_icY + (v) * _icS)

static void icLine(float x0, float y0, float x1, float y1) {
  gfx->drawWideLine(ICX(x0), ICY(y0), ICX(x1), ICY(y1), _icW, _icC);
}
static void icRing(float cx, float cy, float r, float a0 = 0, float a1 = 360) {
  float rr = r * _icS;
  gfx->fillArc((int32_t)lroundf(ICX(cx)), (int32_t)lroundf(ICY(cy)),
               (int32_t)lroundf(rr - _icW), (int32_t)lroundf(rr + _icW), a0, a1, _icC);
}
static void icDot(float cx, float cy, float r, uint16_t c) {
  gfx->fillCircle((int32_t)lroundf(ICX(cx)), (int32_t)lroundf(ICY(cy)), max(1L, lroundf(r * _icS)), c);
}
static void icQuad(float x0, float y0, float cx, float cy, float x1, float y1) {
  float px = x0, py = y0;
  for (int i = 1; i <= 8; i++) {
    float t = i / 8.0f, u = 1 - t;
    float qx = u * u * x0 + 2 * u * t * cx + t * t * x1;
    float qy = u * u * y0 + 2 * u * t * cy + t * t * y1;
    icLine(px, py, qx, qy);
    px = qx; py = qy;
  }
}
static void icRRect(float x, float y, float w, float h, float r) {
  int32_t X = lroundf(ICX(x)), Y = lroundf(ICY(y)), W = lroundf(w * _icS), H = lroundf(h * _icS), R = lroundf(r * _icS);
  gfx->drawRoundRect(X, Y, W, H, R, _icC);
  if (_icW >= 0.75f) gfx->drawRoundRect(X + 1, Y + 1, W - 2, H - 2, max(0L, (long)R - 1), _icC);
}

// Draw icon in a size×size box at (x, y). strokeW is in 24-unit space.
static void uiIcon(UiIcon id, int16_t x, int16_t y, int16_t size, uint16_t color,
                   uint16_t bg = UI_C_TILE, float strokeW = 2.0f) {
  _icS = size / 24.0f;
  _icW = max(0.6f, strokeW * _icS * 0.5f);  // drawWideLine takes a radius
  _icX = x; _icY = y; _icC = color; _icBg = bg;
  switch (id) {
    case IC_MOOD:
      icRing(12, 12, 9);
      icDot(9, 10, 1.4f, color); icDot(15, 10, 1.4f, color);
      icQuad(8, 14.5f, 12, 18.5f, 16, 14.5f);
      break;
    case IC_SCENE:
      icRRect(3, 4, 18, 16, 3);
      icQuad(6, 15, 9, 9, 12, 13); icQuad(12, 13, 15, 17, 18, 10);
      break;
    case IC_WEATHER:
      icRing(17.5f, 7, 3);
      icLine(17.5f, 1.5f, 17.5f, 2); icLine(22.5f, 7, 22, 7); icLine(21.2f, 3.3f, 20.8f, 3.7f);
      icLine(6, 20, 14, 20);
      icRing(14, 16.5f, 3.5f, 270, 450);
      icRing(10, 14.2f, 4.6f, 170, 330);
      icRing(6, 17.3f, 2.7f, 90, 270);
      break;
    case IC_CLOCK:
      icRing(12, 12, 9);
      icLine(12, 7, 12, 12); icLine(12, 12, 15.5f, 14);
      break;
    case IC_LIGHT:
      icRing(12, 12, 4);
      icLine(12, 2.5f, 12, 4.5f); icLine(12, 19.5f, 12, 21.5f);
      icLine(2.5f, 12, 4.5f, 12); icLine(19.5f, 12, 21.5f, 12);
      icLine(5.3f, 5.3f, 6.7f, 6.7f); icLine(17.3f, 17.3f, 18.7f, 18.7f);
      icLine(5.3f, 18.7f, 6.7f, 17.3f); icLine(17.3f, 6.7f, 18.7f, 5.3f);
      break;
    case IC_MORE:
      icLine(4, 7, 20, 7); icLine(4, 17, 20, 17);
      icDot(9, 7, 2.6f, color); icDot(15, 17, 2.6f, color);
      break;
    case IC_WIFI:
      icRing(12, 18.7f, 13.5f, 225, 315);
      icRing(12, 18.7f, 9.0f, 225, 315);
      icRing(12, 18.7f, 4.5f, 225, 315);
      icDot(12, 19.5f, 1.4f, color);
      break;
    case IC_HOTSPOT:
      icDot(12, 12, 2, color);
      icRing(12, 12, 5.4f, 135, 225); icRing(12, 12, 5.4f, 315, 405);
      icRing(12, 12, 9.6f, 135, 225); icRing(12, 12, 9.6f, 315, 405);
      break;
    case IC_BACK:
      icLine(15, 5, 8, 12); icLine(8, 12, 15, 19);
      break;
    case IC_FWD:
      icLine(9, 5, 16, 12); icLine(16, 12, 9, 19);
      break;
    case IC_AUTO:
      icRing(12, 12, 8, 0, 315);
      icLine(20, 4, 20, 8.5f); icLine(20, 8.5f, 15.5f, 8.5f);
      break;
    case IC_MINUS:
      icLine(5, 12, 19, 12);
      break;
    case IC_PLUS:
      icLine(5, 12, 19, 12); icLine(12, 5, 12, 19);
      break;
    case IC_MOON:
      icDot(11, 13, 8.5f, color);
      icDot(16, 8.5f, 7.0f, bg);
      break;
    case IC_SYSTEM:
      icRRect(6, 6, 12, 12, 2);
      icLine(9.5f, 2.5f, 9.5f, 5.5f); icLine(14.5f, 2.5f, 14.5f, 5.5f);
      icLine(9.5f, 18.5f, 9.5f, 21.5f); icLine(14.5f, 18.5f, 14.5f, 21.5f);
      icLine(2.5f, 9.5f, 5.5f, 9.5f); icLine(2.5f, 14.5f, 5.5f, 14.5f);
      icLine(18.5f, 9.5f, 21.5f, 9.5f); icLine(18.5f, 14.5f, 21.5f, 14.5f);
      break;
    case IC_SOUND:
      icLine(4, 9.5f, 7.5f, 9.5f); icLine(7.5f, 9.5f, 12, 5.5f); icLine(12, 5.5f, 12, 18.5f);
      icLine(12, 18.5f, 7.5f, 14.5f); icLine(7.5f, 14.5f, 4, 14.5f); icLine(4, 14.5f, 4, 9.5f);
      icRing(13.5f, 12, 4, 300, 420); icRing(13.5f, 12, 7.5f, 300, 420);
      break;
    case IC_HEAD:
      icRRect(4, 7, 16, 13, 4);
      icDot(9, 13, 1.4f, color); icDot(15, 13, 1.4f, color);
      icLine(12, 7, 12, 4); icDot(12, 3, 1, color);
      break;
    case IC_GAMES:
      icRRect(2, 7, 20, 11, 5.5f);
      icLine(7, 10, 7, 15); icLine(4.5f, 12.5f, 9.5f, 12.5f);
      icDot(15.5f, 11.5f, 1.2f, color); icDot(18, 14, 1.2f, color);
      break;
    case IC_KALEIDO:
      icLine(12, 3, 19.8f, 7.5f); icLine(19.8f, 7.5f, 19.8f, 16.5f); icLine(19.8f, 16.5f, 12, 21);
      icLine(12, 21, 4.2f, 16.5f); icLine(4.2f, 16.5f, 4.2f, 7.5f); icLine(4.2f, 7.5f, 12, 3);
      icLine(12, 3, 12, 21); icLine(4.2f, 7.5f, 19.8f, 16.5f); icLine(19.8f, 7.5f, 4.2f, 16.5f);
      break;
    case IC_HIRES:
      icRRect(4, 4, 7, 7, 1); icRRect(13, 4, 7, 7, 1);
      icRRect(4, 13, 7, 7, 1); icRRect(13, 13, 7, 7, 1);
      break;
  }
}

// ---- Scene model: 0 = Black, 1..16 = ambient effect behind the face -------
#define UI_NUM_SCENES (NUM_AMBIENT_EFFECTS + 1)

static uint8_t uiSceneIndex() {
  return getBotBackgroundStyle() == 4 ? (uint8_t)(effectIndex % NUM_AMBIENT_EFFECTS) + 1 : 0;
}
static const char* uiSceneName(uint8_t i) {
  return i == 0 ? "Black" : ambientEffectNames[(i - 1) % NUM_AMBIENT_EFFECTS];
}
static void uiSetScene(uint8_t i) {
  i %= UI_NUM_SCENES;
  if (i == 0) {
    setBotBackgroundStyle(0);
  } else {
    setBotBackgroundStyle(4);
    effectIndex = i - 1;
  }
  // A hand-picked scene should stick: auto-cycle would replace it within 20 s
  autoCycle = false;
  lastChange = millis();
  markSettingsDirty();
}

// UI sounds: CoreS3 has a speaker; the 1.69 stays silent
#ifdef TARGET_CORES3
#define uiSound(seq) botSounds.play(seq)
#else
#define uiSound(seq) ((void)0)
#endif

// ---- Screen changes -------------------------------------------------------
static bool uiIsSheet(UiScreen s) { return s != UI_HOME; }
static bool uiIsFullPage(UiScreen s) { return s == UI_SETTINGS || s == UI_CAT; }
// Landscape keeps the full face for the dock and scene (the rails sit beside
// it); only the mood sheet needs the room.
static bool uiShrinksFace(UiScreen s) {
#if UI_LANDSCAPE
  return s == UI_MOOD || s == UI_LIGHT;
#else
  return s != UI_HOME;
#endif
}

static float uiFaceAnimValue() {
  unsigned long dt = millis() - ui.faceAnimStart;
  if (dt >= UI_FACE_ANIM_MS) return ui.faceTo;
  return ui.faceFrom + (ui.faceTo - ui.faceFrom) * uiSmooth((float)dt / UI_FACE_ANIM_MS);
}

static void uiGo(UiScreen s) {
  if (s == ui.screen) return;
  if (s == UI_HOME) uiSound(SEQ_DISMISS);
  else if (ui.screen == UI_HOME) uiSound(SEQ_SWIPE);
  float target = uiShrinksFace(s) || uiIsFullPage(s) ? 1.0f : 0.0f;
  ui.faceFrom = uiFaceAnimValue();
  ui.faceTo = target;
  ui.faceAnimStart = millis();
  ui.prevScreen = ui.screen;
  ui.screen = s;
  ui.screenSince = millis();
  ui.pressedId = UI_ID_NONE;
  ui.dragging = false;
  ui.dragRow = -1;
  ui.scrolling = false;
  ui.holdRow = -1;
  if (s == UI_CAT || s == UI_SETTINGS) ui.scroll = 0;
  if (s == UI_MOOD) ui.moodPage = min((uint8_t)(botMode.face.targetExpr / UI_CHIPS_PER_PAGE), (uint8_t)(UI_MOOD_PAGES - 1));
}

static void uiToast(const char* text, uint16_t hue, uint16_t ms = 1500) {
  strncpy(ui.toast, text, sizeof(ui.toast) - 1);
  ui.toast[sizeof(ui.toast) - 1] = 0;
  ui.toastHue = hue;
  ui.toastUntil = millis() + ms;
}

// ---- Called from renderBotMode() ------------------------------------------
void uiFaceTransform(int16_t &cx, int16_t &cy, float &scale) {
  float t = uiFaceAnimValue();
  cx = BOT_FACE_CX;
  cy = (int16_t)lroundf(BOT_FACE_CY + (UI_MINI_CY - BOT_FACE_CY) * t);
  scale = 1.0f + (UI_MINI_SCALE - 1.0f) * t;
}

bool uiFullScreen() { return uiIsFullPage(ui.screen); }

bool uiHidesBotOverlays() {
  return ui.screen != UI_HOME || uiFaceAnimValue() > 0.01f;
}

// ---- Widgets --------------------------------------------------------------
static void uiPill(const UiRect& r, uint16_t bg, uint16_t border) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, r.h / 2, bg);
  if (border != bg) gfx->drawRoundRect(r.x, r.y, r.w, r.h, r.h / 2, border);
}

static bool uiToastActive() { return ui.toast[0] && (long)(ui.toastUntil - millis()) > 0; }

static void uiDrawToast() {
  int16_t w = uiTextW(ui.toast, &fonts::DejaVu12) + 28;
  if (w < 84) w = 84;
  if (w > 216) w = 216;
  UiRect r = {(int16_t)(UI_CX - w / 2), 6, w, 24};
  uiPill(r, ui.toastHue, ui.toastHue);
  uiText(ui.toast, UI_CX, 18, &fonts::DejaVu12, UI_C_BG, UI_MC, true);
}

static void uiDrawBackPill(const char* label, uint16_t hue, uint16_t bg, int16_t x = -1) {
  bool pressed = ui.pressedId == UI_ID_BACK;
  int16_t w = max((int16_t)84, (int16_t)(uiTextW(label, &fonts::DejaVu12) + 44));
  ui.backRect = {(int16_t)(x >= 0 ? x : UI_CX - w / 2), 6, w, 24};
  const UiRect& r = ui.backRect;
  uiPill(r, pressed ? hue : bg, pressed ? hue : UI_C_STROKE);
  uiIcon(IC_BACK, r.x + 10, r.y + 5, 14, pressed ? UI_C_BG : hue, bg, 2.6f);
  uiText(label, r.x + 28, r.y + 12, &fonts::DejaVu12, pressed ? UI_C_BG : UI_C_TEXT, UI_ML, true);
}

static void uiDrawSheet() {
  gfx->fillRoundRect(0, UI_SHEET_TOP, LCD_WIDTH, LCD_HEIGHT - UI_SHEET_TOP + 20, 16, UI_C_SURFACE);
  gfx->drawRoundRect(0, UI_SHEET_TOP, LCD_WIDTH, LCD_HEIGHT - UI_SHEET_TOP + 20, 16, UI_C_STROKE);
}

static void uiDrawTile(const UiRect& r, UiIcon icon, const char* label, uint16_t hue, bool on, bool pressed) {
  uint16_t bg = pressed ? hue : (on ? uiBlend(UI_C_TILE, hue, 0.22f) : UI_C_TILE);
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
  if (on && !pressed) {
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 10, hue);
    gfx->fillCircle(r.x + r.w - 9, r.y + 9, 3, hue);
  }
  uint16_t fg = pressed ? UI_C_BG : UI_C_TEXT;
  uiIcon(icon, r.x + (r.w - 24) / 2, r.y + 6, 24, pressed ? UI_C_BG : hue, bg);
  uiText(label, r.x + r.w / 2, r.y + 36, &fonts::DejaVu12, fg, UI_TC, pressed || on);
}

// Outlined black button used on the scene screen (sits over the live effect)
static void uiDrawOutlineBtn(const UiRect& r, bool pressed, uint16_t hue) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? hue : UI_C_BG);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? hue : UI_C_STROKE);
}

// ---- Screens --------------------------------------------------------------
static const char* uiHostLabel() {
  static char buf[40];
  if (sysStatus.staConnected) snprintf(buf, sizeof(buf), "%s.local", mdnsHostname);
  else snprintf(buf, sizeof(buf), "%s", apSSID);
  return buf;
}

#if UI_LANDSCAPE
#define UI_PILL_RECT UI_R3_PILL
#else
#define UI_PILL_RECT UI_R_PILL
#endif

static void uiRenderTopPill() {
  if (uiToastActive()) { uiDrawToast(); return; }
  bool pressed = ui.pressedId == UI_ID_PILL;
  uiPill(UI_PILL_RECT, pressed ? UI_C_CONNECT : UI_C_SURFACE, pressed ? UI_C_CONNECT : UI_C_STROKE);
  const char* host = uiHostLabel();
  int16_t tw = uiTextW(host, &fonts::DejaVu12);
  int16_t x = UI_CX - (tw + 18) / 2;
  uiIcon(sysStatus.staConnected ? IC_WIFI : IC_HOTSPOT, x, 11, 14,
         pressed ? UI_C_BG : UI_C_CONNECT, UI_C_SURFACE, 2.4f);
  uiText(host, x + 18, 18, &fonts::DejaVu12, pressed ? UI_C_BG : UI_C_SYSTEM, UI_ML);
}

// Dock tiles: portrait is a 3x2 grid in a sheet, landscape two 4-tile rails.
// Landscape slot 7 depends on the build: Head (Stackchan), Games (Faces) or
// Connect (plain CoreS3).
enum UiTileAct : uint8_t { TA_MOOD, TA_SCENE, TA_WEATHER, TA_CLOCK, TA_LIGHT, TA_SOUND, TA_HEAD, TA_GAMES, TA_CONNECT, TA_MORE };
struct UiTile { UiIcon ic; const char* label; uint16_t hue; UiTileAct act; };
#if UI_LANDSCAPE
#define UI_DOCK_TILES 8
static const UiTile UI_DOCK_DEF[UI_DOCK_TILES] = {
  {IC_MOOD, "Mood", UI_C_MOOD, TA_MOOD}, {IC_SCENE, "Scene", UI_C_LOOK, TA_SCENE},
  {IC_WEATHER, "Weather", UI_C_INFO, TA_WEATHER}, {IC_CLOCK, "Clock", UI_C_INFO, TA_CLOCK},
  {IC_LIGHT, "Light", UI_C_LIGHT, TA_LIGHT}, {IC_SOUND, "Sound", UI_C_LIGHT, TA_SOUND},
#if defined(BOARD_HAS_STACKCHAN_BASE)
  {IC_HEAD, "Head", UI_C_MOOD, TA_HEAD},
#elif defined(BOARD_HAS_FACES_BASE)
  {IC_GAMES, "Games", UI_C_CONNECT, TA_GAMES},
#else
  {IC_WIFI, "Connect", UI_C_CONNECT, TA_CONNECT},
#endif
  {IC_MORE, "More", UI_C_SYSTEM, TA_MORE},
};
static UiRect uiDockRect(uint8_t i) { return {UI_RAIL_X[i / 4], UI_RAIL_Y[i % 4], 56, 52}; }
#else
#define UI_DOCK_TILES 6
static const UiTile UI_DOCK_DEF[UI_DOCK_TILES] = {
  {IC_MOOD, "Mood", UI_C_MOOD, TA_MOOD}, {IC_SCENE, "Scene", UI_C_LOOK, TA_SCENE},
  {IC_WEATHER, "Weather", UI_C_INFO, TA_WEATHER}, {IC_CLOCK, "Clock", UI_C_INFO, TA_CLOCK},
  {IC_LIGHT, "Light", UI_C_LIGHT, TA_LIGHT}, {IC_MORE, "More", UI_C_SYSTEM, TA_MORE},
};
static UiRect uiDockRect(uint8_t i) { return {UI_TILE_X[i % 3], UI_TILE_Y[i / 3], 68, 52}; }
#endif

static void uiRenderDock() {
#if !UI_LANDSCAPE
  uiDrawSheet();
  gfx->fillRoundRect(104, 130, 32, 4, 2, UI_C_HANDLE);
#endif
  for (uint8_t i = 0; i < UI_DOCK_TILES; i++) {
    bool on = UI_DOCK_DEF[i].act == TA_CLOCK && isBotTimeOverlayEnabled();
    uiDrawTile(uiDockRect(i), UI_DOCK_DEF[i].ic, UI_DOCK_DEF[i].label, UI_DOCK_DEF[i].hue, on, ui.pressedId == UI_ID_TILE0 + i);
  }
  uiRenderTopPill();
}

#if UI_LANDSCAPE
// Option chip on the scene rails: black so it reads over the live effect
static void uiDrawSceneChip(const UiRect& r, UiIcon ic, const char* label, bool on, bool pressed) {
  uint16_t bg = pressed ? UI_C_LOOK : (on ? uiBlend(UI_C_BG, UI_C_LOOK, 0.28f) : UI_C_BG);
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 10, on || pressed ? UI_C_LOOK : UI_C_STROKE);
  uint16_t fg = pressed ? UI_C_BG : (on ? UI_C_LOOK : UI_C_DIM);
  uiIcon(ic, r.x + (r.w - 20) / 2, r.y + 6, 20, fg, bg);
  uiText(label, r.x + r.w / 2, r.y + 36, &fonts::DejaVu12, pressed ? UI_C_BG : (on ? UI_C_TEXT : UI_C_DIM), UI_MC, on);
}

static void uiRenderScene() {
  if (uiToastActive()) uiDrawToast();
  else uiDrawBackPill("Scene", UI_C_LOOK, UI_C_BG);
  uint8_t pid = ui.pressedId;
  uiDrawSceneChip(UI_R3_AUTO, IC_AUTO, "Auto", autoCycle, pid == UI_ID_AUTO);
  uiDrawSceneChip(UI_R3_KSCOPE, IC_KALEIDO, "Kaleido", kaleidoscopeMode != 0, pid == UI_ID_KSCOPE);
  uiDrawSceneChip(UI_R3_HIRES, IC_HIRES, "Hi-res", hiResMode, pid == UI_ID_HIRES);
  // Palette chip shows the palette itself instead of an icon
  {
    const UiRect& r = UI_R3_PALETTE;
    bool pr = pid == UI_ID_PALETTE;
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 10, pr ? UI_C_LOOK : UI_C_BG);
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 10, pr ? UI_C_LOOK : UI_C_STROKE);
    for (uint8_t k = 0; k < 5; k++) {
      CRGB c = ColorFromPalette(currentPalette, k * 51);
      gfx->fillRect(r.x + 10 + k * 7, r.y + 10, 7, 12, crgbToRgb565(c));
    }
    uiText("Palette", r.x + r.w / 2, r.y + 36, &fonts::DejaVu12, pr ? UI_C_BG : UI_C_DIM, UI_MC);
  }
  uint8_t si = uiSceneIndex();
  uiDrawOutlineBtn(UI_R3_PREV, pid == UI_ID_SCENE_PREV, UI_C_LOOK);
  uiIcon(IC_BACK, UI_R3_PREV.x + 12, UI_R3_PREV.y + 12, 20, pid == UI_ID_SCENE_PREV ? UI_C_BG : UI_C_TEXT, UI_C_BG, 2.6f);
  uiDrawOutlineBtn(UI_R3_NEXT, pid == UI_ID_SCENE_NEXT, UI_C_LOOK);
  uiIcon(IC_FWD, UI_R3_NEXT.x + 12, UI_R3_NEXT.y + 12, 20, pid == UI_ID_SCENE_NEXT ? UI_C_BG : UI_C_TEXT, UI_C_BG, 2.6f);
  uiDrawOutlineBtn(UI_R3_SCENE, false, UI_C_LOOK);
  uiText(uiSceneName(si), UI_CX, UI_R3_SCENE.y + 15, &fonts::DejaVu18, UI_C_TEXT, UI_MC, true);
  char pos[16];
  snprintf(pos, sizeof(pos), "%u of %u", si + 1, UI_NUM_SCENES);
  uiText(pos, UI_CX, UI_R3_SCENE.y + 34, &fonts::DejaVu12, UI_C_DIM, UI_MC);
}
#else
static void uiRenderScene() {
  if (uiToastActive()) uiDrawToast();
  else uiDrawBackPill("Scene", UI_C_LOOK, UI_C_BG);

  uint8_t si = uiSceneIndex();
  uiDrawOutlineBtn(UI_R_PREV, ui.pressedId == UI_ID_SCENE_PREV, UI_C_LOOK);
  uiIcon(IC_BACK, UI_R_PREV.x + 12, UI_R_PREV.y + 12, 20, ui.pressedId == UI_ID_SCENE_PREV ? UI_C_BG : UI_C_TEXT, UI_C_BG, 2.6f);
  uiDrawOutlineBtn(UI_R_NEXT, ui.pressedId == UI_ID_SCENE_NEXT, UI_C_LOOK);
  uiIcon(IC_FWD, UI_R_NEXT.x + 12, UI_R_NEXT.y + 12, 20, ui.pressedId == UI_ID_SCENE_NEXT ? UI_C_BG : UI_C_TEXT, UI_C_BG, 2.6f);

  uiDrawOutlineBtn(UI_R_SCENE, false, UI_C_LOOK);
  uiText(uiSceneName(si), 120, UI_R_SCENE.y + 15, &fonts::DejaVu18, UI_C_TEXT, UI_MC, true);
  char pos[16];
  snprintf(pos, sizeof(pos), "%u of %u", si + 1, UI_NUM_SCENES);
  uiText(pos, 120, UI_R_SCENE.y + 34, &fonts::DejaVu12, UI_C_DIM, UI_MC);

  // Palette row: swatch + name
  bool pp = ui.pressedId == UI_ID_PALETTE;
  uiDrawOutlineBtn(UI_R_PALETTE, pp, UI_C_LOOK);
  for (uint8_t k = 0; k < 5; k++) {
    CRGB c = ColorFromPalette(currentPalette, k * 51);
    gfx->fillRect(UI_R_PALETTE.x + 10 + k * 10, UI_R_PALETTE.y + 12, 10, 10, crgbToRgb565(c));
  }
  uiText(paletteNames[paletteIndex % NUM_PALETTES], UI_R_PALETTE.x + 70, UI_R_PALETTE.y + 17,
         &fonts::DejaVu12, pp ? UI_C_BG : UI_C_TEXT, UI_ML);
  uiText("Palette", UI_R_PALETTE.x + UI_R_PALETTE.w - 10, UI_R_PALETTE.y + 17, &fonts::DejaVu12, pp ? UI_C_BG : UI_C_DIM, UI_MR);

  // Pixel / Hi-res segmented control
  gfx->fillRoundRect(UI_R_SEG.x, UI_R_SEG.y, UI_R_SEG.w, UI_R_SEG.h, 10, UI_C_BG);
  gfx->drawRoundRect(UI_R_SEG.x, UI_R_SEG.y, UI_R_SEG.w, UI_R_SEG.h, 10, UI_C_STROKE);
  bool hr = hiResMode;
  int16_t half = UI_R_SEG.w / 2;
  UiRect segOn = {(int16_t)(UI_R_SEG.x + (hr ? half : 0) + 2), (int16_t)(UI_R_SEG.y + 2), (int16_t)(half - 4), (int16_t)(UI_R_SEG.h - 4)};
  gfx->fillRoundRect(segOn.x, segOn.y, segOn.w, segOn.h, 8, UI_C_LOOK);
  uiText("Pixel", UI_R_SEG.x + half / 2, UI_R_SEG.y + 17, &fonts::DejaVu12, hr ? UI_C_TEXT : UI_C_BG, UI_MC, !hr);
  uiText("Hi-res", UI_R_SEG.x + half + half / 2, UI_R_SEG.y + 17, &fonts::DejaVu12, hr ? UI_C_BG : UI_C_TEXT, UI_MC, hr);

  // Auto-cycle toggle
  bool ap = ui.pressedId == UI_ID_AUTO;
  uint16_t aBg = ap ? UI_C_LOOK : (autoCycle ? uiBlend(UI_C_BG, UI_C_LOOK, 0.25f) : UI_C_BG);
  gfx->fillRoundRect(UI_R_AUTO.x, UI_R_AUTO.y, UI_R_AUTO.w, UI_R_AUTO.h, 10, aBg);
  gfx->drawRoundRect(UI_R_AUTO.x, UI_R_AUTO.y, UI_R_AUTO.w, UI_R_AUTO.h, 10, autoCycle || ap ? UI_C_LOOK : UI_C_STROKE);
  uint16_t aFg = ap ? UI_C_BG : (autoCycle ? UI_C_TEXT : UI_C_DIM);
  uiIcon(IC_AUTO, UI_R_AUTO.x + 18, UI_R_AUTO.y + 9, 16, ap ? UI_C_BG : (autoCycle ? UI_C_LOOK : UI_C_DIM), aBg, 2.4f);
  uiText(autoCycle ? "Auto on" : "Auto off", UI_R_AUTO.x + 40, UI_R_AUTO.y + 17, &fonts::DejaVu12, aFg, UI_ML, autoCycle);
}
#endif

static uint8_t uiBrightPct() { return (uint8_t)((lcdBrightness * 100 + 127) / 255); }

static void uiSetBrightPct(int pct) {
  if (pct < 5) pct = 5;
  if (pct > 100) pct = 100;
  lcdBrightness = (uint8_t)((pct * 255 + 50) / 100);
  setLCDBacklight(lcdBrightness);
  markSettingsDirty();
}

static void uiRenderLight() {
  uiDrawSheet();
  if (uiToastActive()) uiDrawToast();
  else uiDrawBackPill("Light", UI_C_LIGHT, UI_C_SURFACE);

  uiIcon(IC_LIGHT, 16, 138, 20, UI_C_LIGHT, UI_C_SURFACE);
  uiText("Screen", 44, 148, &fonts::DejaVu18, UI_C_TEXT, UI_ML);
  char pct[8];
  uint8_t p = uiBrightPct();
  snprintf(pct, sizeof(pct), "%u%%", p);
  uiText(pct, 228, 148, &fonts::DejaVu24, UI_C_LIGHT, UI_MR, true);

  bool mp = ui.pressedId == UI_ID_MINUS, pp = ui.pressedId == UI_ID_PLUS;
  gfx->fillRoundRect(UI_R_MINUS.x, UI_R_MINUS.y, 44, 44, 12, mp ? UI_C_LIGHT : UI_C_TILE);
  uiIcon(IC_MINUS, UI_R_MINUS.x + 12, UI_R_MINUS.y + 12, 20, mp ? UI_C_BG : UI_C_TEXT, UI_C_TILE, 2.6f);
  gfx->fillRoundRect(UI_R_PLUS.x, UI_R_PLUS.y, 44, 44, 12, pp ? UI_C_LIGHT : UI_C_TILE);
  uiIcon(IC_PLUS, UI_R_PLUS.x + 12, UI_R_PLUS.y + 12, 20, pp ? UI_C_BG : UI_C_TEXT, UI_C_TILE, 2.6f);

  int16_t fill = (int16_t)((p - 5) * UI_TRACK_W / 95);
  gfx->fillRoundRect(UI_TRACK_X, UI_TRACK_Y, UI_TRACK_W, 8, 4, UI_C_TRACK);
  if (fill > 0) gfx->fillRoundRect(UI_TRACK_X, UI_TRACK_Y, max((int16_t)8, fill), 8, 4, UI_C_LIGHT);
  int16_t kx = UI_TRACK_X + fill;
  gfx->fillCircle(kx, UI_TRACK_Y + 4, 10, UI_C_LIGHT);
  gfx->fillCircle(kx, UI_TRACK_Y + 4, 8, ui.dragging ? UI_C_LIGHT : UI_C_TEXT);

  struct { UiIcon ic; const char* label; uint8_t pct; } pre[3] = {
    {IC_MOON, "Night", 10}, {IC_LIGHT, "Day", 60}, {IC_PLUS, "Max", 100}};
  for (uint8_t i = 0; i < 3; i++) {
    UiRect r = {UI_TILE_X[i], 222, 68, 34};
    bool pr = ui.pressedId == UI_ID_PRESET0 + i;
    bool cur = p == pre[i].pct;
    uint16_t bg = pr ? UI_C_LIGHT : (cur ? uiBlend(UI_C_TILE, UI_C_LIGHT, 0.22f) : UI_C_TILE);
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
    if (cur && !pr) gfx->drawRoundRect(r.x, r.y, r.w, r.h, 10, UI_C_LIGHT);
    int16_t tw = uiTextW(pre[i].label, &fonts::DejaVu12);
    int16_t x0 = r.x + (r.w - (tw + 19)) / 2;
    uiIcon(pre[i].ic, x0, r.y + 10, 14, pr ? UI_C_BG : (cur ? UI_C_LIGHT : UI_C_DIM), bg, 2.4f);
    uiText(pre[i].label, x0 + 19, r.y + 17, &fonts::DejaVu12, pr ? UI_C_BG : UI_C_TEXT, UI_ML, cur);
  }
}

// Tiny line-art version of an expression for the mood chips. The full face
// renderer uses fixed pixel strokes and thresholds that fall apart at 1/5 size,
// so this redraws the same geometry with hairline strokes.
static void uiDrawExprGlyph(uint8_t e, int16_t cx, int16_t cy, float s, uint16_t bg) {
  BotFaceState g = botMode.face;
  g.loadExpression(e);
  const uint16_t W = botFaceColor, K = BOT_COLOR_PUPIL;
  // Eyes a bit smaller and mouth a bit bigger than true scale: at chip size
  // the mouth carries most of the expression.
  const float se = s * 0.8f, sm = s * 1.6f;
  float ew = max(3.0f, g.eyeWhiteW * se), eh = max(3.0f, g.eyeWhiteH * se);
  float sp = g.eyeSpacing * s * 0.9f, pr = max(1.5f, g.pupilRadius * se);
  float px = g.pupilOffsetX * s, py = g.pupilOffsetY * s;
  const float lw = 0.9f;  // drawWideLine radius → ~2px strokes
  for (int side = 0; side < 2; side++) {
    float ex = cx + (side ? sp : -sp);
    BotEyeMode m = g.eyeMode;
    if (m == EYE_WINK) m = side ? EYE_CLOSED : EYE_NORMAL;
    switch (m) {
      case EYE_CLOSED:
        gfx->drawWideLine(ex - ew, cy, ex + ew, cy, lw, W);
        break;
      case EYE_CARET:   // ^ happy squint
        gfx->drawWideLine(ex - ew, cy + eh * 0.4f, ex, cy - eh * 0.4f, lw, W);
        gfx->drawWideLine(ex, cy - eh * 0.4f, ex + ew, cy + eh * 0.4f, lw, W);
        break;
      case EYE_CURVED:  // u crescent
        gfx->drawWideLine(ex - ew, cy - eh * 0.3f, ex, cy + eh * 0.4f, lw, W);
        gfx->drawWideLine(ex, cy + eh * 0.4f, ex + ew, cy - eh * 0.3f, lw, W);
        break;
      default: {
        gfx->fillEllipse(ex, cy, ew, eh, W);
        if (m == EYE_HALF) gfx->fillRect(ex - ew - 1, cy - eh - 1, ew * 2 + 3, eh + 1, bg);
        float qx = ex + constrain(px, -(ew - pr), ew - pr), qy = cy + constrain(py, -(eh - pr), eh - pr);
        if (m == EYE_HALF) qy = max(qy, (float)cy + 1);
        if (m == EYE_X) {
          gfx->drawWideLine(qx - pr, qy - pr, qx + pr, qy + pr, 0.7f, K);
          gfx->drawWideLine(qx - pr, qy + pr, qx + pr, qy - pr, 0.7f, K);
        } else if (m == EYE_HEART) {
          gfx->fillCircle(qx, qy, pr + 1, 0xF800);
        } else if (m == EYE_SPIRAL) {
          gfx->drawCircle(qx, qy, pr + 1, K);
        } else if (m == EYE_DOT) {
          gfx->fillCircle(qx, qy, 1, K);
        } else {
          gfx->fillCircle(qx, qy, pr, K);
        }
      }
    }
  }
  if (g.browVisible) {
    float by = cy - eh + g.browOffsetY * s, bl = g.browLength * s;
    float rl = g.browAngleL * PI / 180.0f, rr = g.browAngleR * PI / 180.0f;
    float lx = cx - sp, rx = cx + sp;
    gfx->drawWideLine(lx - bl, by + sinf(-rl) * bl, lx + bl, by + sinf(rl) * bl, lw, W);
    gfx->drawWideLine(rx - bl, by + sinf(rr) * bl, rx + bl, by + sinf(-rr) * bl, lw, W);
  }
  float mx = cx, my = cy + g.mouthOffsetY * s, mw = max(3.0f, g.mouthWidth * sm), mc = g.mouthCurve * sm;
  auto curve = [&](float dip) {  // quadratic from (mx-mw,my) through the dip to (mx+mw,my)
    float ox = mx - mw, oy = my;
    for (int i = 1; i <= 6; i++) {
      float t = i / 6.0f, u = 1 - t;
      float x = u * u * (mx - mw) + 2 * u * t * mx + t * t * (mx + mw);
      float y = u * u * my + 2 * u * t * (my + dip * 2) + t * t * my;
      gfx->drawWideLine(ox, oy, x, y, lw, W);
      ox = x; oy = y;
    }
  };
  switch (g.mouthType) {
    case MOUTH_NONE: break;
    case MOUTH_LINE: gfx->drawWideLine(mx - mw, my, mx + mw, my, lw, W); break;
    case MOUTH_SMILE: case MOUTH_GRIN: case MOUTH_TEETH: case MOUTH_TONGUE:
      curve(max(1.5f, fabsf(mc))); break;
    case MOUTH_FROWN: curve(-max(1.5f, fabsf(mc))); break;
    case MOUTH_FLAT_FROWN:
      gfx->drawWideLine(mx - mw, my + 1, mx, my - 1.5f, lw, W);
      gfx->drawWideLine(mx, my - 1.5f, mx + mw, my + 1, lw, W);
      break;
    case MOUTH_OPEN_O: gfx->fillCircle(mx, my, max(2.0f, mw * 0.5f), W); gfx->fillCircle(mx, my, max(1.0f, mw * 0.5f - 1.5f), K); break;
    case MOUTH_WHISTLE: case MOUTH_POUT: gfx->fillCircle(mx, my, 2, W); break;
    case MOUTH_SMIRK:
      gfx->drawWideLine(mx - mw, my, mx + mw * 0.3f, my, lw, W);
      gfx->drawWideLine(mx + mw * 0.3f, my, mx + mw, my - 2, lw, W);
      break;
    case MOUTH_WAVY: case MOUTH_ZIGZAG: {
      float step = mw / 2;
      for (int i = 0; i < 4; i++) {
        float x0 = mx - mw + i * step, x1 = x0 + step;
        gfx->drawWideLine(x0, my + ((i & 1) ? 1.5f : -1.5f), x1, my + ((i & 1) ? -1.5f : 1.5f), lw, W);
      }
      break;
    }
  }
}

static void uiRenderMood() {
  uiDrawSheet();
  if (uiToastActive()) uiDrawToast();
  else uiDrawBackPill("Mood", UI_C_MOOD, UI_C_SURFACE);

  // Personality segmented control (the three built-ins)
  gfx->fillRoundRect(UI_R_PERS.x, UI_R_PERS.y, UI_R_PERS.w, UI_R_PERS.h, 9, UI_C_TILE);
  int16_t segW = UI_R_PERS.w / 3;
  for (uint8_t i = 0; i < 3; i++) {
    bool sel = botMode.personalityIndex == i;
    bool pr = ui.pressedId == UI_ID_PERS0 + i;
    int16_t sx = UI_R_PERS.x + i * segW;
    if (sel || pr) gfx->fillRoundRect(sx + 2, UI_R_PERS.y + 2, segW - 4, UI_R_PERS.h - 4, 7, UI_C_MOOD);
    const char* nm = i < runtimePersonalityCount ? runtimePersonalities[i].name : "-";
    uiText(nm, sx + segW / 2, UI_R_PERS.y + 16, &fonts::DejaVu12, (sel || pr) ? UI_C_BG : UI_C_TEXT, UI_MC, sel);
  }

  uint8_t cur = botMode.face.targetExpr;
  uiText(uiExprNames[cur % BOT_NUM_EXPRESSIONS], UI_R_PERS.x + 2, UI_MOOD_INFO_Y, &fonts::DejaVu12, UI_C_MOOD, UI_ML, true);
  int16_t dotsR = UI_R_PERS.x + UI_R_PERS.w;
  for (uint8_t d = 0; d < UI_MOOD_PAGES; d++) {
    gfx->fillCircle(dotsR - (UI_MOOD_PAGES - 1 - d) * 9 - 3, UI_MOOD_INFO_Y, 3, d == ui.moodPage ? UI_C_MOOD : UI_C_TRACK);
  }

  // Expression chips: each is a tiny line-art render of that expression
  for (uint8_t k = 0; k < UI_CHIPS_PER_PAGE; k++) {
    uint8_t e = ui.moodPage * UI_CHIPS_PER_PAGE + k;
    if (e >= BOT_NUM_EXPRESSIONS) break;
    UiRect r = {UI_CHIP_X[k % 4], UI_CHIP_Y[k / 4], UI_CHIP_W, UI_CHIP_H};
    bool sel = e == cur;
    bool pr = ui.pressedId == UI_ID_CHIP0 + k;
    uint16_t bg = pr ? UI_C_MOOD : (sel ? uiBlend(UI_C_TILE, UI_C_MOOD, 0.18f) : UI_C_TILE);
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 8, bg);
    if (sel && !pr) gfx->drawRoundRect(r.x, r.y, r.w, r.h, 8, UI_C_MOOD);
    uiDrawExprGlyph(e, r.x + r.w / 2, r.y + r.h / 2 - 5, 0.2f, bg);
  }
}

// ---- Settings: root page + category pages --------------------------------
static const uint16_t UI_CAT_HUE[CAT_COUNT] = {UI_C_LOOK, UI_C_MOOD, UI_C_INFO, UI_C_LIGHT, UI_C_CONNECT, UI_C_SYSTEM};
static const char* const UI_CAT_NAME[CAT_COUNT] = {"Look", "Mood", "Info", "Light", "Connect", "System"};
static const UiIcon UI_CAT_ICON[CAT_COUNT] = {IC_SCENE, IC_MOOD, IC_WEATHER, IC_LIGHT, IC_WIFI, IC_SYSTEM};

static const char* const UI_TZ_VAL[] = {
  "EST5EDT,M3.2.0,M11.1.0", "CST6CDT,M3.2.0,M11.1.0", "MST7MDT,M3.2.0,M11.1.0", "MST7",
  "PST8PDT,M3.2.0,M11.1.0", "AKST9AKDT,M3.2.0,M11.1.0", "HST10", "UTC0",
  "GMT0BST,M3.5.0/1,M10.5.0", "CET-1CEST,M3.5.0,M10.5.0/3"};
static const char* const UI_TZ_NAME[] = {
  "Eastern", "Central", "Mountain", "Arizona", "Pacific", "Alaska", "Hawaii", "UTC", "UK", "C. Europe"};
#define UI_NUM_TZ 10
static const char* const UI_KSCOPE_NAME[KSCOPE_MODE_COUNT] = {"Off", "Vert", "Horiz", "Quad", "6-Slice", "8-Slice"};
static const uint16_t UI_FACE_COLORS[5] = {0xFFFF, 0x07FF, 0x07E0, 0xF81F, 0xFFE0};
static const uint32_t UI_ROT_MS[] = {0, 60000UL, 300000UL, 900000UL, 3600000UL};
static const char* const UI_ROT_NAME[] = {"Off", "1 min", "5 min", "15 min", "1 hour"};
#define UI_NUM_ROT 5

static int8_t uiTzIndex() {
  for (uint8_t i = 0; i < UI_NUM_TZ; i++) if (strcmp(timezoneTZ, UI_TZ_VAL[i]) == 0) return i;
  return -1;
}
static int8_t uiRotIndex() {
  for (uint8_t i = 0; i < UI_NUM_ROT; i++) if (botMode.personalityRotIntervalMs == UI_ROT_MS[i]) return i;
  return -1;
}

enum UiRowKind : uint8_t { ROW_NAV, ROW_TOGGLE, ROW_STEP, ROW_INFO, ROW_SWATCH, ROW_DOTS, ROW_HOLD, ROW_SLIDER };
enum UiRowKey : uint8_t {
  RK_SCENE, RK_PALETTE, RK_HIRES, RK_KSCOPE, RK_FACE, RK_AUTO,
  RK_PERSONALITY, RK_ROTATE, RK_EXPRESSIONS,
  RK_WEATHER, RK_CLOCK, RK_TZ, RK_SCHEDULE,
  RK_SCREEN,
  RK_NETWORK, RK_ADDRESS, RK_IP, RK_SIGNAL, RK_NEARBY, RK_HOTSPOT,
  RK_FIRMWARE, RK_DEVICE, RK_UPTIME, RK_MEMORY, RK_RESTART,
  RK_BASE_LEDS, RK_LED_MODE, RK_VOLUME, RK_AUDIOFX, RK_REACT,
  RK_CHILL, RK_NOD, RK_SHAKE, RK_LOOKUP, RK_LOOKDOWN, RK_CENTER,
  RK_BATTERY, RK_GAMES, RK_POWEROFF,
};
struct UiRow {
  const char* label;
  UiRowKind kind;
  UiRowKey key;
  bool on;
  char value[28];
  float frac;   // slider position 0..1
};
#define UI_MAX_ROWS 8
#define UI_ROW_Y0 40
#if UI_LANDSCAPE
#define UI_ROW_H 38
#define UI_ROW_L 104        // label x (pane starts at 98, right of the category rail)
#define UI_ROW_R 308        // right edge for values and controls
#define UI_RAIL_W 92
#else
#define UI_ROW_H 44
#define UI_ROW_L 14
#define UI_ROW_R 226
#endif
#define UI_STEP_X (UI_ROW_R - 102)
#define UI_TOGGLE_X (UI_ROW_R - 40)
#define UI_DOTS_X (UI_ROW_R - 96)
static UiRow uiRows[UI_MAX_ROWS];
static uint8_t uiRowCount = 0;

static void uiAddRow(const char* label, UiRowKind kind, UiRowKey key, const char* value = "", bool on = false, float frac = 0) {
  if (uiRowCount >= UI_MAX_ROWS) return;
  UiRow& r = uiRows[uiRowCount++];
  r.label = label; r.kind = kind; r.key = key; r.on = on; r.frac = frac;
  strncpy(r.value, value, sizeof(r.value) - 1);
  r.value[sizeof(r.value) - 1] = 0;
}

extern uint8_t uiMeshPeerCount();  // esp_now_mesh.h

// Rebuilt every frame from live state, so pages always show current values
static void uiBuildRows(uint8_t cat) {
  uiRowCount = 0;
  char buf[28];
  switch (cat) {
    case CAT_LOOK:
      uiAddRow("Scene", ROW_NAV, RK_SCENE, uiSceneName(uiSceneIndex()));
      uiAddRow("Palette", ROW_SWATCH, RK_PALETTE, paletteNames[paletteIndex % NUM_PALETTES]);
      uiAddRow("Hi-res", ROW_TOGGLE, RK_HIRES, "", hiResMode);
      uiAddRow("Kaleido", ROW_STEP, RK_KSCOPE, UI_KSCOPE_NAME[kaleidoscopeMode % KSCOPE_MODE_COUNT]);
      uiAddRow("Face", ROW_DOTS, RK_FACE);
      uiAddRow("Auto-cycle", ROW_TOGGLE, RK_AUTO, "", autoCycle);
      break;
    case CAT_MOOD: {
      uiAddRow("Personality", ROW_STEP, RK_PERSONALITY, botMode.personality->name);
      int8_t ri = uiRotIndex();
      if (ri >= 0) uiAddRow("Rotate", ROW_STEP, RK_ROTATE, UI_ROT_NAME[ri]);
      else { snprintf(buf, sizeof(buf), "%lu min", (unsigned long)(botMode.personalityRotIntervalMs / 60000)); uiAddRow("Rotate", ROW_STEP, RK_ROTATE, buf); }
      uiAddRow("Expression", ROW_NAV, RK_EXPRESSIONS, uiExprNames[botMode.face.targetExpr % BOT_NUM_EXPRESSIONS]);
      break;
    }
    case CAT_INFO: {
      uiAddRow("Weather", ROW_NAV, RK_WEATHER, "Show");
      uiAddRow("Clock", ROW_TOGGLE, RK_CLOCK, "", isBotTimeOverlayEnabled());
      int8_t ti = uiTzIndex();
      uiAddRow("Time zone", ROW_STEP, RK_TZ, ti >= 0 ? UI_TZ_NAME[ti] : "Custom");
      if (wledData.enabled) uiAddRow("Scheduled", ROW_TOGGLE, RK_SCHEDULE, "", schedContent.enabled);
      break;
    }
    case CAT_LIGHT:
      snprintf(buf, sizeof(buf), "%u%%", (unsigned)((lcdBrightness * 100 + 127) / 255));
#if UI_LANDSCAPE
      uiAddRow("Screen", ROW_SLIDER, RK_SCREEN, buf, false, lcdBrightness / 255.0f);
#if defined(BOARD_HAS_STACKCHAN_BASE) || defined(BOARD_HAS_FACES_BASE)
      if (sysStatus.scBaseLedsReady) {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)(scLeds.brightness * 100 / 255));
        uiAddRow("Base LEDs", ROW_SLIDER, RK_BASE_LEDS, buf, false, scLeds.brightness / 255.0f);
        uiAddRow("LED mode", ROW_STEP, RK_LED_MODE, SC_LED_MODE_NAMES[scLeds.mode % SC_LED_MODE_COUNT]);
      }
#endif
      snprintf(buf, sizeof(buf), "%u%%", (unsigned)(botSounds.volume * 100 / 255));
      uiAddRow("Volume", ROW_SLIDER, RK_VOLUME, buf, false, botSounds.volume / 255.0f);
      uiAddRow("Audio FX", ROW_TOGGLE, RK_AUDIOFX, "", audioSpectrum.enabled);
      snprintf(buf, sizeof(buf), "%u%%", (unsigned)(audioDrama * 100 / 200));
      uiAddRow("Reactivity", ROW_SLIDER, RK_REACT, buf, false, audioDrama / 200.0f);
#else
      uiAddRow("Screen", ROW_NAV, RK_SCREEN, buf);
#endif
      break;
#ifdef BOARD_HAS_STACKCHAN_BASE
    case CAT_HEAD:
      uiAddRow("Chill mode", ROW_TOGGLE, RK_CHILL, "", scTouch_state.chillMode);
      uiAddRow("Nod", ROW_NAV, RK_NOD);
      uiAddRow("Shake", ROW_NAV, RK_SHAKE);
      uiAddRow("Look up", ROW_NAV, RK_LOOKUP);
      uiAddRow("Look down", ROW_NAV, RK_LOOKDOWN);
      uiAddRow("Center", ROW_NAV, RK_CENTER);
      break;
#endif
    case CAT_CONNECT:
      if (sysStatus.staConnected) {
        uiAddRow("Network", ROW_INFO, RK_NETWORK, WiFi.SSID().c_str());
        snprintf(buf, sizeof(buf), "%s.local", mdnsHostname);
        uiAddRow("Address", ROW_INFO, RK_ADDRESS, buf);
        uiAddRow("IP", ROW_INFO, RK_IP, sysStatus.staIP.toString().c_str());
        snprintf(buf, sizeof(buf), "%d dBm", (int)WiFi.RSSI());
        uiAddRow("Signal", ROW_INFO, RK_SIGNAL, buf);
        snprintf(buf, sizeof(buf), "%u", uiMeshPeerCount());
        uiAddRow("Nearby bots", ROW_INFO, RK_NEARBY, buf);
      }
      break;
    case CAT_SYSTEM: {
      uiAddRow("Firmware", ROW_INFO, RK_FIRMWARE, FIRMWARE_VERSION);
      uiAddRow("Name", ROW_INFO, RK_DEVICE, apSSID);
      unsigned long m = millis() / 60000;
      if (m >= 1440) snprintf(buf, sizeof(buf), "%lud %luh", m / 1440, (m % 1440) / 60);
      else snprintf(buf, sizeof(buf), "%luh %lum", m / 60, m % 60);
      uiAddRow("Uptime", ROW_INFO, RK_UPTIME, buf);
      snprintf(buf, sizeof(buf), "%u KB free", (unsigned)(ESP.getFreeHeap() / 1024));
      uiAddRow("Memory", ROW_INFO, RK_MEMORY, buf);
#ifdef BOARD_HAS_STACKCHAN_BASE
      if (sysStatus.scBatteryMonReady) {
        snprintf(buf, sizeof(buf), "%.2f V", scGetBatteryVoltage());
        uiAddRow("Battery", ROW_INFO, RK_BATTERY, buf);
      }
#endif
#ifdef BOARD_HAS_FACES_BASE
      uiAddRow("Games", ROW_HOLD, RK_GAMES, "Hold");
#endif
      uiAddRow("Restart", ROW_HOLD, RK_RESTART, "Hold");
#ifdef BOARD_HAS_STACKCHAN_BASE
      uiAddRow("Power off", ROW_HOLD, RK_POWEROFF, "Hold");
#endif
      break;
    }
  }
}

static int16_t uiMaxScroll() {
  if (ui.cat == CAT_CONNECT && !sysStatus.staConnected) return 0;
  int16_t content = uiRowCount * UI_ROW_H;
  int16_t visible = LCD_HEIGHT - UI_ROW_Y0 - 4;
  return content > visible ? content - visible : 0;
}

static void uiDrawToggle(int16_t x, int16_t y, bool on) {
  gfx->fillRoundRect(x, y, 40, 24, 12, on ? UI_C_CONNECT : UI_C_TRACK);
  gfx->fillCircle(on ? x + 28 : x + 12, y + 12, 10, UI_C_TEXT);
}

static void uiDrawRow(uint8_t i, int16_t y, uint16_t hue) {
  const UiRow& r = uiRows[i];
  const int16_t L = UI_ROW_L, R = UI_ROW_R;
  bool pressed = ui.pressedId == UI_ID_ROW0 + i && r.kind != ROW_INFO && r.kind != ROW_SLIDER;
  if (pressed) gfx->fillRoundRect(L - 8, y + 2, R - L + 16, UI_ROW_H - 4, 8, UI_C_TILE);
  if (i + 1 < uiRowCount) gfx->drawFastHLine(L, y + UI_ROW_H - 1, R - L, UI_C_TILE);
  int16_t cy = y + UI_ROW_H / 2;

  if (r.kind == ROW_HOLD) {
    // Fill grows while held; fires at UI_HOLD_MS
    float t = (ui.holdRow == i) ? min(1.0f, (millis() - ui.holdStart) / (float)UI_HOLD_MS) : 0;
    if (t > 0) gfx->fillRoundRect(L - 8, y + 2, (int16_t)((R - L + 16) * t), UI_ROW_H - 4, 8, uiBlend(UI_C_TILE, UI_C_OFF, 0.5f));
    uiText(r.label, L, cy, &fonts::DejaVu18, UI_C_OFF, UI_ML);
    uiText(ui.holdRow == i ? "Keep holding" : "Hold to confirm", R, cy, &fonts::DejaVu12, UI_C_DIM, UI_MR);
    return;
  }

  if (r.kind == ROW_SLIDER) {
    // Label and value on top, full-width draggable track underneath
    int16_t ty = y + UI_ROW_H - 10;
    uiText(r.label, L, y + 12, &fonts::DejaVu12, UI_C_TEXT, UI_ML);
    uiText(r.value, R, y + 12, &fonts::DejaVu12, hue, UI_MR, true);
    // Track inset by the knob radius so the knob never leaves the pane
    const int16_t tl = L + 8, tw = R - L - 16;
    int16_t fill = (int16_t)(tw * constrain(r.frac, 0.0f, 1.0f));
    gfx->fillRoundRect(tl, ty - 3, tw, 6, 3, UI_C_TRACK);
    if (fill > 0) gfx->fillRoundRect(tl, ty - 3, max((int16_t)6, fill), 6, 3, hue);
    gfx->fillCircle(tl + fill, ty, 8, hue);
    gfx->fillCircle(tl + fill, ty, 6, ui.dragRow == i ? hue : UI_C_TEXT);
    return;
  }

  // Label drops to the small font when it would run into the control
  int16_t ctrlX;
  switch (r.kind) {
    case ROW_STEP:   ctrlX = UI_STEP_X; break;
    case ROW_TOGGLE: ctrlX = UI_TOGGLE_X; break;
    case ROW_DOTS:   ctrlX = UI_DOTS_X - 12; break;
    case ROW_SWATCH: ctrlX = R - 72 - uiTextW(r.value, &fonts::DejaVu12); break;
    case ROW_NAV:    ctrlX = R - 18 - uiTextW(r.value, &fonts::DejaVu12); break;
    default:         ctrlX = R - uiTextW(r.value, &fonts::DejaVu12); break;
  }
  bool small = L + uiTextW(r.label, &fonts::DejaVu18) + 8 > ctrlX;
#if UI_LANDSCAPE
  if (ui.cat == CAT_LIGHT) small = true;  // match the slider rows' small labels
#endif
  uiText(r.label, L, cy, small ? &fonts::DejaVu12 : &fonts::DejaVu18, UI_C_TEXT, UI_ML);
  switch (r.kind) {
    case ROW_NAV:
      uiIcon(IC_FWD, R - 14, cy - 7, 14, UI_C_DIM, UI_C_BG, 2.4f);
      uiText(r.value, R - 18, cy, &fonts::DejaVu12, UI_C_DIM, UI_MR);
      break;
    case ROW_INFO:
      uiText(r.value, R, cy, &fonts::DejaVu12, UI_C_SYSTEM, UI_MR);
      break;
    case ROW_TOGGLE:
      uiDrawToggle(UI_TOGGLE_X, cy - 12, r.on);
      break;
    case ROW_STEP:
      gfx->fillRoundRect(UI_STEP_X, cy - 14, 104, 28, 8, UI_C_TILE);
      uiIcon(IC_BACK, UI_STEP_X + 4, cy - 8, 16, hue, UI_C_TILE, 2.6f);
      uiIcon(IC_FWD, UI_STEP_X + 84, cy - 8, 16, hue, UI_C_TILE, 2.6f);
      uiText(r.value, UI_STEP_X + 52, cy, &fonts::DejaVu12, UI_C_TEXT, UI_MC);
      break;
    case ROW_SWATCH:
      for (uint8_t k = 0; k < 5; k++) {
        CRGB c = ColorFromPalette(currentPalette, k * 51);
        gfx->fillRect(R - 66 + k * 10, cy - 5, 10, 10, crgbToRgb565(c));
      }
      uiText(r.value, R - 72, cy, &fonts::DejaVu12, UI_C_DIM, UI_MR);
      uiIcon(IC_FWD, R - 14, cy - 7, 14, UI_C_DIM, UI_C_BG, 2.4f);
      break;
    case ROW_DOTS:
      for (uint8_t k = 0; k < 5; k++) {
        int16_t dx = UI_DOTS_X + 4 + k * 20;
        if (botFaceColor == UI_FACE_COLORS[k]) gfx->fillCircle(dx, cy, 10, UI_C_MOOD);
        if (botFaceColor == UI_FACE_COLORS[k]) gfx->fillCircle(dx, cy, 8, UI_C_BG);
        gfx->fillCircle(dx, cy, 7, UI_FACE_COLORS[k]);
      }
      break;
    default: break;
  }
}

// No home WiFi: three numbered steps to reach the hotspot, plus its toggle.
// Landscape draws the same content into the pane right of the rail.
#if UI_LANDSCAPE
#define UI_SETUP_X 98
#define UI_SETUP_Y 38
#define UI_SETUP_STEP 36
#else
#define UI_SETUP_X 0
#define UI_SETUP_Y 36
#define UI_SETUP_STEP 42
#endif

static void uiRenderConnectSetup() {
  const int16_t ox = UI_SETUP_X, w = LCD_WIDTH - UI_SETUP_X;
  int16_t y0 = UI_SETUP_Y;
#if !UI_LANDSCAPE
  uiText("Let's get online!", ox + w / 2, y0 + 16, &fonts::DejaVu18, UI_C_TEXT, UI_MC, true);
  y0 += 36;
#endif
  const char* labels[3] = {"Join this WiFi", "Password", "Then open"};
  char ip[20];
  snprintf(ip, sizeof(ip), "%s", WiFi.softAPIP().toString().c_str());
  const char* values[3] = {apSSID, WIFI_PASSWORD, ip};
  for (uint8_t i = 0; i < 3; i++) {
    int16_t y = y0 + i * UI_SETUP_STEP;
    gfx->fillCircle(ox + 30, y + 19, 12, UI_C_CONNECT);
    char n[2] = {(char)('1' + i), 0};
    uiText(n, ox + 30, y + 19, &fonts::DejaVu12, UI_C_BG, UI_MC, true);
    uiText(labels[i], ox + 52, y + 8, &fonts::DejaVu12, UI_C_DIM, UI_ML);
    uiText(values[i], ox + 52, y + 27, &fonts::DejaVu18, UI_C_TEXT, UI_ML, true);
  }
  int16_t hy = y0 + 3 * UI_SETUP_STEP + 8;
  gfx->drawFastHLine(ox + 16, hy - 4, w - 32, UI_C_TILE);
  uiIcon(IC_HOTSPOT, ox + 16, hy + 6, 20, UI_C_CONNECT, UI_C_BG);
  uiText("Hotspot", ox + 44, hy + 16, &fonts::DejaVu18, UI_C_TEXT, UI_ML);
  uiDrawToggle(LCD_WIDTH - 54, hy + 4, wifiEnabled);
}

// Hotspot toggle row on the setup page
static UiRect uiSetupHotspotRect() {
  int16_t y0 = UI_SETUP_Y + (UI_LANDSCAPE ? 0 : 36);
  int16_t hy = y0 + 3 * UI_SETUP_STEP + 8;
  return {(int16_t)UI_SETUP_X, (int16_t)(hy - 4), (int16_t)(LCD_WIDTH - UI_SETUP_X), 40};
}

static void uiRenderSettings() {
  for (uint8_t i = 0; i < CAT_COUNT; i++) {
    UiRect r = {(int16_t)(i % 2 ? 124 : 12), (int16_t)(40 + (i / 2) * 66), 104, 60};
    bool pr = ui.pressedId == UI_ID_CAT0 + i;
    uint16_t hue = UI_CAT_HUE[i];
    uint16_t bg = pr ? hue : UI_C_TILE;
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 12, bg);
    uiIcon(UI_CAT_ICON[i], r.x + 10, r.y + 8, 24, pr ? UI_C_BG : hue, bg);
    char st[20];
    uint16_t sc = UI_C_DIM;
    switch (i) {
      case CAT_LOOK: snprintf(st, sizeof(st), "%s", uiSceneName(uiSceneIndex())); break;
      case CAT_MOOD: snprintf(st, sizeof(st), "%s", botMode.personality->name); break;
      case CAT_INFO: {
        struct tm t;
        if (getLocalTime(&t, 0)) strftime(st, sizeof(st), "%H:%M", &t);
        else snprintf(st, sizeof(st), "%s", uiTzIndex() >= 0 ? UI_TZ_NAME[uiTzIndex()] : "");
        break;
      }
      case CAT_LIGHT: snprintf(st, sizeof(st), "%u%%", uiBrightPct()); break;
      case CAT_CONNECT:
        snprintf(st, sizeof(st), "%s", sysStatus.staConnected ? "Online" : "Setup");
        sc = sysStatus.staConnected ? UI_C_CONNECT : UI_C_LIGHT;
        break;
      default: snprintf(st, sizeof(st), "%s", FIRMWARE_VERSION); break;
    }
    // Long values would collide with the icon: keep the status short
    if (uiTextW(st, &fonts::DejaVu12) > 60) st[8] = 0;
    uiText(st, r.x + r.w - 8, r.y + 20, &fonts::DejaVu12, pr ? UI_C_BG : sc, UI_MR);
    uiText(UI_CAT_NAME[i], r.x + 10, r.y + 46, &fonts::DejaVu18, pr ? UI_C_BG : UI_C_TEXT, UI_ML);
  }
  // Mini eyes keep the character on screen
  gfx->fillEllipse(113, 251, 10, 9, botFaceColor);
  gfx->fillEllipse(127, 251, 10, 9, botFaceColor);
  gfx->fillCircle(114, 252, 3, BOT_COLOR_PUPIL);
  gfx->fillCircle(128, 252, 3, BOT_COLOR_PUPIL);
  if (uiToastActive()) uiDrawToast();
  else uiDrawBackPill("Settings", UI_C_SYSTEM, UI_C_BG);
}

static uint16_t uiCatHue(uint8_t c) { return c < CAT_COUNT ? UI_CAT_HUE[c] : UI_C_MOOD; }
static const char* uiCatName(uint8_t c) { return c < CAT_COUNT ? UI_CAT_NAME[c] : "Head"; }

#if UI_LANDSCAPE
// Category rail for the master-detail settings page
static void uiRenderRail() {
  gfx->fillRect(0, 38, UI_RAIL_W, LCD_HEIGHT - 38, UI_C_SURFACE);
  for (uint8_t i = 0; i < CAT_COUNT; i++) {
    int16_t y = 38 + 33 * i;
    bool on = ui.cat == i;
    bool pr = ui.pressedId == UI_ID_CAT0 + i;
    if (on || pr) gfx->fillRect(0, y, UI_RAIL_W, 33, pr ? uiCatHue(i) : UI_C_TILE);
    if (on && !pr) gfx->fillRoundRect(0, y + 6, 3, 21, 1, uiCatHue(i));
    uiIcon(UI_CAT_ICON[i], 7, y + 8, 16, pr ? UI_C_BG : uiCatHue(i), on ? UI_C_TILE : UI_C_SURFACE);
    const char* nm = i == CAT_LIGHT ? "Sound" : UI_CAT_NAME[i];
    uiText(nm, 28, y + 17, &fonts::DejaVu12, pr ? UI_C_BG : (on ? UI_C_TEXT : UI_C_DIM), UI_ML, on);
  }
}
#endif

static void uiRenderCat() {
  uint16_t hue = uiCatHue(ui.cat);
  if (ui.cat == CAT_CONNECT && !sysStatus.staConnected) {
    uiRenderConnectSetup();
  } else {
    uiBuildRows(ui.cat);
    ui.scroll = constrain(ui.scroll, (int16_t)0, uiMaxScroll());
    for (uint8_t i = 0; i < uiRowCount; i++) {
      int16_t y = UI_ROW_Y0 + i * UI_ROW_H - ui.scroll;
      if (y > LCD_HEIGHT || y + UI_ROW_H < UI_ROW_Y0 - UI_ROW_H) continue;
      uiDrawRow(i, y, hue);
    }
    int16_t ms = uiMaxScroll();
    if (ms > 0) {
      int16_t track = LCD_HEIGHT - UI_ROW_Y0 - 16;
      int16_t bar = max((int16_t)20, (int16_t)(track * track / (track + ms)));
      int16_t by = UI_ROW_Y0 + 4 + (int32_t)(track - bar) * ui.scroll / ms;
      gfx->fillRoundRect(UI_ROW_R + 6, by, 2, bar, 1, UI_C_TRACK);
    }
    gfx->fillRect(0, 0, LCD_WIDTH, 36, UI_C_BG);  // rows scroll under the pill
  }
#if UI_LANDSCAPE
  uiRenderRail();
  if (uiToastActive()) { uiDrawToast(); return; }
  uiDrawBackPill("Settings", UI_C_SYSTEM, UI_C_BG, 6);
  const char* title = ui.cat == CAT_LIGHT ? "Light & sound" : uiCatName(ui.cat);
  uiText(title, 120, 19, &fonts::DejaVu18, UI_C_TEXT, UI_ML, true);
  gfx->fillEllipse(287, 19, 10, 9, botFaceColor);
  gfx->fillEllipse(301, 19, 10, 9, botFaceColor);
  gfx->fillCircle(288, 20, 3, BOT_COLOR_PUPIL);
  gfx->fillCircle(302, 20, 3, BOT_COLOR_PUPIL);
#else
  if (uiToastActive()) uiDrawToast();
  else uiDrawBackPill(uiCatName(ui.cat), hue, UI_C_BG);
#endif
}

static void uiRenderHome() {
  // Swipe handle for a few seconds after a touch
  if (millis() - ui.lastTouch < UI_HANDLE_SHOW_MS && ui.lastTouch != 0) {
    gfx->fillRoundRect(104, 262, 32, 4, 2, UI_C_HANDLE);
  }
  if (uiToastActive()) uiDrawToast();
}

void uiRenderOverlay() {
  switch (ui.screen) {
    case UI_HOME:  uiRenderHome(); break;
    case UI_DOCK:  uiRenderDock(); break;
    case UI_SCENE: uiRenderScene(); break;
    case UI_LIGHT: uiRenderLight(); break;
    case UI_MOOD:  uiRenderMood(); break;
    case UI_SETTINGS: uiRenderSettings(); break;
    case UI_CAT:   uiRenderCat(); break;
  }
}

// ---- Hit testing ----------------------------------------------------------
static uint8_t uiHitTest(int16_t x, int16_t y) {
  switch (ui.screen) {
    case UI_DOCK:
      if (UI_PILL_RECT.hit(x, y)) return UI_ID_PILL;
      for (uint8_t i = 0; i < UI_DOCK_TILES; i++) {
        if (uiDockRect(i).hit(x, y)) return UI_ID_TILE0 + i;
      }
      break;
    case UI_SCENE:
      if (ui.backRect.hit(x, y, 8)) return UI_ID_BACK;
#if UI_LANDSCAPE
      if (UI_R3_PREV.hit(x, y)) return UI_ID_SCENE_PREV;
      if (UI_R3_NEXT.hit(x, y)) return UI_ID_SCENE_NEXT;
      if (UI_R3_PALETTE.hit(x, y)) return UI_ID_PALETTE;
      if (UI_R3_HIRES.hit(x, y)) return UI_ID_HIRES;
      if (UI_R3_AUTO.hit(x, y)) return UI_ID_AUTO;
      if (UI_R3_KSCOPE.hit(x, y)) return UI_ID_KSCOPE;
      break;
#endif
      if (UI_R_PREV.hit(x, y)) return UI_ID_SCENE_PREV;
      if (UI_R_NEXT.hit(x, y)) return UI_ID_SCENE_NEXT;
      if (UI_R_PALETTE.hit(x, y)) return UI_ID_PALETTE;
      if (UI_R_SEG.hit(x, y)) return x < UI_R_SEG.x + UI_R_SEG.w / 2 ? UI_ID_PIXEL : UI_ID_HIRES;
      if (UI_R_AUTO.hit(x, y)) return UI_ID_AUTO;
      break;
    case UI_LIGHT:
      if (ui.backRect.hit(x, y, 8)) return UI_ID_BACK;
      if (UI_R_MINUS.hit(x, y)) return UI_ID_MINUS;
      if (UI_R_PLUS.hit(x, y)) return UI_ID_PLUS;
      if (UI_R_TRACKROW.hit(x, y, 0)) return UI_ID_TRACK;
      for (uint8_t i = 0; i < 3; i++) {
        UiRect r = {UI_TILE_X[i], 222, 68, 34};
        if (r.hit(x, y)) return UI_ID_PRESET0 + i;
      }
      break;
    case UI_MOOD:
      if (ui.backRect.hit(x, y, 8)) return UI_ID_BACK;
      if (UI_R_PERS.hit(x, y)) return UI_ID_PERS0 + min(2, (x - UI_R_PERS.x) / (UI_R_PERS.w / 3));
      for (uint8_t k = 0; k < UI_CHIPS_PER_PAGE; k++) {
        UiRect r = {UI_CHIP_X[k % 4], UI_CHIP_Y[k / 4], UI_CHIP_W, UI_CHIP_H};
        if (ui.moodPage * UI_CHIPS_PER_PAGE + k < BOT_NUM_EXPRESSIONS && r.hit(x, y, 2)) return UI_ID_CHIP0 + k;
      }
      break;
    case UI_SETTINGS:
      if (ui.backRect.hit(x, y, 8)) return UI_ID_BACK;
      for (uint8_t i = 0; i < CAT_COUNT; i++) {
        UiRect r = {(int16_t)(i % 2 ? 124 : 12), (int16_t)(40 + (i / 2) * 66), 104, 60};
        if (r.hit(x, y)) return UI_ID_CAT0 + i;
      }
      break;
    case UI_CAT:
      if (ui.backRect.hit(x, y, 8)) return UI_ID_BACK;
#if UI_LANDSCAPE
      if (x < UI_RAIL_W && y >= 38) {
        int16_t i = (y - 38) / 33;
        if (i >= 0 && i < CAT_COUNT) return UI_ID_CAT0 + i;
        break;
      }
#endif
      if (ui.cat == CAT_CONNECT && !sysStatus.staConnected) {
        if (uiSetupHotspotRect().hit(x, y, 0)) return UI_ID_ROW0;  // hotspot toggle
        break;
      }
      if (y >= UI_ROW_Y0 - 4) {
        uiBuildRows(ui.cat);
        int16_t i = (y - UI_ROW_Y0 + ui.scroll) / UI_ROW_H;
        if (i >= 0 && i < uiRowCount) return UI_ID_ROW0 + i;
      }
      break;
    default: break;
  }
  return UI_ID_NONE;
}

// ---- Actions --------------------------------------------------------------
static void uiStepScene(int8_t dir) {
  uint8_t n = UI_NUM_SCENES;
  uint8_t i = (uiSceneIndex() + n + dir) % n;
  uiSetScene(i);
  uiToast(uiSceneName(i), UI_C_LOOK);
}

static void uiBack() {
  switch (ui.screen) {
#if UI_LANDSCAPE
    case UI_CAT: uiGo(UI_DOCK); break;   // master-detail: the page is the settings root
#else
    case UI_CAT: uiGo(UI_SETTINGS); break;
#endif
    case UI_SETTINGS: uiGo(UI_DOCK); break;
    default: uiGo(ui.backTo); break;
  }
}

static void uiOpenSheet(UiScreen s, UiScreen backTo) {
  ui.backTo = backTo;
  uiGo(s);
}

static void uiRowAction(uint8_t i, int16_t x) {
  if (ui.cat == CAT_CONNECT && !sysStatus.staConnected) {
    toggleWifiAP();  // offline: the hotspot is the only way in, so it is safe to toggle
    return;
  }
  uiBuildRows(ui.cat);
  if (i >= uiRowCount) return;
  const UiRow& r = uiRows[i];
  int dir = (r.kind == ROW_STEP && x < UI_STEP_X + 52) ? -1 : 1;
  switch (r.key) {
    case RK_SCENE: uiOpenSheet(UI_SCENE, UI_CAT); break;
    case RK_PALETTE:
      paletteIndex = (paletteIndex + 1) % NUM_PALETTES;
      currentPalette = palettes[paletteIndex];
      autoCycle = false;
      markSettingsDirty();
      break;
    case RK_HIRES: toggleHiResMode(); break;
    case RK_KSCOPE:
      kaleidoscopeMode = (kaleidoscopeMode + KSCOPE_MODE_COUNT + dir) % KSCOPE_MODE_COUNT;
      markSettingsDirty();
      break;
    case RK_FACE: {
      int k = constrain((x - UI_DOTS_X + 6) / 20, 0, 4);
      setBotFaceColor(UI_FACE_COLORS[k]);
      markSettingsDirty();
      break;
    }
    case RK_AUTO:
      autoCycle = !autoCycle;
      lastChange = millis();
      markSettingsDirty();
      break;
    case RK_PERSONALITY:
      setBotPersonality((botMode.personalityIndex + runtimePersonalityCount + dir) % runtimePersonalityCount);
      break;
    case RK_ROTATE: {
      int8_t ri = uiRotIndex();
      ri = ri < 0 ? 0 : (ri + UI_NUM_ROT + dir) % UI_NUM_ROT;
      botMode.personalityRotIntervalMs = UI_ROT_MS[ri];
      botMode.lastPersonalityRotMs = millis();
      break;
    }
    case RK_EXPRESSIONS: uiOpenSheet(UI_MOOD, UI_CAT); break;
    case RK_WEATHER:
      uiGo(UI_HOME);
      infoMode.beginEnterTransition();
      break;
    case RK_CLOCK: toggleBotTimeOverlay(); break;
    case RK_TZ: {
      int8_t ti = uiTzIndex();
      ti = ti < 0 ? 0 : (ti + UI_NUM_TZ + dir) % UI_NUM_TZ;
      strncpy(timezoneTZ, UI_TZ_VAL[ti], TZ_BUF_LEN - 1);
      timezoneTZ[TZ_BUF_LEN - 1] = 0;
      applyTimezone();
      configTzTime(timezoneTZ, "pool.ntp.org", "time.nist.gov");
      markSettingsDirty();
      break;
    }
    case RK_SCHEDULE:
      schedContent.enabled = !schedContent.enabled;
      if (schedContent.enabled) schedContent.lastCycleStartMs = millis() - schedContent.cycleIntervalMs + 60000;
      saveScheduleSettings();
      break;
    case RK_SCREEN:
#if !UI_LANDSCAPE
      uiOpenSheet(UI_LIGHT, UI_CAT);
#endif
      break;
#if defined(BOARD_HAS_STACKCHAN_BASE) || defined(BOARD_HAS_FACES_BASE)
    case RK_LED_MODE:
      scLeds.mode = (scLeds.mode + SC_LED_MODE_COUNT + dir) % SC_LED_MODE_COUNT;
      break;
#endif
#ifdef TARGET_CORES3
    case RK_AUDIOFX:
      audioSpectrum.setEnabled(!audioSpectrum.enabled);
      markSettingsDirty();
      break;
#endif
#ifdef BOARD_HAS_STACKCHAN_BASE
    case RK_CHILL: scFireChillMode(); break;
    case RK_NOD: scFireNod(); break;
    case RK_SHAKE: scFireShake(); break;
    case RK_LOOKUP: scMovePitch(900, 500); break;
    case RK_LOOKDOWN: scMovePitch(SC_SERVO_Y_MIN_DEG * 10, 500); break;
    case RK_CENTER: scGoHome(500); break;
#endif
    default: break;  // info rows; restart fires from the hold timer
  }
}

static void uiActivate(uint8_t id) {
  switch (id) {
    case UI_ID_TILE0 + 0: case UI_ID_TILE0 + 1: case UI_ID_TILE0 + 2: case UI_ID_TILE0 + 3:
    case UI_ID_TILE0 + 4: case UI_ID_TILE0 + 5: case UI_ID_TILE0 + 6: case UI_ID_TILE0 + 7: {
      uint8_t t = id - UI_ID_TILE0;
      if (t >= UI_DOCK_TILES) break;
      switch (UI_DOCK_DEF[t].act) {
        case TA_MOOD: uiOpenSheet(UI_MOOD, UI_DOCK); break;
        case TA_SCENE: uiOpenSheet(UI_SCENE, UI_DOCK); break;
        case TA_WEATHER:
          uiGo(UI_HOME);
          infoMode.beginEnterTransition();
          break;
        case TA_CLOCK:
          toggleBotTimeOverlay();
          uiSound(isBotTimeOverlayEnabled() ? SEQ_TOGGLE_ON : SEQ_TOGGLE_OFF);
          uiToast(isBotTimeOverlayEnabled() ? "Clock on" : "Clock off", UI_C_INFO);
          break;
#if UI_LANDSCAPE
        case TA_LIGHT: case TA_SOUND: ui.cat = CAT_LIGHT; uiGo(UI_CAT); break;
        case TA_HEAD: ui.cat = CAT_HEAD; uiGo(UI_CAT); break;
        case TA_GAMES: ui.cat = CAT_SYSTEM; uiGo(UI_CAT); break;
        case TA_CONNECT: ui.cat = CAT_CONNECT; uiGo(UI_CAT); break;
        case TA_MORE: uiGo(UI_CAT); break;   // last category viewed
#else
        case TA_LIGHT: uiOpenSheet(UI_LIGHT, UI_DOCK); break;
        case TA_MORE: uiGo(UI_SETTINGS); break;
        default: break;
#endif
      }
      break;
    }
    case UI_ID_PILL: {
      char buf[40];
      if (sysStatus.staConnected) snprintf(buf, sizeof(buf), "%s", sysStatus.staIP.toString().c_str());
      else snprintf(buf, sizeof(buf), "Hotspot %s", WiFi.softAPIP().toString().c_str());
      uiToast(buf, UI_C_CONNECT, 3000);
      break;
    }
    case UI_ID_BACK: uiBack(); break;

    case UI_ID_SCENE_PREV: uiStepScene(-1); break;
    case UI_ID_SCENE_NEXT: uiStepScene(1); break;
    case UI_ID_PALETTE:
      paletteIndex = (paletteIndex + 1) % NUM_PALETTES;
      currentPalette = palettes[paletteIndex];
      autoCycle = false;  // keep the pick (auto-cycle rotates palettes every 5 s)
      markSettingsDirty();
      break;
    case UI_ID_PIXEL: if (hiResMode) toggleHiResMode(); break;
#if UI_LANDSCAPE
    case UI_ID_HIRES: toggleHiResMode(); break;   // one chip toggles
    case UI_ID_KSCOPE:
      kaleidoscopeMode = (kaleidoscopeMode + 1) % KSCOPE_MODE_COUNT;
      uiToast(UI_KSCOPE_NAME[kaleidoscopeMode], UI_C_LOOK);
      markSettingsDirty();
      break;
#else
    case UI_ID_HIRES: if (!hiResMode) toggleHiResMode(); break;
#endif
    case UI_ID_AUTO:
      autoCycle = !autoCycle;
      lastChange = millis();
      markSettingsDirty();
      break;

    case UI_ID_MINUS: uiSetBrightPct(((uiBrightPct() + 5) / 10) * 10 - 10); break;
    case UI_ID_PLUS:  uiSetBrightPct(((uiBrightPct() + 5) / 10) * 10 + 10); break;
    case UI_ID_PRESET0 + 0: uiSetBrightPct(10); break;
    case UI_ID_PRESET0 + 1: uiSetBrightPct(60); break;
    case UI_ID_PRESET0 + 2: uiSetBrightPct(100); break;

    case UI_ID_PERS0 + 0: case UI_ID_PERS0 + 1: case UI_ID_PERS0 + 2: {
      uint8_t p = id - UI_ID_PERS0;
      if (p < runtimePersonalityCount) {
        setBotPersonality(p);
        uiToast(runtimePersonalities[p].name, UI_C_MOOD);
      }
      break;
    }
    default:
      if (id >= UI_ID_CAT0 && id < UI_ID_CAT0 + CAT_COUNT) {
        uint8_t c = id - UI_ID_CAT0;
#if UI_LANDSCAPE
        ui.cat = c;
        ui.scroll = 0;
#else
        if (c == CAT_LIGHT) { uiOpenSheet(UI_LIGHT, UI_SETTINGS); break; }
        ui.cat = c;
        uiGo(UI_CAT);
#endif
        break;
      }
      if (id >= UI_ID_CHIP0 && id < UI_ID_CHIP0 + UI_CHIPS_PER_PAGE) {
        uint8_t e = ui.moodPage * UI_CHIPS_PER_PAGE + (id - UI_ID_CHIP0);
        if (e < BOT_NUM_EXPRESSIONS) botMode.setExpression(e, 250);
      }
      break;
  }
}

// Slider rows (landscape Light & sound): x → value
static void uiSliderTo(uint8_t row, int16_t x) {
  if (row >= uiRowCount) return;
  float f = constrain((x - UI_ROW_L - 8) / (float)(UI_ROW_R - UI_ROW_L - 16), 0.0f, 1.0f);
  switch (uiRows[row].key) {
    case RK_SCREEN: uiSetBrightPct(max(5, (int)lroundf(f * 100))); break;
#if defined(BOARD_HAS_STACKCHAN_BASE) || defined(BOARD_HAS_FACES_BASE)
    case RK_BASE_LEDS: scLeds.brightness = (uint8_t)lroundf(f * 255); break;
#endif
#ifdef TARGET_CORES3
    case RK_VOLUME: botSounds.setVolume((uint8_t)lroundf(f * 255)); markSettingsDirty(); break;
    case RK_REACT: audioDrama = (uint8_t)lroundf(f * 200); markSettingsDirty(); break;
#endif
    default: break;
  }
}

static void uiTrackTo(int16_t x) {
  int pct = 5 + (int)(x - UI_TRACK_X) * 95 / UI_TRACK_W;
  uiSetBrightPct(pct);
}

// ---- Gesture handlers -----------------------------------------------------
static void uiOnDown(int16_t x, int16_t y) {
  botMode.registerInteraction();
  if (infoMode.active) return;
  ui.pressedId = uiHitTest(x, y);
  ui.scrollStart = ui.scroll;
  if (ui.screen == UI_CAT && ui.pressedId >= UI_ID_ROW0 && ui.cat != CAT_CONNECT) {
    uint8_t i = ui.pressedId - UI_ID_ROW0;
    if (i < uiRowCount && uiRows[i].kind == ROW_HOLD) {
      ui.holdRow = i;
      ui.holdStart = millis();
    } else if (i < uiRowCount && uiRows[i].kind == ROW_SLIDER) {
      ui.dragRow = i;
      ui.dragging = true;
      uiSliderTo(i, x);
    }
  }
  if (ui.pressedId == UI_ID_TRACK) {
    ui.dragging = true;
    uiTrackTo(x);
  }
}

static void uiOnMove(int16_t x, int16_t y) {
  if (ui.dragging) {
    if (ui.dragRow >= 0) { uiBuildRows(ui.cat); uiSliderTo(ui.dragRow, x); }
    else uiTrackTo(x);
    return;
  }
  // Vertical drag scrolls long category pages
  int16_t dy = y - uiTrack.y0, dx = x - uiTrack.x0;
  if (ui.screen == UI_CAT && uiMaxScroll() > 0 &&
      (ui.scrolling || (abs(dy) > UI_TAP_SLOP_PX && abs(dy) > abs(dx)))) {
    ui.scrolling = true;
    ui.pressedId = UI_ID_NONE;
    ui.holdRow = -1;
    ui.scroll = constrain((int16_t)(ui.scrollStart - dy), (int16_t)0, uiMaxScroll());
    return;
  }
  if (ui.holdRow >= 0 && (abs(dx) > UI_TAP_SLOP_PX || abs(dy) > UI_TAP_SLOP_PX)) ui.holdRow = -1;
  // Finger slid off the target: cancel the press
  if (ui.pressedId != UI_ID_NONE && uiHitTest(x, y) != ui.pressedId) ui.pressedId = UI_ID_NONE;
}

static void uiOnTap(int16_t x, int16_t y) {
  if (infoMode.active) { infoMode.nextPage(); return; }

  if (ui.screen == UI_HOME) {
    botMode.onTap();
    return;
  }

  uint8_t id = uiHitTest(x, y);
  if (id != UI_ID_NONE && id == ui.pressedId) {
    uiSound(SEQ_CONFIRM);
    if (ui.screen == UI_CAT && id >= UI_ID_ROW0) uiRowAction(id - UI_ID_ROW0, x);
    else uiActivate(id);
  } else if (id == UI_ID_NONE && y < UI_SHEET_TOP && !uiIsFullPage(ui.screen) && ui.screen != UI_SCENE) {
    // Tap on the mini face closes the sheet
    uiGo(UI_HOME);
  }
}

static void uiOnLongPress(int16_t x, int16_t y) {
  if (infoMode.active) {
    infoMode.beginExitTransition();
    uiGo(UI_DOCK);
    return;
  }
  if (ui.screen == UI_HOME) uiGo(UI_DOCK);
}

static void uiOnSwipe(uint8_t dir) {
  if (infoMode.active) {
    if (dir == UI_SWIPE_UP) { infoMode.beginExitTransition(); uiGo(UI_DOCK); }
    else if (dir == UI_SWIPE_DOWN) infoMode.beginExitTransition();
    else infoMode.nextPage();
    return;
  }
  switch (ui.screen) {
    case UI_HOME:
      if (dir == UI_SWIPE_UP) uiGo(UI_DOCK);
      else if (dir == UI_SWIPE_LEFT) uiStepScene(1);
      else if (dir == UI_SWIPE_RIGHT) uiStepScene(-1);
      break;
    case UI_DOCK:
      if (dir == UI_SWIPE_DOWN) uiGo(UI_HOME);
      break;
    case UI_SCENE:
      if (dir == UI_SWIPE_LEFT) uiStepScene(1);
      else if (dir == UI_SWIPE_RIGHT) uiStepScene(-1);
      else if (dir == UI_SWIPE_DOWN) uiGo(UI_HOME);
      break;
    case UI_MOOD:
      if (dir == UI_SWIPE_LEFT) ui.moodPage = (ui.moodPage + 1) % UI_MOOD_PAGES;
      else if (dir == UI_SWIPE_RIGHT) ui.moodPage = (ui.moodPage + UI_MOOD_PAGES - 1) % UI_MOOD_PAGES;
      else if (dir == UI_SWIPE_DOWN) uiGo(UI_HOME);
      break;
    case UI_LIGHT:
      if (dir == UI_SWIPE_DOWN) uiGo(UI_HOME);
      else if (dir == UI_SWIPE_RIGHT) uiBack();
      break;
    case UI_SETTINGS:
    case UI_CAT:
      if (dir == UI_SWIPE_RIGHT) uiBack();
      else if (dir == UI_SWIPE_DOWN && ui.screen == UI_SETTINGS) uiGo(UI_HOME);
      break;
    default: break;
  }
}

// ---- Timers: idle close, hints ---------------------------------------------
static void uiTick() {
  unsigned long now = millis();
  unsigned long idle = now - max(ui.lastTouch, ui.screenSince);

  if (ui.screen != UI_HOME && !ui.dragging && !uiTrack.down) {
    unsigned long limit = uiIsFullPage(ui.screen) ? UI_PAGE_IDLE_MS
                        : ui.screen == UI_SCENE ? UI_SCENE_IDLE_MS : UI_SHEET_IDLE_MS;
    if (idle > limit) uiGo(UI_HOME);
  }

  // Discoverability: the bot says the hint itself on the first few boots and
  // after a long stretch with no touch.
  if (ui.screen == UI_HOME && !infoMode.active && botMode.state == BOT_ACTIVE) {
    if (!ui.bootHintDone && now > UI_HINT_BOOT_MS) {
      ui.bootHintDone = true;
      Preferences p;
      if (p.begin("ui", false)) {
        ui.hintsShown = p.getUChar("hints", 0);
        if (ui.hintsShown < UI_HINT_BOOTS && ui.lastTouch == 0) {
          botMode.speechBubble.show("Psst! Swipe up", 4000, true);
          p.putUChar("hints", ui.hintsShown + 1);
        }
        p.end();
      }
    }
    unsigned long since = now - (ui.lastTouch ? ui.lastTouch : 0);
    if (!ui.idleHintDone && since > UI_HINT_IDLE_MS) {
      ui.idleHintDone = true;
      botMode.speechBubble.show("Psst! Swipe up", 4000, true);
    }
  }
}

// ---- Main entry: replaces the legacy handleTouch() ------------------------
void handleTouch() {
  if (!touchInitialized) return;

  #ifdef TARGET_CORES3
  // Refresh M5Unified touch state. It polls over the shared internal I2C bus,
  // so on Stackchan it takes the same mutex as the servo/battery peripherals.
  #ifdef BOARD_HAS_STACKCHAN_BASE
  {
    bool held = i2cAcquire(50);
    M5.update();
    if (held) i2cRelease();
  }
  #else
  M5.update();
  #endif
  #endif

  unsigned long now = millis();

  // Dev endpoint gestures (web task sets, main loop consumes)
  uint8_t inj = uiInjectGesture;
  if (inj) {
    int16_t ix = uiInjectX, iy = uiInjectY;
    uiInjectGesture = 0;
    ui.lastTouch = now;
    ui.idleHintDone = false;
    if (inj == 1) { uiOnDown(ix, iy); uiOnTap(ix, iy); }
    else if (inj == 2) uiOnLongPress(ix, iy);
    else uiOnSwipe(inj - 2);
    ui.pressedId = UI_ID_NONE;
    ui.dragging = false;
  }

  uint16_t rx, ry;
  bool touching = readTouch(rx, ry);
  if (touchReadBusy) { uiTick(); return; }  // bus busy: no information this frame
  int16_t x = rx, y = ry;

  if (touching) {
    ui.lastTouch = now;
    ui.idleHintDone = false;
    if (!uiTrack.down) {
      uiTrack.down = true;
      uiTrack.x0 = uiTrack.x = x;
      uiTrack.y0 = uiTrack.y = y;
      uiTrack.t0 = now;
      uiTrack.longFired = false;
      uiOnDown(x, y);
    } else {
      uiTrack.x = x;
      uiTrack.y = y;
      uiOnMove(x, y);
    }
    uiTrack.lastSeen = now;

    // Hold-to-confirm rows (Restart)
    if (ui.holdRow >= 0 && now - ui.holdStart >= UI_HOLD_MS) {
      uiBuildRows(ui.cat);
      UiRowKey key = ui.holdRow < uiRowCount ? uiRows[ui.holdRow].key : RK_FIRMWARE;
      ui.holdRow = -1;
      uiSound(SEQ_CONFIRM);
      if (key == RK_RESTART) {
        uiToast("Restarting...", UI_C_OFF, 5000);
        runBotMode();  // show the toast before we go
        delay(300);
        ESP.restart();
      }
#ifdef BOARD_HAS_FACES_BASE
      if (key == RK_GAMES) bootToChooser();  // does not return
#endif
#ifdef BOARD_HAS_STACKCHAN_BASE
      if (key == RK_POWEROFF) {
        uiToast("Goodbye!", UI_C_OFF, 5000);
        runBotMode();
        delay(300);
        scSetServoPower(false);
        scSetAllBaseLeds(0, 0, 0);
        scRefreshBaseLeds();
        M5.Display.setBrightness(0);
        delay(200);
        M5.Power.powerOff();
      }
#endif
    }

    int16_t dx = uiTrack.x - uiTrack.x0, dy = uiTrack.y - uiTrack.y0;
    if (!uiTrack.longFired && !ui.dragging && now - uiTrack.t0 >= UI_LONG_PRESS_MS &&
        abs(dx) < UI_TAP_SLOP_PX && abs(dy) < UI_TAP_SLOP_PX) {
      uiTrack.longFired = true;
      ui.pressedId = UI_ID_NONE;
      uiOnLongPress(uiTrack.x0, uiTrack.y0);
    }
  } else if (uiTrack.down && now - uiTrack.lastSeen >= UI_LIFT_DEBOUNCE_MS) {
    // Finger lifted: classify the gesture
    uiTrack.down = false;
    int16_t dx = uiTrack.x - uiTrack.x0, dy = uiTrack.y - uiTrack.y0;
    unsigned long dur = uiTrack.lastSeen - uiTrack.t0;
    ui.holdRow = -1;
    if (ui.dragging) {
      ui.dragging = false;
      ui.dragRow = -1;
    } else if (ui.scrolling) {
      ui.scrolling = false;
    } else if (!uiTrack.longFired) {
      int16_t adx = abs(dx), ady = abs(dy);
      if (max(adx, ady) >= UI_SWIPE_MIN_PX && dur <= UI_SWIPE_MAX_MS) {
        if (ady > adx) uiOnSwipe(dy < 0 ? UI_SWIPE_UP : UI_SWIPE_DOWN);
        else uiOnSwipe(dx < 0 ? UI_SWIPE_LEFT : UI_SWIPE_RIGHT);
      } else if (adx < UI_TAP_SLOP_PX && ady < UI_TAP_SLOP_PX) {
        uiOnTap(uiTrack.x0, uiTrack.y0);
      }
    }
    ui.pressedId = UI_ID_NONE;
  }

  uiTick();
}

#endif // TOUCH_UI_V2
#endif // TOUCH_UI_H
