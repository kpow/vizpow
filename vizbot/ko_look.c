// SPDX-License-Identifier: Apache-2.0
/* vizbot copy of esp-mosaico projects/kaleido/components/kaleido_core (commit a7438ac).
 * Port changes are marked PORT. Built only with -DKALEIDO_ENABLED. */
#ifdef KALEIDO_ENABLED
#pragma GCC optimize("O2")
#include "ko_look.h"

#include <math.h>
#include <stdio.h>

#define KO_PI 3.14159265358979f

/* mode, segments, scale, zoom, spin, hue, drift, saturation, contrast, invert, speed, pace.
 * pace: measured so every look shows the same on-screen motion at speed 1x
 * (mean luma change per 0.1 s = 6.0 over the three organic sources). */
static const ko_look_t presets[KO_LOOK_COUNT] = {
    {KO_MODE_RADIAL, 8, 2.4f, 1.3f, 0.12f, 0.0f, 0.0f, 1.2f, 1.06f, false, 1.0f, 0.57f},
    {KO_MODE_TRIANGLE, 6, 2.6f, 1.15f, 0.08f, 0.0f, 0.0f, 1.12f, 1.06f, false, 1.0f, 0.95f},
    {KO_MODE_SNOWFLAKE, 6, 1.0f, 1.1f, 0.06f, 0.0f, 0.0f, 1.15f, 1.08f, false, 1.0f, 0.88f},
    {KO_MODE_RADIAL, 12, 2.4f, 0.95f, -0.10f, -0.45f, 0.0f, 1.35f, 1.2f, false, 1.0f, 0.75f},
    {KO_MODE_RADIAL, 6, 2.4f, 1.6f, 0.07f, 3.3f, 0.0f, 0.9f, 1.06f, false, 1.0f, 0.76f},
    {KO_MODE_RADIAL, 16, 2.4f, 1.9f, 0.10f, 0.0f, 0.3f, 1.12f, 1.06f, false, 1.0f, 0.74f},
    {KO_MODE_TRIANGLE, 6, 3.3f, 1.2f, -0.06f, 2.2f, 0.12f, 1.12f, 1.06f, false, 1.0f, 1.02f},
    {KO_MODE_TUNNEL, 10, 2.2f, 1.0f, 0.08f, 0.0f, 0.0f, 1.25f, 1.08f, false, 1.0f, 0.49f},
    /* Drum looks from Cymatics: Mirrors = fold order, Hue drift 0.175 rad/s and
     * pace 1/SPEED_SCALE, so Speed 1x matches Cymatics' tempo and 10 deg/s drift. */
    {KO_MODE_DRUM_CATHEDRAL, 12, 2.4f, 1.0f, 0.0f, 0.0f, 0.175f, 1.0f, 1.0f, false, 1.0f, 1.333f},
    {KO_MODE_DRUM_ACID, 6, 2.4f, 1.0f, 0.0f, 0.0f, 0.175f, 1.0f, 1.0f, false, 1.0f, 1.333f},
};

static const char *const preset_names[KO_LOOK_COUNT] = {
    "Rose Window", "Prism", "Snowflake", "Ember", "Ice", "Bloom", "Tidepool", "Tunnel",
    "Cathedral", "Acid Bloom",
};

const ko_look_t *ko_look_preset(int index)
{
    return index >= 0 && index < KO_LOOK_COUNT ? &presets[index] : &presets[0];
}

const char *ko_look_name(int index)
{
    return index >= 0 && index < KO_LOOK_COUNT ? preset_names[index] : "Custom";
}

static float rnd(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return (float)(*seed >> 8) * (1.0f / 16777216.0f);
}

