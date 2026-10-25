// The lab's fixed copy and control tables, kept apart from the components: per-face review
// notes (brief §5 and rounds 2–4), the six scorecard criteria, the tuning slider specs, and
// the number formats the readouts and C export use. Text is the artifact's, unchanged.

export const NOTES: [string, string][] = [
  ['Pupils dead centre, no lids: a blank stare. Seen most often.', 'lidTop .04, inset .08, circle pupils, idle life from §4. r2: pop 0 (a bounce on every return read as a hiccup); idle blinks 11–21/min, saccades every 2.8–6.6 s.'],
  ['Eyes just get bigger (52×50), reads as mild surprise. The smile is only in the mouth.', 'lidBot .35, bow .5 (r2, was .3: a real smiling crescent that survives mono), joy pupils, scale 1.03, pop 1.'],
  ['A shorter neutral with pupils down. The droop is only in the brows.', 'lidTop .22, slant −.45, lidBot .1, bow .3, scale .96, plead, gaze (0, +.2).'],
  ['Close to SHOCKED (55×55 vs 55×52). 150 ms lerp, no pop.', 'aspect 1.0, scale 1.1, lidTop 0, small → dot via the pupil spring, pop 1. r2: keeps dot; SHOCKED now gets the tall oval. r4: 170 ms shape spring (was 150), so DEVIOUS → SURPRISED stays inside the startle cap.'],
  ['EYE_HALF is a flat rectangle chop: black box in ambient mode, pupil pinned.', 'lidTop .38, sag .08, slant −.05, lidBot .06, circle pdy +.05, slow shape spring.'],
  ['A squashed oval (52×32) plus V brows. Brows hidden, it reads bored.', 'lidTop .3, slant +.45, lidBot .12, aspect .85, small, pop 0.5. r2: 230 ms shape spring, so SURPRISED → ANGRY stays under the 25 % step.'],
  ['Heart snaps in at 50 %. Brows hidden.', 'heart morph, size × (1 + .06·sin 7t) pulse, lidBot .25, bow .4. r4: heart preset .42 (was .36), the §7.3 mono minimum.'],
  ['Dotted spiral breaks apart on mono. Asymmetric brows only.', 'Stroke spiral overlay, swapped under a blink; scale [1.04, .94]. r2: pop 0 (it landed under the swap blink). r4: the spiral closes with the lids (scaled by the opening, gone below .3), so it no longer dashes the lid line. Scored on the overlay (A1).'],
  ['Pupils fixed up-right; one brow does all the work.', 'r2: a real one-eye squint: lidTop [.24, 0], lidBot [.1, 0], scale [.96, 1.06]; slant [−.1, .1], small, orbit gaze.'],
  ['Static star snaps in.', 'star morph, spin .6, scale 1.08, pop 1. r4: star preset .42 (was .40), the §7.3 mono minimum.'],
  ['"One eye narrowed" in the comment, but both eyes are identical.', 'lidTop [.32, .08], slant [.2, .05]. r2: side-eye gaze (+.32, 0), raised cheeks lidBot [.2, .12], bow .35: sly, not suspicious.'],
  ['Same as MISCHIEF: the narrowed eye is not possible today.', 'lidTop [.38, 0], slant [.15, −.05], scale [.95, 1.05], gaze (+.12, 0). r4: pop 0 (was .3): a raised-eyebrow appraisal does not bounce.'],
  ['52×50 with r11 pupils is nearly SURPRISED.', 'lidTop .12, slant −.35, small, pdy +.06, nervous darts at half rate.'],
  ['Asymmetric brows and a side look only.', 'lidTop [0, .3], scale [1.06, .88], odd pupils. r4: slant [0, −.2] (was +.25) and gaze (0, −.1): the narrowed eye tilts up toward the nose, puzzled, instead of mirroring SKEPTICAL.'],
  ['50×35 squash reads like a mild ANNOYED.', 'Beaming "^ ^": shut 1, cup 1, scale 1.04, pop 0.5.'],
  ['Small pupils down-left; the eye shape is unchanged.', 'lidBot .3, bow .4, scale [1, .97], shy gaze. r4: lidTop .16, a lowered lid over the cheek crescent (LOVE IoU .904 → .794).'],
  ['52×28 squash, no lid line.', 'lidTop .5, slant .03, aspect .88, peek with pdy +.02 (r2: a half-pupil under the lid, no notch), annoyed side-flick. r4: 350 ms shape spring (was 250) and an exit blink: a slow lid drop in, an "ugh" blink out.'],
  ['48×38 is barely different from NEUTRAL.', 'lidTop .2, slant .08, lidBot .1, small, fixation.'],
  ['Right eye is a full white ellipse with a black line through it: reads as split, not closed.', 'shut [0, 1], cup [0, 1] (r2: a cheeky "^"); left eye lidBot .2, bow .3; pop 0.5. The wink closes on the blink path: lids, then arc.'],
  ['Same flat chop as CHILL.', 'lidTop .36, slant .3, small, gaze (+.15, 0). r2: grin crescent lidBot .22, bow .45, pop 0 (scheming is slow).'],
  ['The dot is pinned at the centre; the 1 px glint is invisible.', 'r2: a tall, lidless stare: aspect 1.12, scale 1.12, lidTop 0. Dot with tremble, no blink for 1.5 s, pop 1. r4: 200 ms shape spring (was 100), so it opens over 3 frames inside the startle window; exit blink, so the stare ends with a blink. Brows that would leave the screen are not drawn.'],
  ['The eyes are neutral; the mouth does everything.', 'r4: shut 1, cup .2 (was .8): ‿ ‿, closed and savouring, no longer a flatter PROUD. pop 0.5. Mouth-led (A4): passes criterion 1 only if Kevin accepts A4.'],
  ['Brows only.', 'r2: tense and shrunk: slant −.3, lidBot .14, scale .94. lidTop .08, darting gaze, blink interval halved. r4: aspect 1.05 and pinprick pupils .16 (was small .18): wide and wired, not a smaller WORRIED.'],
  ['Paints black bars, which show as boxes in ambient mode.', 'Row-shift glitch overlay, eyes out of sync. r4: shifts .1–.2 R (was .05–.15) and the pupil rides the shifted rows, so the eye reads as sliced on mono. Scored on the overlay (A1).'],
  ['Neutral eyes plus a side look.', 'On entry: eyeroll fidget, then lidTop [.25, .15], gaze (+.2, −.05).'],
];

