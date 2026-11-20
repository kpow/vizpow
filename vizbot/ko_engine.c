// SPDX-License-Identifier: Apache-2.0
/* vizbot copy of esp-mosaico projects/kaleido/components/kaleido_core (commit a7438ac).
 * Port changes are marked PORT. Built only with -DKALEIDO_ENABLED. */
#ifdef KALEIDO_ENABLED
#pragma GCC optimize("O2")
#include "ko_engine.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define KO_Q_ONE 4096.0f            /* Fold coordinates are Q12 in int16. */
/* Texture coordinates are unsigned fixed point with KO_FRAC fraction bits,
 * chosen so the 2N-texel mirrored-repeat period is exactly 2^32: integer
 * overflow then wraps onto the same texel, and the rotation coefficients
 * keep ~20 bits of precision at any zoom (Q16 left as few as 8 when zoomed
 * in, which made slow motion stall and jump). */
#define KO_FRAC (31 - KO_SRC_SHIFT)
#define KO_TEX_PERIOD 4294967296.0
#define KO_TRI_ITERATIONS 48
#define KO_PI 3.14159265358979f

/* PORT: where the fold table lives, and the output pixel byte order. */
#ifndef KO_FOLD_ALLOC
#define KO_FOLD_ALLOC(bytes) malloc(bytes)
#define KO_FOLD_FREE(ptr) free(ptr)
#endif
#ifndef KO_OUT
#define KO_OUT(v) ((uint16_t)(v))
#endif
/* PORT: where everything else the engine allocates lives. */
#ifndef KO_CALLOC
#define KO_CALLOC(n, size) calloc((n), (size))
#define KO_FREE(ptr) free(ptr)
#endif

struct ko_engine {
    int w, h;                       /* Output size this table covers. */
    ko_geometry_t geometry;
    float sample_scale;             /* Mode-specific wedge-to-texture factor. */
    int valid;
    int16_t *fold;                  /* Interleaved qx, qy per output pixel. */
};

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float triangle_wave(float x)
{
    float m = fmodf(x + 1.0f, 4.0f);
    if (m < 0.0f) {
        m += 4.0f;
    }
    return 1.0f - fabsf(m - 2.0f);
}

/* Reflect into the fundamental equilateral triangle (inradius 0.5, centred). */
static void fold_equilateral(float *tx, float *ty)
{
    static const float nx[3] = {0.0f, 0.8660254f, -0.8660254f};
    static const float ny[3] = {-1.0f, 0.5f, 0.5f};
    for (int i = 0; i < KO_TRI_ITERATIONS; ++i) {
        int moved = 0;
        for (int k = 0; k < 3; ++k) {
            const float d = *tx * nx[k] + *ty * ny[k] - 0.5f;
            if (d > 0.0f) {
                *tx -= 2.0f * d * nx[k];
                *ty -= 2.0f * d * ny[k];
                moved = 1;
            }
        }
        if (!moved) {
            break;
        }
    }
}

/* Mirror an angle into [0, seg] with mirrors every seg radians. */
static float fold_angle(float a, float seg)
{
    a = fmodf(a, 2.0f * seg);
    if (a < 0.0f) {
        a += 2.0f * seg;
    }
    return a > seg ? 2.0f * seg - a : a;
}

