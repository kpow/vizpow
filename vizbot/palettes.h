#ifndef PALETTES_H
#define PALETTES_H

#include <FastLED.h>
#include "fx_palettes.h"

// ============================================================================
// The Noodle/viz family palette set — the same 23 palettes, same ids, as the
// Noodle 2K Console, vizSpot and vizMac.
//
// fx_palettes.h is GENERATED in noodlez-v2 (palettes/palettes.json -> gen.py)
// and copied here by noodlez-v2/palettes/sync.py. Never hand-edit it: edit in
// the Palette Lab (noodlez-v2/palettes/lab) and re-sync. FX_PALETTE_SET_CRC in
// that file says which set this tree is on.
//
// The table is plain 16-entry RGB data, so it is lifted into CRGBPalette16 once
// at static-init time and every existing ColorFromPalette() call keeps working.
// One difference from the Console's own lookup: ColorFromPalette always wraps
// entry 15 -> 0, where the Console clamps palettes whose FX_PALETTE_CYCLIC is 0.
// ============================================================================

CRGBPalette16 palettes[FX_NUM_PALETTES];

static struct PaletteTableInit {
  PaletteTableInit() {
    for (uint8_t p = 0; p < FX_NUM_PALETTES; p++)
      for (uint8_t e = 0; e < FX_PAL_ENTRIES; e++)
        palettes[p][e] = CRGB(FX_PALETTES[p][e][0], FX_PALETTES[p][e][1], FX_PALETTES[p][e][2]);
  }
} paletteTableInit;   // defined after palettes[], so it runs after it is constructed

#endif