export const CRIT: [string, string, string][] = [
  ['Eyes carry it', 'Actual size, 60 cm, brows and mouth hidden: you name it from the 25 within 2 s. Mouth-led faces (A4, pending Kevin): KISSING passes if the eyes read as closed and content and are distinct from PROUD; the mouth names it.', 'c1'],
  ['Silhouette distinct', 'Pupils off. IoU below 0.90 for the named pairs and P1 vs P1. IoU 0.90 or more elsewhere is an eyeball flag, not a fail: it passes if the lid corners differ by ≥ 2 OLED px or the face is asymmetric. Overlay faces (DIZZY, GLITCHING) are scored on the overlay, not the rest silhouette (A1): the glyph is legible at actual size and on mono (stroke or shift ≥ 2 OLED px), is swapped under a blink, and closes with the lids.', 'c2'],
  ['Survives 1-bit', '128×64 at ×0.45: features ≥ 2 px, lid slant shows ≥ 2 px corner difference. Overlay faces: stroke or shift ≥ 2 px (A1).', 'c3'],
  ["Transitions don't pop", "No eye area step over 25 % per frame. Blinks and shut changes are excepted for the eye that closes; the open eye still counts. Startle entry (SURPRISED, SHOCKED): opening steps up to 50 % in the first 100 ms. A face may end with an exit blink (SHOCKED, ANNOYED); it counts as a blink, even inside the next face's no-blink window (A2). Swaps under a blink. Pupils inside. Never merge.", 'c4'],
  ['Stays in palette', 'Only background and botFaceColor, hard edges, no black boxes in ambient, in all 4 colours.', 'c5'],
  ['Calm but alive', '60 s idle NEUTRAL: 10–25 gaze moves, 9–23 blinks, no still gap over 7 s, ≤ 1 fidget per 8 s.', 'c6'],
];

/** [group, key, min, max, step, per-eye, note] */
export type PSpecRow = [string, string, number, number, number, 0 | 1, string];
export const PSPEC: PSpecRow[] = [
  ['Silhouette', 'aspect', .55, 1.15, .01, 1, 'half-height ÷ half-width'],
  ['Silhouette', 'scale', .85, 1.12, .01, 1, 'per-eye size, ×'],
  ['Top lid', 'lidTop', 0, 1, .01, 1, 'coverage from top, ×H'],
  ['Top lid', 'slant', -.6, .5, .01, 1, '+ inner corner down, ×H/2'],
  ['Top lid', 'sag', 0, .3, .01, 1, 'middle droop, ×H'],
  ['Bottom lid', 'lidBot', 0, .6, .01, 1, 'coverage from bottom, ×H'],
  ['Bottom lid', 'bow', 0, .5, .01, 1, 'arches up in the middle'],
  ['Shut arc', 'shut', 0, 1, .01, 1, 'morph to an arc'],
  ['Shut arc', 'cup', 0, 1, .01, 1, '0 = ‿ asleep, 1 = ^ beaming'],
  ['Pupil', 'pw', .06, .42, .01, 1, 'half-width'],
  ['Pupil', 'ph', .06, .42, .01, 1, 'half-height'],
  ['Pupil', 'inset', 0, .2, .01, 1, 'toward the nose'],
  ['Pupil', 'pdy', -.2, .2, .01, 1, 'vertical bias, + down'],
  ['Pupil', 'heart', 0, 1, .01, 1, 'heart-ness'],
  ['Pupil', 'star', 0, 1, .01, 1, 'star-ness, heart + star ≤ 1'],
  ['Gaze and motion', 'gazeX', -.35, .35, .01, 0, 'authored look, + right'],
  ['Gaze and motion', 'gazeY', -.25, .25, .01, 0, 'authored look, + down'],
  ['Gaze and motion', 'pop', 0, 1, .05, 0, 'mood pop on entry'],
  ['Gaze and motion', 'transitionMs', 100, 600, 10, 0, 'ms; shape f = 0.75 / T'],
];
/** [key, min, max, step, note] */
export const GSPEC: [ "R" | "gap" | "arcW" | "spin", number, number, number, string][] = [
  ['R', 40, 56, 1, 'eye half-width, px at 240 wide'], ['gap', 0, .3, .01, 'min gap between eyes, ×R'],
  ['arcW', .12, .2, .005, 'arc thickness when shut, ×R'], ['spin', 0, 1.2, .05, 'star rotation, rad/s'],
];

export const fmt = (key: string, v: number) => key === 'transitionMs' ? String(Math.round(v)) : key === 'R' ? String(Math.round(v)) : (Math.abs(v) < .0005 ? 0 : v).toFixed(key === 'arcW' ? 3 : 2);
export const cf = (v: number) => { const s = (Math.abs(v) < .0005 ? 0 : v).toFixed(3).replace(/0+$/, '').replace(/\.$/, '.0'); return s + 'f'; };