static void fold_point(const ko_geometry_t *g, float px, float py,
                       float *qx, float *qy)
{
    if (g->mode == KO_MODE_TUNNEL) {
        /* Depth = -log(radius): equal steps toward the centre look like
         * equal steps down a tube. Angle is mirrored into n segments. */
        const float r = fmaxf(sqrtf(px * px + py * py), 1e-3f);
        const float seg = KO_PI / (float)g->segments;
        const float a = fold_angle(atan2f(py, px) + KO_PI * 0.5f, seg);
        *qx = clampf(-logf(r) * 0.35f * g->scale, -7.5f, 7.5f);
        *qy = a / seg * 0.5f;
        return;
    }
    if (g->mode == KO_MODE_RADIAL) {
        const float seg = KO_PI / (float)g->segments;
        const float r = sqrtf(px * px + py * py);
        float a = atan2f(py, px) + KO_PI * 0.5f;
        a = fmodf(a, 2.0f * seg);
        if (a < 0.0f) {
            a += 2.0f * seg;
        }
        a = fabsf(a - seg);
        *qx = r * cosf(a);
        *qy = r * sinf(a);
        return;
    }
    float tx = px * g->scale;
    float ty = py * g->scale;
    if (g->mode == KO_MODE_SNOWFLAKE) {
        ty += 1.0f;     /* Centre the screen on a triangle vertex: a 6-fold point. */
    }
    if (g->mode == KO_MODE_SQUARE) {
        *qx = triangle_wave(tx);
        *qy = triangle_wave(ty);
        return;
    }
    fold_equilateral(&tx, &ty);
    if (g->mode == KO_MODE_SNOWFLAKE) {
        /* Add the triangle's three medians as mirrors: the 30-60-90 half
         * of one sixth of it. Mirror lines through the centre at 30, 90 and
         * 150 degrees, so fold the angle into the 60-degree wedge 30..90. */
        const float r = sqrtf(tx * tx + ty * ty);
        const float a = fold_angle(atan2f(ty, tx) - KO_PI / 6.0f, KO_PI / 3.0f) + KO_PI / 6.0f;
        tx = r * cosf(a);
        ty = r * sinf(a) - 1.0f;    /* Sample relative to the 6-fold vertex. */
    }
    *qx = tx;
    *qy = ty;
}

ko_engine_t *ko_engine_create(void)
{
    return ko_engine_create_sized(KO_OUT_W, KO_OUT_H);
}

ko_engine_t *ko_engine_create_sized(int width, int height)
{
    if (width <= 0 || height <= 0 || width > KO_OUT_W || height > KO_OUT_H) {
        return NULL;
    }
    ko_engine_t *engine = KO_CALLOC(1, sizeof(*engine));
    if (!engine) {
        return NULL;
    }
    engine->w = width;
    engine->h = height;
    engine->fold = KO_FOLD_ALLOC((size_t)width * height * 2 * sizeof(int16_t));
    if (!engine->fold) {
        KO_FREE(engine);
        return NULL;
    }
    return engine;
}

void ko_engine_destroy(ko_engine_t *engine)
{
    if (engine) {
        KO_FOLD_FREE(engine->fold);
        KO_FREE(engine);
    }
}

int ko_engine_set_geometry(ko_engine_t *engine, const ko_geometry_t *geometry)
{
    if (!engine || !geometry || geometry->mode >= KO_MODE_MIRROR_COUNT) {
        return -1;
    }
    ko_geometry_t g = *geometry;
    g.segments = g.segments < 2 ? 2 : (g.segments > 24 ? 24 : g.segments);
    g.scale = clampf(g.scale, 1.0f, 6.0f);
    if (engine->valid && memcmp(&g, &engine->geometry, sizeof(g)) == 0) {
        return 0;
    }
    /* PORT: non-square output. Unit radius is half the short side, centred. */
    const float half = 0.5f * (float)(engine->w < engine->h ? engine->w : engine->h);
    const float cx = 0.5f * (float)engine->w, cy = 0.5f * (float)engine->h;
    int16_t *out = engine->fold;
    for (int y = 0; y < engine->h; ++y) {
        const float py = ((float)y + 0.5f - cy) / half;
        for (int x = 0; x < engine->w; ++x) {
            const float px = ((float)x + 0.5f - cx) / half;
            float qx, qy;
            fold_point(&g, px, py, &qx, &qy);
            *out++ = (int16_t)clampf(qx * KO_Q_ONE, -32767.0f, 32767.0f);
            *out++ = (int16_t)clampf(qy * KO_Q_ONE, -32767.0f, 32767.0f);
        }
    }
    engine->geometry = g;
    engine->sample_scale = g.mode == KO_MODE_TRIANGLE ? 0.45f
                           : (g.mode == KO_MODE_SNOWFLAKE ? 0.55f : 0.5f);
    engine->valid = 1;
    return 0;
}

/* RGB565 spread so R, G and B blend in one 32-bit multiply. */
#define SPREAD_MASK 0x07E0F81Fu
static inline uint32_t spread(uint16_t c)
{
    return ((uint32_t)c | ((uint32_t)c << 16)) & SPREAD_MASK;
}

