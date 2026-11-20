// SPDX-License-Identifier: Apache-2.0
#pragma once

/* Kaleidoscope mapper. Portable C: no GSP, FreeRTOS or board dependencies.
 *
 * The fold geometry (mode, mirror count, tiling scale) is precomputed once
 * into a per-pixel lookup table of wedge-local coordinates. Each frame then
 * costs one affine transform + one texture fetch per output pixel, so view
 * changes (rotation, zoom, pan) never rebuild the table. */

#include <stddef.h>
#include <stdint.h>

/* PORT: optional per-platform overrides (KO_SRC_SHIFT, KO_OUT, KO_FOLD_ALLOC). */
#if defined(__has_include)
#if __has_include("ko_port.h")
#include "ko_port.h"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define KO_OUT_W 480
#define KO_OUT_H 480
#ifndef KO_SRC_SHIFT
#define KO_SRC_SHIFT 8          /* Device build overrides (project CMakeLists). */
#endif
#define KO_SRC_N (1 << KO_SRC_SHIFT) /**< Source texture edge, power of two. */

typedef enum {
    KO_MODE_RADIAL = 0, /**< Classic 2-mirror wedge, n segments. */
    KO_MODE_TRIANGLE,   /**< 3-mirror triangular tiling. */
    KO_MODE_SQUARE,     /**< 4-mirror square tiling. */
    KO_MODE_SNOWFLAKE,  /**< 30-60-90 triangle mirrors (p6m): 6-fold stars on a hexagonal tiling. */
    KO_MODE_TUNNEL,     /**< Log-polar: n mirrored segments receding into the centre. */
    /* Procedural drum looks (kaleido_membrane): no source texture, so this
     * engine rejects them; the app renders them with km_membrane instead. */
    KO_MODE_DRUM_CATHEDRAL,   /**< Bessel membrane, Phase colouring (Cymatics "Cathedral"). */
    KO_MODE_DRUM_ACID,        /**< Bessel membrane, Wave colouring (Cymatics "Acid Bloom"). */
    KO_MODE_COUNT,
} ko_mode_t;

#define KO_MODE_MIRROR_COUNT KO_MODE_DRUM_CATHEDRAL  /**< Modes this engine renders. */

/** True for the procedural drum modes (rendered without a source texture). */
static inline int ko_mode_is_drum(int mode)
{
    return mode >= KO_MODE_DRUM_CATHEDRAL && mode < KO_MODE_COUNT;
}

typedef struct {
    ko_mode_t mode;
    int segments;   /**< Mirror segments for radial and tunnel modes, 2..24. */
    float scale;    /**< Tiling density (triangle/square/snowflake) or ring depth (tunnel). */
} ko_geometry_t;

typedef struct {
    float angle;    /**< Radians, rotation of the sampled wedge. */
    float zoom;     /**< >1 magnifies the source. */
    float off_x;    /**< Sample centre offset, source units (-0.5..0.5). */
    float off_y;
    int smooth;     /**< Non-zero: bilinear sampling (about 3x the cost). */
    double travel;  /**< Shift along the fold's x axis (tunnel depth), fold units.
                     *   Double: it grows without bound and must keep tiny steps. */
} ko_view_t;

typedef struct ko_engine ko_engine_t;

/** Allocate an engine for KO_OUT_W x KO_OUT_H output; NULL on failure. */
ko_engine_t *ko_engine_create(void);

/** Same, for a smaller output (e.g. half size, upscaled in hardware). The
 * pattern covers the same view at any size; render() rectangles are in
 * this engine's own pixels. */
ko_engine_t *ko_engine_create_sized(int width, int height);
void ko_engine_destroy(ko_engine_t *engine);

/** Rebuild the fold table if the geometry changed. Returns 0 on success,
 * -1 for invalid geometry, including the drum modes. */
int ko_engine_set_geometry(ko_engine_t *engine, const ko_geometry_t *geometry);

/** Render a rectangle of the output. src is KO_SRC_N x KO_SRC_N RGB565,
 * sampled with mirrored repeat. dst points at pixel (x0, y0); stride is in
 * pixels. Safe to call per partition from a draw callback. */
/** Unpack an RGB565 KO_SRC_N^2 texture into the 32-bit "spread" layout
 * (0x07E0F81F) that ko_engine_render_spread() samples. */
void ko_engine_spread_texture(const uint16_t *src, uint32_t *dst);

/** Bilinear render from a spread texture (always smooth; faster than
 * ko_engine_render with view->smooth). Same arguments otherwise. */
void ko_engine_render_spread(const ko_engine_t *engine, const uint32_t *src32,
                             const ko_view_t *view, uint16_t *dst, size_t stride,
                             int x0, int y0, int width, int height);

void ko_engine_render(const ko_engine_t *engine, const uint16_t *src,
                      const ko_view_t *view, uint16_t *dst, size_t stride,
                      int x0, int y0, int width, int height);

#ifdef __cplusplus
}
#endif
