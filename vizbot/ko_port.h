// SPDX-License-Identifier: Apache-2.0
#pragma once

/* vizbot settings for the Kaleido engine (ko_engine.c and ko_organic.c include
 * this when it is on the include path). Only used with -DKALEIDO_ENABLED. */

#include <stddef.h>

/* 128 px source texture, as on the ESP-Mosaico device build. */
#define KO_SRC_SHIFT 7

/* Output stays plain RGB565 (KO_OUT default): kaleido_scene.h swaps bytes for
 * the LovyanGFX canvas while it enlarges the picture. */

/* Everything the engine allocates goes through the memory owner (mem.h) as
 * MEM_KALEIDO, in PSRAM only: none of it may take internal RAM. The fold table
 * (4 bytes per pixel) is read front to back, so PSRAM costs no measurable time.
 * kaleido_scene.h defines these two (mem.h is C++). */
#ifdef __cplusplus
extern "C" {
#endif
void *ks_mem_calloc(size_t n, size_t size);
void ks_mem_free(void *ptr);
#ifdef __cplusplus
}
#endif

#define KO_FOLD_ALLOC(bytes) ks_mem_calloc(1, (bytes))
#define KO_FOLD_FREE(ptr) ks_mem_free(ptr)
#define KO_CALLOC(n, size) ks_mem_calloc((n), (size))
#define KO_FREE(ptr) ks_mem_free(ptr)
