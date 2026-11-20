// SPDX-License-Identifier: Apache-2.0
/* vizbot copy of esp-mosaico projects/kaleido/components/kaleido_core (commit a7438ac).
 * Port changes are marked PORT. Built only with -DKALEIDO_ENABLED. */
#ifdef KALEIDO_ENABLED
#pragma GCC optimize("O2")
#include "ko_organic.h"

#include <math.h>
#include <stdlib.h>

#define KO_PI 3.14159265358979f
#define KO_TAU 6.28318530717959f
#define CELL_COUNT 16
#define ANGLE_STEPS 64          /* Per-cell outline radius table. */
#define GRID_DIV 8              /* Coarse noise grid: one sample per 8 texels. */
#define MARBLE_DIV 2            /* Marbling bands need a finer field. */
#define CELL_MARGIN 0.11f       /* Beyond this distance a cell adds nothing. */
#define SIN_STEPS 1024          /* Sine table over one period. */
#define EXP_STEPS 1024          /* exp(-x) table over [0, EXP_RANGE). */
#define EXP_RANGE 12.0f

typedef struct {
    float cx, cy, r0, rmax;
    float radius[ANGLE_STEPS + 1];
    float rgb[3];
} cell_t;

struct ko_organic {
    /* Lookup tables: libm sin/exp are very slow on the device's RISC-V core. */
    float sin_tab[SIN_STEPS + 1];
    float exp_tab[EXP_STEPS + 1];
    int size;
    int grid;                   /* Coarse grid edge (size / GRID_DIV + 1). */
    int mgrid;                  /* Marbling grid edge (size / MARBLE_DIV + 1). */
    float *warp_x, *warp_y;     /* Domain warp, coarse. */
    float *bg, *ring;           /* Background blend and ring jitter, coarse. */
    float *mq_x, *mq_y, *mr;    /* Marbling fields, coarse. */
    cell_t cells[CELL_COUNT];
};

/* ---------------------------------------------------------------- fast math */

static float fast_sin(const ko_organic_t *g, float x)
{
    float turns = x * (1.0f / KO_TAU);
    turns -= (float)(int)turns;         /* fractional turn, sign kept */
    if (turns < 0.0f) {
        turns += 1.0f;
    }
    const float f = turns * (float)SIN_STEPS;
    int i = (int)f;
    const float w = f - (float)i;
    i &= SIN_STEPS - 1;
    return g->sin_tab[i] + (g->sin_tab[i + 1] - g->sin_tab[i]) * w;
}

static float fast_cos(const ko_organic_t *g, float x)
{
    return fast_sin(g, x + KO_PI * 0.5f);
}

/* exp(-x) for x >= 0 (negative x clamps to 1). */
static float fast_expneg(const ko_organic_t *g, float x)
{
    if (x <= 0.0f) {
        return 1.0f;
    }
    if (x >= EXP_RANGE) {
        return 0.0f;
    }
    const float f = x * (EXP_STEPS / EXP_RANGE);
    const int i = (int)f;
    return g->exp_tab[i] + (g->exp_tab[i + 1] - g->exp_tab[i]) * (f - (float)i);
}

/* atan2 approximation, max error about 0.0015 rad. */
static float fast_atan2(float y, float x)
{
    const float ax = fabsf(x), ay = fabsf(y);
    const float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx == 0.0f) {
        return 0.0f;
    }
    const float a = mn / mx, s2 = a * a;
    float r = ((-0.0464964749f * s2 + 0.15931422f) * s2 - 0.327622764f) * s2 * a + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0.0f) {
        r = 3.14159274f - r;
    }
    return y < 0.0f ? -r : r;
}

static void init_tables(ko_organic_t *g)
{
    for (int i = 0; i <= SIN_STEPS; ++i) {
        g->sin_tab[i] = sinf((float)i * (KO_TAU / SIN_STEPS));
    }
    for (int i = 0; i <= EXP_STEPS; ++i) {
        g->exp_tab[i] = expf(-(float)i * (EXP_RANGE / EXP_STEPS));
    }
}

