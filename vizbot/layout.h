#pragma once
// ============================================================================
// layout.h — Centralized UI Position Constants
// ============================================================================
// All screen-dependent positions are derived here from LCD_WIDTH / LCD_HEIGHT
// (defined in config.h per board). Adding a new device only requires adding
// its dimensions to config.h — every rendering file picks up the right layout
// automatically.
//
// Include this after config.h. Override any constant in config.h before
// including layout.h if a board needs a custom value.
// ============================================================================

#include "config.h"

// ---- Boot Screen --------------------------------------------------------
#define BOOT_LEFT_MARGIN   10
#define BOOT_TOP_MARGIN    30
#if defined(TARGET_CORES3) && defined(CLOUD_ENABLED)
  #define BOOT_LINE_HEIGHT  16  // Core S3: 12 stages on 240px height
#elif defined(CLOUD_ENABLED)
  #if defined(LCD_HEIGHT) && LCD_HEIGHT <= 240
    #define BOOT_LINE_HEIGHT  18
  #else
    #define BOOT_LINE_HEIGHT  20  // 1.69" LCD: 11 stages on 280px height
  #endif
#elif defined(TARGET_CORES3)
  #define BOOT_LINE_HEIGHT  18  // Core S3 without cloud: 10 stages on 240px
#else
  #define BOOT_LINE_HEIGHT  22
#endif
#define BOOT_STATUS_X      (LCD_WIDTH - 40)   // Right-aligned; was hardcoded 200 for 240px

// ---- Speech Bubble (bot_overlays.h) ------------------------------------
#ifndef OVERLAY_BUBBLE_Y
  #define OVERLAY_BUBBLE_Y    (LCD_HEIGHT - 58)   // Near bottom; was hardcoded 220 for 280px
#endif
#define OVERLAY_BUBBLE_MAX_W  (LCD_WIDTH - 6)     // 3px margin each side; was hardcoded 234

// ---- Sleeping Zzz Animation (relative to BOT_FACE_CX/CY) ---------------
#define ZZZ_OFFSET_X    50
#define ZZZ_OFFSET_Y   (-40)

// ---- Info Mode: Mini Eyes (top-right corner) ----------------------------
// Was hardcoded CX=190 / CY=24 for 240px portrait.
// Now 50px in from right edge so it adapts to wider/narrower screens.
#define MINI_EYE_CX    (LCD_WIDTH - 50)
#define MINI_EYE_CY    24

// ---- Info Mode: Weather Content Y Positions ----------------------------
#define INFO_ICON_Y        64                      // Weather icon center Y
#define INFO_TEMP_Y        42                      // Large temperature number Y
#define INFO_CONDITION_Y   (LCD_HEIGHT / 2 - 12)  // Condition text (108 for 240, 128 for 280)
#define INFO_DIVIDER_Y     (LCD_HEIGHT / 2 + 10)  // Current / forecast divider (130 for 240)
#define INFO_LOADING_X     (LCD_WIDTH / 4)        // Loading / error text X (60 for 240)
#define INFO_LOADING_Y     (LCD_HEIGHT / 3)       // Loading / error text Y (80 for 240)

// ---- Info Mode: Forecast Bar Columns (3 columns, evenly spaced) ---------
// Was hardcoded 40 / 120 / 200 for 240px width.
#define FORECAST_COL_0    (LCD_WIDTH / 6)         // 40 for 240px, 53 for 320px
#define FORECAST_COL_1    (LCD_WIDTH / 2)         // 120 for 240px, 160 for 320px
#define FORECAST_COL_2    (LCD_WIDTH * 5 / 6)     // 200 for 240px, 267 for 320px
#define FORECAST_BAR_W    (LCD_WIDTH / 5)         // 48 for 240px, 64 for 320px
#define FORECAST_BAR_MAX_H (LCD_HEIGHT - INFO_DIVIDER_Y - 15)  // 95 for 240px
