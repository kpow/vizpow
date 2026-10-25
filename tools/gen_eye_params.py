#!/usr/bin/env python3
"""Generate vizbot/bot_eye_params.h from an eye lab export.

The eye lab (eyelab/, docs/eye-art-direction.md, docs/eye-art-review-r4.md) is
where the eye parameters are designed and tuned. The input is either the lab's
face files (a directory: eyelab/faces/NN-name.json plus globals.json), its
"Copy JSON" export, or the frozen snapshot in tools/eye_params_r4.json.

    python3 tools/gen_eye_params.py eyelab/faces > vizbot/bot_eye_params.h

Until a face is edited, eyelab/faces and tools/eye_params_r4.json produce the
same header byte for byte.
"""
import json
import os
import re
import sys

# The header's Source and Regenerate lines when it is built from eyelab/faces. They still
# name the round 4 snapshot so that, before any face is edited, the regenerated header is
# byte-identical to the committed one. Change both together once the faces diverge.
FACES_SOURCE = 'vizBot eye lab round 4 baseline + Kevin saved tweaks, frozen 2026-10-24'
REGEN = 'python3 tools/gen_eye_params.py tools/eye_params_r4.json > vizbot/bot_eye_params.h'

PAIR_KEYS = ['aspect', 'scale', 'lidTop', 'slant', 'sag', 'lidBot', 'bow', 'shut', 'cup',
             'inset', 'pdy', 'pw', 'ph', 'heart', 'star']
OVERLAY = {'none': 'EYE_OVERLAY_NONE', 'x': 'EYE_OVERLAY_X', 'spiral': 'EYE_OVERLAY_SPIRAL',
           'glitch': 'EYE_OVERLAY_GLITCH'}
GAZE = ['idle', 'thinking', 'nervous', 'worried', 'shy', 'focused', 'annoyed', 'shocked']
ENTRY = {'': 'EYE_ENTRY_NONE', 'small': 'EYE_ENTRY_SMALL', 'eyeroll': 'EYE_ENTRY_EYEROLL'}


def f(v):
    s = ('%.3f' % (0.0 if abs(v) < 0.0005 else v)).rstrip('0')
    return (s + '0' if s.endswith('.') else s) + 'f'


def pair(v):
    return '{%s, %s}' % (f(v[0]), f(v[1]))


def load_faces(folder):
    """eyelab/faces as the export's shape: one entry per face file, eye params flattened."""
    exprs = []
    for name in sorted(os.listdir(folder)):
        if not re.match(r'^\d{2}-[a-z0-9-]+\.json$', name):
            continue
        face = json.load(open(os.path.join(folder, name)))
        exprs.append(dict(index=face['index'], name=face['name'], kevinTuned=face['kevinTuned'], **face['eye']))
    globals_ = json.load(open(os.path.join(folder, 'globals.json')))
    return {'source': FACES_SOURCE, 'globals': globals_, 'expressions': exprs}