void ko_look_random(ko_look_t *look, uint32_t *seed)
{
    look->mode = rnd(seed) < 0.5f ? KO_MODE_RADIAL : (ko_mode_t)(1 + (int)(rnd(seed) * (KO_MODE_MIRROR_COUNT - 1)));
    look->segments = 4 + (int)(rnd(seed) * 12.0f);
    look->scale = 2.0f + rnd(seed) * 2.0f;
    look->zoom = 0.8f + rnd(seed) * 1.4f;
    look->spin = (rnd(seed) - 0.5f) * 0.3f;
    look->hue = rnd(seed) * 2.0f * KO_PI;
    look->hue_drift = rnd(seed) < 0.35f ? rnd(seed) * 0.3f : 0.0f;
    look->saturation = 0.9f + rnd(seed) * 0.6f;
    look->contrast = 0.95f + rnd(seed) * 0.4f;
    look->invert = rnd(seed) < 0.08f;
    look->speed = 1.0f;
    look->pace = 0.75f;
}

const char *ko_param_name(ko_param_t param)
{
    static const char *const names[KO_PARAM_COUNT] = {
        "Mirrors", "Symmetry", "Zoom", "Spin", "Speed", "Hue", "Hue drift", "Vivid", "Contrast", "Invert",
    };
    return param < KO_PARAM_COUNT ? names[param] : "?";
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void ko_param_step(ko_look_t *look, ko_param_t param, int direction)
{
    const float d = (float)direction;
    switch (param) {
    case KO_PARAM_MIRRORS:
        look->segments = (int)clampf((float)(look->segments + direction), 2.0f, 24.0f);
        break;
    case KO_PARAM_SYMMETRY: {
        /* Cycle within the family: a mirror look never turns into a drum. */
        const int first = ko_mode_is_drum(look->mode) ? KO_MODE_DRUM_CATHEDRAL : 0;
        const int count = ko_mode_is_drum(look->mode) ? KO_MODE_COUNT - KO_MODE_DRUM_CATHEDRAL
                          : KO_MODE_MIRROR_COUNT;
        const int i = ((int)look->mode - first + direction % count + count) % count;
        look->mode = (ko_mode_t)(first + i);
        break;
    }
    case KO_PARAM_ZOOM:
        look->zoom = clampf(look->zoom * (direction > 0 ? 1.12f : 1.0f / 1.12f), 0.4f, 4.0f);
        break;
    case KO_PARAM_SPIN:
        look->spin = clampf(look->spin + d * 0.03f, -0.6f, 0.6f);
        break;
    case KO_PARAM_SPEED:
        look->speed = clampf(look->speed * (direction > 0 ? 1.25f : 1.0f / 1.25f), 0.1f, 3.0f);
        break;
    case KO_PARAM_HUE:
        look->hue = fmodf(look->hue + d * (KO_PI / 12.0f) + 2.0f * KO_PI, 2.0f * KO_PI);
        break;
    case KO_PARAM_DRIFT:
        look->hue_drift = clampf(look->hue_drift + d * 0.05f, 0.0f, 1.0f);
        break;
    case KO_PARAM_VIVID:
        look->saturation = clampf(look->saturation + d * 0.1f, 0.0f, 2.0f);
        break;
    case KO_PARAM_CONTRAST:
        look->contrast = clampf(look->contrast + d * 0.1f, 0.5f, 2.0f);
        break;
    case KO_PARAM_INVERT:
        look->invert = !look->invert;
        break;
    default:
        break;
    }
}

void ko_param_format(const ko_look_t *look, ko_param_t param, char *out, int capacity)
{
    static const char *const modes[KO_MODE_COUNT] = {
        "Radial", "Triangle", "Square", "Snowflake", "Tunnel", "Cathedral", "Acid",  /* Fits the 200 px rail. */
    };
    switch (param) {
    case KO_PARAM_MIRRORS:
        snprintf(out, (size_t)capacity, "%d", look->segments);
        break;
    case KO_PARAM_SYMMETRY:
        snprintf(out, (size_t)capacity, "%s", look->mode < KO_MODE_COUNT ? modes[look->mode] : "?");
        break;
    case KO_PARAM_ZOOM:
        snprintf(out, (size_t)capacity, "%.1fx", (double)look->zoom);
        break;
    case KO_PARAM_SPIN:
        snprintf(out, (size_t)capacity, "%+.0f%%", (double)(look->spin / 0.6f * 100.0f));
        break;
    case KO_PARAM_SPEED:
        snprintf(out, (size_t)capacity, "%.1fx", (double)look->speed);
        break;
    case KO_PARAM_HUE:
        snprintf(out, (size_t)capacity, "%.0f\xC2\xB0", (double)(look->hue * 180.0f / KO_PI));
        break;
    case KO_PARAM_DRIFT:
        snprintf(out, (size_t)capacity, "%.0f%%", (double)(look->hue_drift * 100.0f));
        break;
    case KO_PARAM_VIVID:
        snprintf(out, (size_t)capacity, "%.0f%%", (double)(look->saturation * 100.0f));
        break;
    case KO_PARAM_CONTRAST:
        snprintf(out, (size_t)capacity, "%.0f%%", (double)(look->contrast * 100.0f));
        break;
    case KO_PARAM_INVERT:
        snprintf(out, (size_t)capacity, "%s", look->invert ? "On" : "Off");
        break;
    default:
        snprintf(out, (size_t)capacity, "-");
        break;
    }
}

/* Contrast that mostly lifts highlights: shadows get a quarter of the
 * stretch, so dark backgrounds never crush to black (a black screen read as
 * "broken" on the device). */
static float shadow_safe_contrast(float v, float contrast)
{
    const float k = v < 0.5f ? 1.0f + (contrast - 1.0f) * 0.25f : contrast;
    return (v - 0.5f) * k + 0.5f;
}

void ko_grade(const uint8_t *rgb, uint16_t *out565, int size,
              float hue, float saturation, float contrast, bool invert)
{
    /* Hue rotation about the grey axis (Rodrigues), as in the design shader. */
    const float c = cosf(hue), s = sinf(hue), k = 1.0f / 3.0f, r3 = 0.57735027f;
    const float m00 = c + (1.0f - c) * k, m01 = (1.0f - c) * k - r3 * s, m02 = (1.0f - c) * k + r3 * s;
    const float m10 = (1.0f - c) * k + r3 * s, m11 = c + (1.0f - c) * k, m12 = (1.0f - c) * k - r3 * s;
    const float m20 = (1.0f - c) * k - r3 * s, m21 = (1.0f - c) * k + r3 * s, m22 = c + (1.0f - c) * k;
    const size_t count = (size_t)size * (size_t)size;
    for (size_t i = 0; i < count; ++i) {
        const float r0 = rgb[i * 3] * (1.0f / 255.0f);
        const float g0 = rgb[i * 3 + 1] * (1.0f / 255.0f);
        const float b0 = rgb[i * 3 + 2] * (1.0f / 255.0f);
        float r = m00 * r0 + m01 * g0 + m02 * b0;
        float g = m10 * r0 + m11 * g0 + m12 * b0;
        float b = m20 * r0 + m21 * g0 + m22 * b0;
        const float l = 0.299f * r + 0.587f * g + 0.114f * b;
        r = shadow_safe_contrast(l + (r - l) * saturation, contrast);
        g = shadow_safe_contrast(l + (g - l) * saturation, contrast);
        b = shadow_safe_contrast(l + (b - l) * saturation, contrast);
        if (invert) {
            r = 1.0f - r;
            g = 1.0f - g;
            b = 1.0f - b;
        }
        const uint16_t R = (uint16_t)(clampf(r, 0.0f, 1.0f) * 31.0f + 0.5f);
        const uint16_t G = (uint16_t)(clampf(g, 0.0f, 1.0f) * 63.0f + 0.5f);
        const uint16_t B = (uint16_t)(clampf(b, 0.0f, 1.0f) * 31.0f + 0.5f);
        out565[i] = (uint16_t)((R << 11) | (G << 5) | B);
    }
}
#endif /* KALEIDO_ENABLED */
