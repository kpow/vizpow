// SPDX-License-Identifier: Apache-2.0
#pragma once

/* A "look" is the full set of tunable kaleidoscope parameters, plus the
 * colour grade that turns an RGB888 source into the RGB565 texture the
 * engine samples. */

#include <stdbool.h>
#include <stdint.h>
#include "ko_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ko_mode_t mode;
    int segments;       /**< Radial mirrors. */
    float scale;        /**< Tiling density (triangle/square). */
    float zoom;
    float spin;         /**< Auto rotation, radians/second. */
    float hue;          /**< Hue rotation, radians. */
    float hue_drift;    /**< Hue cycling, radians/second. */
    float saturation;
    float contrast;
    bool invert;
    float speed;        /**< User animation multiplier (the Speed setting), 0.1..3. */
    float pace;         /**< Hidden per-look calibration: speed 1x moves equally in every look. */
} ko_look_t;

typedef enum {
    KO_PARAM_MIRRORS = 0,
    KO_PARAM_SYMMETRY,
    KO_PARAM_ZOOM,
    KO_PARAM_SPIN,
    KO_PARAM_SPEED,
    KO_PARAM_HUE,
    KO_PARAM_DRIFT,
    KO_PARAM_VIVID,
    KO_PARAM_CONTRAST,
    KO_PARAM_INVERT,
    KO_PARAM_COUNT,
} ko_param_t;

/* Presets keep their index forever (saved tuned looks and the last look are
 * stored by index): add new looks at the end only. 8 = Cathedral and
 * 9 = Acid Bloom, the procedural drum looks (2026-11-14). */
#define KO_LOOK_COUNT 10

/** Built-in look presets from the approved design. */
const ko_look_t *ko_look_preset(int index);
const char *ko_look_name(int index);

/** Fill a look with a random, tasteful combination. seed advances. */
void ko_look_random(ko_look_t *look, uint32_t *seed);

/** Tune parameter metadata and stepping (direction -1 / +1). Symmetry steps
 * within the current family: mirror modes, or the two drum modes. */
const char *ko_param_name(ko_param_t param);
void ko_param_step(ko_look_t *look, ko_param_t param, int direction);
/** Human-readable value, e.g. "8", "1.4x", "+0.12". */
void ko_param_format(const ko_look_t *look, ko_param_t param, char *out, int capacity);

/** Grade an RGB888 size*size source into RGB565 with hue/sat/contrast/
 * invert applied. hue is the current absolute hue (radians). */
void ko_grade(const uint8_t *rgb, uint16_t *out565, int size,
              float hue, float saturation, float contrast, bool invert);

#ifdef __cplusplus
}
#endif
