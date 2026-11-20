// SPDX-License-Identifier: Apache-2.0
#pragma once

/* vizbot settings for the Kaleido engine (ko_engine.h includes this when it
 * is on the include path). Only used with -DKALEIDO_ENABLED. */

#include <esp_heap_caps.h>

/* 128 px source texture, as on the ESP-Mosaico device build. */
#define KO_SRC_SHIFT 7

/* Output stays plain RGB565 (KO_OUT default): kaleido_scene.h swaps bytes for
 * the LovyanGFX canvas while it enlarges the picture. */

/* The fold table (4 bytes per pixel) goes in PSRAM: read front to back, it
 * costs no measurable time there and leaves internal RAM alone. */
#define KO_FOLD_ALLOC(bytes) heap_caps_malloc((bytes), MALLOC_CAP_SPIRAM)
#define KO_FOLD_FREE(ptr) heap_caps_free(ptr)