static inline uint32_t fold_coord(int32_t t, int32_t wrap)
{
    t &= wrap;
    t ^= -(t >> KO_SRC_SHIFT) & wrap;
    return (uint32_t)t;
}

static void render_smooth(const ko_engine_t *engine, const uint16_t *src,
                          uint32_t a, uint32_t b, uint32_t cu, uint32_t cv,
                          uint16_t *dst, size_t stride, int x0, int y0, int width, int height)
{
    const int32_t wrap = (KO_SRC_N << 1) - 1;
    for (int y = 0; y < height; ++y) {
        const int16_t *q = engine->fold + ((size_t)(y0 + y) * (size_t)engine->w + (size_t)x0) * 2;
        uint16_t *row = dst + (size_t)y * stride;
        for (int x = 0; x < width; ++x) {
            const uint32_t qx = (uint32_t)(int32_t)q[0], qy = (uint32_t)(int32_t)q[1];
            q += 2;
            /* Texel centres sit at +0.5; cu/cv already subtract it. */
            const uint32_t uf = a * qx - b * qy + cu;
            const uint32_t vf = b * qx + a * qy + cv;
            const int32_t u = (int32_t)(uf >> KO_FRAC), v = (int32_t)(vf >> KO_FRAC);
            const uint32_t wx = (uf >> (KO_FRAC - 5)) & 31u, wy = (vf >> (KO_FRAC - 5)) & 31u;
            const uint32_t u0 = fold_coord(u, wrap), u1 = fold_coord(u + 1, wrap);
            const uint32_t r0 = fold_coord(v, wrap) << KO_SRC_SHIFT;
            const uint32_t r1 = fold_coord(v + 1, wrap) << KO_SRC_SHIFT;
            const uint32_t p00 = spread(src[r0 | u0]), p01 = spread(src[r0 | u1]);
            const uint32_t p10 = spread(src[r1 | u0]), p11 = spread(src[r1 | u1]);
            const uint32_t top = ((p00 * (32u - wx) + p01 * wx) >> 5) & SPREAD_MASK;
            const uint32_t bot = ((p10 * (32u - wx) + p11 * wx) >> 5) & SPREAD_MASK;
            const uint32_t c = ((top * (32u - wy) + bot * wy) >> 5) & SPREAD_MASK;
            row[x] = KO_OUT(c | (c >> 16));
        }
    }
}

/* View -> fixed-point rotation (a, b) and offset (cu, cv); smooth sampling
 * subtracts half a texel so texel centres sit at +0.5. */
static void view_coeffs(const ko_engine_t *engine, const ko_view_t *view, bool smooth,
                        uint32_t *pa, uint32_t *pb, uint32_t *pcu, uint32_t *pcv)
{
    const float zoom = view->zoom < 0.05f ? 0.05f : view->zoom;
    const float k = (float)KO_SRC_N * engine->sample_scale / zoom;
    const float to_fix = k * (float)(1u << KO_FRAC) / KO_Q_ONE;
    const int32_t ai = (int32_t)lrintf(cosf(view->angle) * to_fix);
    const int32_t bi = (int32_t)lrintf(sinf(view->angle) * to_fix);
    const double one = (double)(1u << KO_FRAC) * (double)KO_SRC_N;
    const double half = smooth ? (double)(1u << (KO_FRAC - 1)) : 0.0;
    double du = (view->off_x + 0.5) * one + (double)ai * view->travel * KO_Q_ONE - half;
    double dv = (view->off_y + 0.5) * one + (double)bi * view->travel * KO_Q_ONE - half;
    du -= floor(du / KO_TEX_PERIOD) * KO_TEX_PERIOD;
    dv -= floor(dv / KO_TEX_PERIOD) * KO_TEX_PERIOD;
    *pa = (uint32_t)ai;
    *pb = (uint32_t)bi;
    *pcu = (uint32_t)du;
    *pcv = (uint32_t)dv;
}

void ko_engine_spread_texture(const uint16_t *src, uint32_t *dst)
{
    for (size_t i = 0; i < (size_t)KO_SRC_N * KO_SRC_N; ++i) {
        dst[i] = spread(src[i]);
    }
}

/* Each coordinate is folded once, fraction included (mirrored repeat is
 * continuous), and the +1 neighbour clamps at the mirror edge, where the
 * mirror image of a texel is itself. */
