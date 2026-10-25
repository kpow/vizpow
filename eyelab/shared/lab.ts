// One simulated device frame for both sides, without any DOM: build a Lab from the face
// files, step the before (firmware) and after (model) state, and draw each into an RGB565
// Frame. The client blits these frames; the phase 2 parity check will diff them against
// the firmware's host build.

import type { Face, Globals } from "./faces";
import { clone } from "./faces";
import { EM, drawMouth, drawZzz, exprTable, renderBotFace, scaledFace } from "./before";
import type { BeforeOpts } from "./before";
import { Frame, Gfx, rnd } from "./raster";
import type { GfxMap } from "./raster";
import { drawAmbient, renderAfter } from "./model";
import type { AfterMetrics } from "./model";
import { afterSetExpr } from "./motion";
import { createState, dev, padEngaged } from "./state";
import type { Device, Lab } from "./state";

const MONO_MAP: GfxMap = { mono: true, s: .45, ox: 64, oy: 30, dcx: 120, dcy: 118 };

/** A Lab at simulated time 0 showing face 0, as the artifact started. */
export function createLab(faces: Face[], globals: Globals): Lab {
  const L = createState(exprTable(faces), faces.map((f) => clone(f.eye)), { ...globals });
  const BF = L.BF;
  BF.face.init(); BF.blink.init(0); BF.look.init(0); BF.imu.init();
  afterSetExpr(L, 0, 0, true);
  const AS = L.AS;
  AS.blink.next = 1200; AS.gaze.next = 900; AS.idle.nextFid = rnd(8000, 16000); AS.idle.nextLook = rnd(9000, 22000);
  AS.glitch[1].next = 60;
  return L;
}

export function stepBefore(L: Lab, now: number) {
  const f = L.BF.face, BF = L.BF, ui = L.ui;
  L.tweens.update(now);
  if (BF.wasSleep && !ui.sleep) f.load(f.targetExpr);
  BF.wasSleep = ui.sleep;
  const em = f.eyeMode;
  const canBlink = em === EM.NORMAL || em === EM.DIAMOND || em === EM.HALF || em === EM.DOT || em === EM.WINK;
  if (!ui.sleep && canBlink) {
    if (ui.idle) f.blinkAmount = BF.blink.update(now);
    else { f.blinkAmount = 0; BF.blink.init(now); }
  } else if (ui.sleep) { f.blinkAmount = 0; f.eyeMode = EM.CLOSED; }
  // (non-blinkable modes leave blinkAmount as it was, as the firmware does)
  let lx = 0, ly = 0;
  if (!ui.sleep) {
    if (padEngaged(L)) [lx, ly] = BF.imu.update(L.pad.x, L.pad.y);
    else if (ui.idle) { [lx, ly] = BF.look.update(now); BF.imu.init(); }
  }
  f.dynamicPupilX = lx; f.dynamicPupilY = ly;
  f.update();
}

function bgFill(L: Lab, fr: Frame, D: Device, now: number) { if (L.ui.ambient && !D.mono) drawAmbient(fr, now); else fr.clear(0); }

/** Draw the before face into fr. Returns the colours it drew with (scorecard criterion 5). */
export function renderBeforeInto(L: Lab, fr: Frame, now: number): Set<number> {
  const D = dev(L), ui = L.ui, fc = D.mono ? 0xFFFF : ui.fc;
  bgFill(L, fr, D, now);
  const o: BeforeOpts = { stroke: ui.ambient && !D.mono, fc, now, brows: ui.brows, mouth: ui.mouth, pupils: ui.pupils };
  let g: Gfx;
  if (D.mono) { g = new Gfx(fr, MONO_MAP); renderBotFace(g, L.BF.face, 0, 120, 118, o); if (ui.sleep) drawZzz(g, 120, 118, now, fc); }
  else if (D.chip) { g = new Gfx(fr); renderBotFace(g, scaledFace(L.BF.face, .25), 0, D.cx, D.cy, o); }
  else { g = new Gfx(fr); renderBotFace(g, L.BF.face, 0, D.cx, D.cy, o); if (ui.sleep) drawZzz(g, D.cx, D.cy, now, fc); }
  return g.cols;
}

/** Draw the after face into fr. Returns the scorecard metrics and the colours it drew with. */
export function renderAfterInto(L: Lab, fr: Frame, now: number): { m: AfterMetrics; cols: Set<number> } {
  const D = dev(L), ui = L.ui, fc = D.mono ? 0xFFFF : ui.fc, BFf = L.BF.face;
  bgFill(L, fr, D, now);
  let mg: Gfx | null = null;
  const mouthFn = () => {
    const stroke = ui.ambient && !D.mono;
    if (D.mono) { mg = new Gfx(fr, MONO_MAP); drawMouth(mg, BFf, 120, 118 + BFf.mouthOffsetY, false, fc); }
    else if (D.chip) { mg = new Gfx(fr); const f = scaledFace(BFf, .25); drawMouth(mg, f, D.cx, D.cy + f.mouthOffsetY, stroke, fc); }
    else { mg = new Gfx(fr); drawMouth(mg, BFf, D.cx, D.cy + BFf.mouthOffsetY, stroke, fc); }
  };
  const m = renderAfter(L, fr, D, now, fc, mouthFn);
  const cols = new Set([fc, 0]);
  const used = mg as Gfx | null;
  if (used) for (const c of used.cols) cols.add(c);
  return { m, cols };
}

export { Frame };
export type { Lab, Device };
