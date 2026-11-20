// SPDX-License-Identifier: Apache-2.0
#pragma once

/* Generative organic sources (the approved "Lava Cells", "Flora" and
 * "Marbling" built-ins). Shapes morph continuously between blob, 3-lobe,
 * petal and bean outlines; colour is flat with rings and glowing edges,
 * never 3D-shaded. Output is a square RGB888 texture. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KO_ORGANIC_LAVA_CELLS = 0,
    KO_ORGANIC_FLORA,
    KO_ORGANIC_MARBLING,
    KO_ORGANIC_COUNT,
} ko_organic_variant_t;

typedef struct ko_organic ko_organic_t;

/** size must be a power of two between 64 and 512. NULL on failure. */
ko_organic_t *ko_organic_create(int size);
void ko_organic_destroy(ko_organic_t *gen);

/** Render the variant at time t (seconds) into rgb (size*size*3 bytes). */
void ko_organic_render(ko_organic_t *gen, ko_organic_variant_t variant,
                       float t, uint8_t *rgb);

const char *ko_organic_name(ko_organic_variant_t variant);

#ifdef __cplusplus
}
#endif