/* ---------------------------------------------------------------- noise */

static float hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFFFu) * (1.0f / 16777215.0f);
}

static float value_noise(float x, float y)
{
    const float fx0 = floorf(x), fy0 = floorf(y);
    const int ix = (int)fx0, iy = (int)fy0;
    const float fx = x - fx0, fy = y - fy0;
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash2(ix, iy), b = hash2(ix + 1, iy);
    const float c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

static float fbm(float x, float y)
{
    float sum = 0.0f, amp = 0.5f;
    for (int i = 0; i < 4; ++i) {
        sum += amp * value_noise(x, y);
        x = x * 2.03f + 1.7f;
        y = y * 2.03f + 9.2f;
        amp *= 0.5f;
    }
    return sum;
}

static float hash1(float seed, float salt)
{
    return hash2((int)(seed * 977.0f), (int)(salt * 131.0f) + 17);
}

/* ---------------------------------------------------------------- helpers */

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float smoothstep(float e0, float e1, float x)
{
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

static float smin(float a, float b, float k)
{
    const float h = clamp01(0.5f + 0.5f * (b - a) / k);
    return b + (a - b) * h - k * h * (1.0f - h);
}

static float sample_grid(const float *g, int grid, float gx, float gy)
{
    int ix = (int)gx, iy = (int)gy;
    if (ix >= grid - 1) {
        ix = grid - 2;
    }
    if (iy >= grid - 1) {
        iy = grid - 2;
    }
    const float fx = gx - (float)ix, fy = gy - (float)iy;
    const float *r0 = g + iy * grid + ix, *r1 = r0 + grid;
    const float top = r0[0] + (r0[1] - r0[0]) * fx;
    const float bottom = r1[0] + (r1[1] - r1[0]) * fx;
    return top + (bottom - top) * fy;
}

static void swatch(int variant, float h, float out[3])
{
    static const float lava[5][3] = {
        {1.0f, 0.35f, 0.30f}, {1.0f, 0.69f, 0.0f}, {0.88f, 0.2f, 0.54f},
        {0.07f, 0.76f, 0.69f}, {0.48f, 0.3f, 1.0f},
    };
    static const float flora[5][3] = {
        {1.0f, 0.31f, 0.55f}, {1.0f, 0.76f, 0.23f}, {0.69f, 0.36f, 1.0f},
        {0.36f, 0.82f, 0.42f}, {1.0f, 0.54f, 0.36f},
    };
    int i = (int)(h * 5.0f);
    i = i < 0 ? 0 : (i > 4 ? 4 : i);
    const float *c = variant == KO_ORGANIC_LAVA_CELLS ? lava[i] : flora[i];
    out[0] = c[0];
    out[1] = c[1];
    out[2] = c[2];
}

/* Outline radius multiplier for morph stage s at polar angle th. */
static float stage_radius(int s, float th, float ph, float floral)
{
    switch (s) {
    case 1:
        return 1.0f + (0.30f + floral * 0.1f) * cosf(3.0f * th + ph);
    case 2:
        return 1.0f + (0.38f + floral * 0.12f) * cosf((5.0f + floral * 2.0f) * th + ph);
    case 3:
        return 1.0f + 0.24f * cosf(2.0f * th + ph);
    default:
        return 1.0f;
    }
}

static void update_cells(ko_organic_t *gen, int variant, float t)
{
    const float floral = variant == KO_ORGANIC_FLORA ? 1.0f : 0.0f;
    for (int i = 0; i < CELL_COUNT; ++i) {
        cell_t *c = &gen->cells[i];
        const float sd = (float)i * 7.31f + (float)variant * 3.1f;
        /* Orbit the texture centre, where the kaleidoscope samples its wedge,
         * so the view is never empty. Radius breathes slowly; direction varies. */
        const float orbit = 0.04f + 0.40f * hash1(sd, 1.0f);
        const float speed = (0.05f + 0.08f * hash1(sd, 2.0f)) * (i & 1 ? 1.0f : -1.0f);
        const float rr = orbit * (1.0f + 0.18f * sinf(t * 0.21f + sd));
        const float th0 = sd * 2.39996f + t * speed;
        c->cx = 0.5f + rr * cosf(th0);
        c->cy = 0.5f + rr * sinf(th0);
        c->r0 = 0.08f + 0.08f * hash1(sd, 3.0f);
        const float st = t * 0.11f + hash1(sd, 4.0f) * 4.0f;
        const float st_floor = floorf(st);
        const int k = ((int)st_floor) & 3, k2 = (k + 1) & 3;
        const float m = smoothstep(0.15f, 0.85f, st - st_floor);
        const float ph = t * 0.17f + sd;
        c->rmax = 0.0f;
        for (int a = 0; a <= ANGLE_STEPS; ++a) {
            const float th = (float)a / ANGLE_STEPS * KO_TAU - KO_PI;
            float r = stage_radius(k, th, ph, floral) * (1.0f - m)
                      + stage_radius(k2, th, ph, floral) * m;
            r += 0.28f * (fbm(cosf(th) * 1.4f + sd, sinf(th) * 1.4f + t * 0.14f) - 0.5f);
            c->radius[a] = r;
            if (r > c->rmax) {
                c->rmax = r;
            }
        }
        swatch(variant, hash1(sd, 5.0f), c->rgb);
    }
}

/* Signed distance-like value to cell outline, plus the polar angle. */
static float cell_distance(const cell_t *c, float px, float py, float *angle)
{
    const float dx = px - c->cx, dy = py - c->cy;
    const float th = fast_atan2(dy, dx);
    float f = (th + KO_PI) * (ANGLE_STEPS / KO_TAU);
    int i = (int)f;
    i = i < 0 ? 0 : (i >= ANGLE_STEPS ? ANGLE_STEPS - 1 : i);
    f -= (float)i;
    const float r = c->radius[i] + (c->radius[i + 1] - c->radius[i]) * f;
    *angle = th;
    return sqrtf(dx * dx + dy * dy) - c->r0 * r;
}

static void update_grids(ko_organic_t *gen, int variant, float t)
{
    const int g = gen->grid;
    for (int y = 0; y < g; ++y) {
        for (int x = 0; x < g; ++x) {
            const float px = (float)x / (float)(g - 1), py = (float)y / (float)(g - 1);
            const int i = y * g + x;
            gen->bg[i] = fbm(px * 2.5f + t * 0.02f, py * 2.5f + t * 0.02f);
            if (variant != KO_ORGANIC_MARBLING) {
                gen->warp_x[i] = fbm(px * 3.0f + t * 0.05f, py * 3.0f + t * 0.05f) - 0.5f;
                gen->warp_y[i] = fbm(px * 3.0f + 4.1f - t * 0.04f, py * 3.0f + 7.3f - t * 0.04f) - 0.5f;
                gen->ring[i] = fbm(px * 9.0f, py * 9.0f);
            }
        }
    }
    if (variant != KO_ORGANIC_MARBLING) {
        return;
    }
    const int m = gen->mgrid;
    for (int y = 0; y < m; ++y) {
        for (int x = 0; x < m; ++x) {
            const float px = (float)x / (float)(m - 1), py = (float)y / (float)(m - 1);
            const int i = y * m + x;
            const float qx = fbm(px * 3.0f + t * 0.04f, py * 3.0f + t * 0.04f);
            const float qy = fbm(px * 3.0f + 5.2f - t * 0.03f, py * 3.0f + 1.3f - t * 0.03f);
            gen->mq_x[i] = qx;
            gen->mq_y[i] = qy;
            gen->mr[i] = fbm(px * 3.0f + 4.0f * qx + t * 0.02f, py * 3.0f + 4.0f * qy);
        }
    }
}

static void put_pixel(uint8_t *p, float r, float g, float b)
{
    p[0] = (uint8_t)(clamp01(r) * 255.0f + 0.5f);
    p[1] = (uint8_t)(clamp01(g) * 255.0f + 0.5f);
    p[2] = (uint8_t)(clamp01(b) * 255.0f + 0.5f);
}

static void render_marbling(ko_organic_t *gen, float t, uint8_t *rgb)
{
    const int n = gen->size, g = gen->mgrid;
    const float to_grid = (float)(g - 1) / (float)n;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float gx = (float)x * to_grid, gy = (float)y * to_grid;
            const float qx = sample_grid(gen->mq_x, g, gx, gy);
            const float qy = sample_grid(gen->mq_y, g, gx, gy);
            const float r = sample_grid(gen->mr, g, gx, gy);
            const float band = 0.5f + 0.5f * fast_sin(gen, r * 24.0f + qx * 6.0f);
            const float h = r * 1.3f + qy * 0.6f + t * 0.01f;
            float c[3] = {
                0.5f + 0.5f * fast_cos(gen, KO_TAU * (h + 0.0f)),
                0.5f + 0.5f * fast_cos(gen, KO_TAU * (h + 0.15f)),
                0.5f + 0.5f * fast_cos(gen, KO_TAU * (h + 0.35f)),
            };
            const float lit = 0.22f + 0.78f * smoothstep(0.15f, 0.85f, band);
            const float v1 = 1.0f - fabsf(band * 2.0f - 1.0f);
            const float v2 = v1 * v1, v4 = v2 * v2, v8 = v4 * v4, v16 = v8 * v8;
            const float vein = v16 * v2 * 0.35f;     /* v1^18 */
            put_pixel(rgb + ((size_t)y * n + x) * 3,
                      c[0] * lit + vein, c[1] * lit + vein * 0.95f, c[2] * lit + vein * 0.85f);
        }
    }
}