def main(path):
    data = load_faces(path) if os.path.isdir(path) else json.load(open(path))
    exprs = sorted(data['expressions'], key=lambda e: e['index'])
    if [e['index'] for e in exprs] != list(range(25)):
        sys.exit('expected expressions 0..24 in firmware order')
    g = data['globals']
    out = []
    w = out.append
    w('#ifndef BOT_EYE_PARAMS_H')
    w('#define BOT_EYE_PARAMS_H')
    w('')
    w('// ============================================================================')
    w('// Bot Eye Parameters (generated: do not edit by hand)')
    w('// ============================================================================')
    w('// Source: %s' % data.get('source', path))
    w('// Regenerate: %s' % REGEN)
    w('//')
    w('// One parametric eye per side, drawn as one vertical span per pixel column.')
    w('// Sizes are x R (eye half-width). [0] = screen-left eye, [1] = screen-right.')
    w('// Row order matches the EXPR_* indices in bot_faces.h; vizbrain and /expr rely on it.')
    w('// ============================================================================')
    w('')
    w('#include <Arduino.h>')
    w('')
    w('enum BotEyeOverlay : uint8_t { EYE_OVERLAY_NONE = 0, EYE_OVERLAY_X, EYE_OVERLAY_SPIRAL, EYE_OVERLAY_GLITCH };')
    w('enum BotGazePattern : uint8_t {')
    w('  ' + ', '.join('GAZE_%s%s' % (n.upper(), ' = 0' if i == 0 else '') for i, n in enumerate(GAZE)))
    w('};')
    w('enum BotEyeEntry : uint8_t { EYE_ENTRY_NONE = 0, EYE_ENTRY_SMALL, EYE_ENTRY_EYEROLL };')
    w('')
    w('struct BotEyeParams {')
    w('  float aspect[2];   // half-height / half-width')
    w('  float scale[2];    // per-eye size')
    w('  float lidTop[2];   // top lid coverage, x eye height')
    w('  float slant[2];    // + inner corner down, x half-height')
    w('  float sag[2];      // top lid middle droop, x eye height')
    w('  float lidBot[2];   // bottom lid coverage, x eye height')
    w('  float bow[2];      // bottom lid arches up in the middle')
    w('  float shut[2];     // morph to the closed arc')
    w('  float cup[2];      // 0 = asleep, 1 = beaming ^')
    w('  float inset[2];    // pupil toward the nose, x R')
    w('  float pdy[2];      // pupil vertical bias, + down, x R')
    w('  float pw[2];       // pupil half-width, x R')
    w('  float ph[2];       // pupil half-height, x R')
    w('  float heart[2];    // pupil heart-ness')
    w('  float star[2];     // pupil star-ness (heart + star <= 1)')
    w('  float gazeX;       // authored look, + right, x R')
    w('  float gazeY;       // authored look, + down, x R')
    w('  float pop;         // mood pop on entry, 0..1')
    w('  uint16_t transitionMs;  // shape spring settles in about this long')
    w('  BotEyeOverlay overlay;')
    w('  BotGazePattern gaze;')
    w('  BotEyeEntry entry;')
    w('  bool pulse;        // heart pulse')
    w('  bool exitBlink;    // leaving this face starts a blink')
    w('  uint16_t noBlinkMs;  // no auto blink for this long after entry')
    w('  float blinkRate;   // auto blink rate multiplier')
    w('};')
    w('')
    w('// Globals')
    w('#define BOT_EYE_R        %s  // eye half-width in px at 240 wide' % f(g['R']))
    w('#define BOT_EYE_GAP      %s  // min gap between the eyes, x R' % f(g['gap']))
    w('#define BOT_EYE_ARC_W    %s  // closed-arc thickness, x R' % f(g['arcW']))
    w('#define BOT_EYE_STAR_SPIN %s // star pupil rotation, rad/s' % f(g['spin']))
    w('')
    w('const BotEyeParams botEyeParams[25] PROGMEM = {')
    for e in exprs:
        tag = ' (Kevin-tuned)' if e.get('kevinTuned') else ''
        w('  // %d: %s%s' % (e['index'], e['name'], tag))
        missing = [k for k in PAIR_KEYS if len(e.get(k, [])) != 2]
        if missing:
            sys.exit('%s: missing per-eye values for %s' % (e['name'], ', '.join(missing)))
        w('  {')
        w('    .aspect = %s, .scale = %s,' % (pair(e['aspect']), pair(e['scale'])))
        w('    .lidTop = %s, .slant = %s, .sag = %s,' % (pair(e['lidTop']), pair(e['slant']), pair(e['sag'])))
        w('    .lidBot = %s, .bow = %s,' % (pair(e['lidBot']), pair(e['bow'])))
        w('    .shut = %s, .cup = %s,' % (pair(e['shut']), pair(e['cup'])))
        w('    .inset = %s, .pdy = %s,' % (pair(e['inset']), pair(e['pdy'])))
        w('    .pw = %s, .ph = %s,' % (pair(e['pw']), pair(e['ph'])))
        w('    .heart = %s, .star = %s,' % (pair(e['heart']), pair(e['star'])))
        w('    .gazeX = %s, .gazeY = %s, .pop = %s, .transitionMs = %d,' % (
            f(e['gazeX']), f(e['gazeY']), f(e['pop']), round(e['transitionMs'])))
        w('    .overlay = %s, .gaze = GAZE_%s, .entry = %s,' % (
            OVERLAY[e['overlay']], e['gaze'].upper(), ENTRY[e.get('entry', '')]))
        w('    .pulse = %s, .exitBlink = %s, .noBlinkMs = %d, .blinkRate = %s' % (
            'true' if e.get('pulse') else 'false', 'true' if e.get('exitBlink') else 'false',
            e.get('noBlinkMs', 0), f(e.get('blinkRate', 1))))
        w('  },')
    w('};')
    w('')
    w('#endif // BOT_EYE_PARAMS_H')
    print('\n'.join(out))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'tools/eye_params_r4.json')