void ko_engine_render_spread(const ko_engine_t *engine, const uint32_t *src32,
                             const ko_view_t *view, uint16_t *dst, size_t stride,
                             int x0, int y0, int width, int height)
{
    if (!engine || !engine->valid || !src32 || !view || !dst) {
        return;
    }
    uint32_t a, b, cu, cv;
    view_coeffs(engine, view, true, &a, &b, &cu, &cv);
    const int sh = KO_FRAC - 5;                         /* Keep 5 fraction bits. */
    const uint32_t mirror = ((uint32_t)KO_SRC_N << 6) - 1u;   /* 2N texels x 32 - 1 */
    const uint32_t last = (uint32_t)KO_SRC_N - 1u;
    for (int y = 0; y < height; ++y) {
        const int16_t *q = engine->fold + ((size_t)(y0 + y) * (size_t)engine->w + (size_t)x0) * 2;
        uint16_t *row = dst + (size_t)y * stride;
        for (int x = 0; x < width; ++x) {
            const uint32_t qx = (uint32_t)(int32_t)q[0], qy = (uint32_t)(int32_t)q[1];
            q += 2;
            uint32_t tu = (a * qx - b * qy + cu) >> sh;
            uint32_t tv = (b * qx + a * qy + cv) >> sh;
            tu ^= (0u - (tu >> (KO_SRC_SHIFT + 5))) & mirror;
            tv ^= (0u - (tv >> (KO_SRC_SHIFT + 5))) & mirror;
            const uint32_t u0 = tu >> 5, v0 = tv >> 5, wx = tu & 31u, wy = tv & 31u;
            const uint32_t du = (u0 - last) >> 31;                     /* 1 unless at the edge */
            const uint32_t dv = ((v0 - last) >> 31) << KO_SRC_SHIFT;
            const uint32_t *r = src32 + (v0 << KO_SRC_SHIFT) + u0;
            const uint32_t p00 = r[0], p01 = r[du], p10 = r[dv], p11 = r[dv + du];
            const uint32_t top = ((p00 * (32u - wx) + p01 * wx) >> 5) & SPREAD_MASK;
            const uint32_t bot = ((p10 * (32u - wx) + p11 * wx) >> 5) & SPREAD_MASK;
            const uint32_t c = ((top * (32u - wy) + bot * wy) >> 5) & SPREAD_MASK;
            row[x] = KO_OUT(c | (c >> 16));
        }
    }
}

void ko_engine_render(const ko_engine_t *engine, const uint16_t *src,
                      const ko_view_t *view, uint16_t *dst, size_t stride,
                      int x0, int y0, int width, int height)
{
    if (!engine || !engine->valid || !src || !view || !dst) {
        return;
    }
    /* u = N*(rot(q)*k/zoom + off + 0.5), in KO_FRAC fixed point from Q12. */
    uint32_t a, b, cu, cv;
    view_coeffs(engine, view, view->smooth != 0, &a, &b, &cu, &cv);
    if (view->smooth) {
        render_smooth(engine, src, a, b, cu, cv, dst, stride, x0, y0, width, height);
        return;
    }
    const int32_t wrap = (KO_SRC_N << 1) - 1;
    for (int y = 0; y < height; ++y) {
        const int16_t *q = engine->fold + ((size_t)(y0 + y) * (size_t)engine->w + (size_t)x0) * 2;
        uint16_t *row = dst + (size_t)y * stride;
        for (int x = 0; x < width; ++x) {
            const uint32_t qx = (uint32_t)(int32_t)q[0], qy = (uint32_t)(int32_t)q[1];
            q += 2;
            int32_t u = (int32_t)((a * qx - b * qy + cu) >> KO_FRAC);
            int32_t v = (int32_t)((b * qx + a * qy + cv) >> KO_FRAC);
            u &= wrap;   /* Mirrored repeat: fold [N, 2N) back onto [0, N). */
            v &= wrap;
            u ^= -(u >> KO_SRC_SHIFT) & wrap;
            v ^= -(v >> KO_SRC_SHIFT) & wrap;
            row[x] = KO_OUT(src[((uint32_t)v << KO_SRC_SHIFT) | (uint32_t)u]);
        }
    }
}
#endif /* KALEIDO_ENABLED */