static void render_cells(ko_organic_t *gen, int variant, uint8_t *rgb)
{
    const int n = gen->size, g = gen->grid;
    const float to_grid = (float)(g - 1) / (float)n;
    const float inv_n = 1.0f / (float)n;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float gx = (float)x * to_grid, gy = (float)y * to_grid;
            const float px = ((float)x + 0.5f) * inv_n + sample_grid(gen->warp_x, g, gx, gy) * 0.07f;
            const float py = ((float)y + 0.5f) * inv_n + sample_grid(gen->warp_y, g, gx, gy) * 0.07f;
            const float b = sample_grid(gen->bg, g, gx, gy);
            /* Coloured wash instead of black: gaps read as deep jewel tones. */
            float col[3];
            if (variant == KO_ORGANIC_LAVA_CELLS) {   /* plum <-> ink teal */
                col[0] = 0.24f - 0.18f * b; col[1] = 0.05f + 0.12f * b; col[2] = 0.20f + 0.04f * b;
            } else {                                  /* moss <-> violet */
                col[0] = 0.05f + 0.14f * b; col[1] = 0.15f - 0.08f * b; col[2] = 0.08f + 0.16f * b;
            }
            float acc = 1e3f, blend[3] = {0}, wsum = 0.0f;
            for (int i = 0; i < CELL_COUNT; ++i) {
                const cell_t *c = &gen->cells[i];
                const float reach = c->r0 * c->rmax + CELL_MARGIN;
                if (fabsf(px - c->cx) > reach || fabsf(py - c->cy) > reach) {
                    continue;
                }
                float angle;
                const float d = cell_distance(c, px, py, &angle);
                if (variant == KO_ORGANIC_LAVA_CELLS) {
                    acc = smin(acc, d, 0.07f);
                    const float w = fast_expneg(gen, d * 40.0f);
                    blend[0] += c->rgb[0] * w;
                    blend[1] += c->rgb[1] * w;
                    blend[2] += c->rgb[2] * w;
                    wsum += w;
                } else {
                    const float inside = 1.0f - smoothstep(-0.007f, 0.007f, d);
                    const float rad = clamp01(-d * 8.0f);
                    const float vein = 0.86f + 0.14f * fast_sin(gen, angle * 26.0f + d * 90.0f);
                    const float rim = fast_expneg(gen, fabsf(d) * 120.0f) * 0.35f;
                    static const float deep[3] = {0.15f, 0.05f, 0.2f};
                    for (int ch = 0; ch < 3; ++ch) {
                        const float edge = c->rgb[ch] * 1.15f;
                        const float core = c->rgb[ch] * 0.5f + deep[ch];
                        const float pc = (edge + (core - edge) * rad) * vein;
                        col[ch] += (pc - col[ch]) * inside * 0.88f + c->rgb[ch] * rim;
                    }
                }
            }
            if (variant == KO_ORGANIC_LAVA_CELLS && wsum > 1e-4f) {
                const float inside = 1.0f - smoothstep(-0.007f, 0.007f, acc);
                const float ring = 0.74f + 0.26f * fast_sin(gen, acc * 80.0f
                                   + sample_grid(gen->ring, g, gx, gy) * 4.0f);
                const float lift = 0.75f + 0.4f * smoothstep(-0.14f, 0.0f, acc);
                const float glow = fast_expneg(gen, fabsf(acc) * 90.0f) * 0.6f;
                const float ink = 1.0f - 0.5f * fast_expneg(gen, fabsf(acc + 0.016f) * 140.0f) * inside;
                for (int ch = 0; ch < 3; ++ch) {
                    const float cc = blend[ch] / wsum;
                    const float ic = cc * ring * lift;
                    col[ch] = (col[ch] + (ic - col[ch]) * inside + cc * glow) * ink;
                }
            }
            put_pixel(rgb + ((size_t)y * n + x) * 3, col[0], col[1], col[2]);
        }
    }
}

ko_organic_t *ko_organic_create(int size)
{
    if (size < 64 || size > 512 || (size & (size - 1))) {
        return NULL;
    }
    ko_organic_t *gen = calloc(1, sizeof(*gen));
    if (!gen) {
        return NULL;
    }
    init_tables(gen);
    gen->size = size;
    gen->grid = size / GRID_DIV + 1;
    gen->mgrid = size / MARBLE_DIV + 1;
    const size_t cells = (size_t)gen->grid * gen->grid;
    const size_t mcells = (size_t)gen->mgrid * gen->mgrid;
    float **fields[] = {&gen->warp_x, &gen->warp_y, &gen->bg, &gen->ring,
                        &gen->mq_x, &gen->mq_y, &gen->mr};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        *fields[i] = calloc(i < 4 ? cells : mcells, sizeof(float));
        if (!*fields[i]) {
            ko_organic_destroy(gen);
            return NULL;
        }
    }
    return gen;
}

void ko_organic_destroy(ko_organic_t *gen)
{
    if (!gen) {
        return;
    }
    free(gen->warp_x);
    free(gen->warp_y);
    free(gen->bg);
    free(gen->ring);
    free(gen->mq_x);
    free(gen->mq_y);
    free(gen->mr);
    free(gen);
}

void ko_organic_render(ko_organic_t *gen, ko_organic_variant_t variant,
                       float t, uint8_t *rgb)
{
    if (!gen || !rgb || variant >= KO_ORGANIC_COUNT) {
        return;
    }
    update_grids(gen, (int)variant, t);
    if (variant == KO_ORGANIC_MARBLING) {
        render_marbling(gen, t, rgb);
        return;
    }
    update_cells(gen, (int)variant, t);
    render_cells(gen, (int)variant, rgb);
}

const char *ko_organic_name(ko_organic_variant_t variant)
{
    static const char *const names[KO_ORGANIC_COUNT] = {"Lava Cells", "Flora", "Marbling"};
    return variant < KO_ORGANIC_COUNT ? names[variant] : "?";
}
#endif /* KALEIDO_ENABLED */
