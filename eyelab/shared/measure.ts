// Scorecard measurements (brief §6 as amended in rounds 2–4): silhouette IoU between faces,
// the lid-corner check on the 1-bit OLED, the per-frame area-step / gap / pupil monitor,
// and the overlay (A1) stroke widths. Ported from the artifact lab unchanged.

import { SHAPE_KEYS } from "./faces";
import { clamp, near } from "./raster";
import { DEVICES, dev } from "./state";
import type { Lab, Profile } from "./state";
import { eyeGeom, rasterEye } from "./model";
import type { AfterMetrics } from "./model";

export const P1 = [0, 1, 3, 2, 5, 4, 10, 18];
export const IS_P1 = new Set(P1);

// Static silhouette for IoU (rest params, no blink, no pop, pupils off)
function silhouette(L: Lab, i: number) {
  const W = 240, H = 280, m = new Uint8Array(W * H), P = L.AP[i];
  for (let e = 0; e < 2; e++) {
    const g = eyeGeom(L, e, 1, 120, 118, 0, { rest: P as unknown as Record<string, unknown> }); g.lt = g.ltRaw; g.lb = clamp(P.lidBot[e], 0, 1);
    rasterEye(m, W, H, g);
  }
  return m;
}
function iou(a: Uint8Array, b: Uint8Array) { let I = 0, U = 0; for (let p = 0; p < a.length; p++) { const x = a[p], y = b[p]; if (x & y) I++; if (x | y) U++; } return U ? I / U : 1; }
// Column top/bottom of a silhouette (240 × 280), -1 where the column is empty.
function profile(m: Uint8Array): Profile {
  const W = 240, H = 280, t = new Int16Array(W).fill(-1), b = new Int16Array(W).fill(-1);
  for (let x = 0; x < W; x++) for (let y = 0; y < H; y++) if (m[y * W + x]) { if (t[x] < 0) t[x] = y; b[x] = y; }
  return { t, b };
}
// Largest lid-line difference in the inner and outer quarter of each eye of face i, in vizMini OLED px (×0.45).
function cornerDiff(L: Lab, i: number, j: number) {
  const A = L.profCache![i], B = L.profCache![j]; let d = 0;
  for (const [lo, hi] of [[0, 120], [120, 240]]) {
    let mn = -1, mx = -1;
    for (let x = lo; x < hi; x++) if (A.t[x] >= 0) { if (mn < 0) mn = x; mx = x; }
    if (mn < 0) continue;
    const zw = Math.max(1, Math.round(.25 * (mx - mn + 1)));
    for (let x = mn; x <= mx; x++) {
      if (x >= mn + zw && x <= mx - zw) continue;
      if (B.t[x] < 0) { d = Math.max(d, A.b[x] - A.t[x] + 1); continue; }
      d = Math.max(d, Math.abs(A.t[x] - B.t[x]), Math.abs(A.b[x] - B.b[x]));
    }
  }
  return d * .45;
}
const asym = (L: Lab, i: number) => SHAPE_KEYS.some((k) => !near(L.AP[i][k][0], L.AP[i][k][1]));
// Review conflict 2: IoU < .90 is required for the named pairs and for P1 vs P1.
// IoU ≥ .90 anywhere else is a flag for an eyeball check: it passes if the lid corners differ by ≥ 2 OLED px or the face is asymmetric.
const PAIRS = [[0, 17], [1, 14], [2, 12], [5, 16], [4, 19], [10, 11], [3, 20]];
export interface IouInfo {
  pair: { j: number; v: number } | null; best: number; bj: number; p1: { j: number; v: number } | null;
  flag: { j: number; v: number; corner: number; asym: boolean; ok: boolean } | null;
}
export function iouInfo(L: Lab, i: number): IouInfo {
  if (!L.silCache) { L.silCache = L.AP.map((_, j) => silhouette(L, j)); L.profCache = L.silCache.map(profile); }
  const sil = L.silCache;
  let best = -1, bj = -1, p1best = -1, p1j = -1;
  for (let j = 0; j < L.AP.length; j++) {
    if (j === i || L.AP[j].overlay !== "none") continue;
    const v = iou(sil[i], sil[j]);
    if (v > best) { best = v; bj = j; }
    if (IS_P1.has(i) && IS_P1.has(j) && v > p1best) { p1best = v; p1j = j; }
  }
  const pr = PAIRS.find((p) => p[0] === i || p[1] === i);
  let pair = null; if (pr) { const o = pr[0] === i ? pr[1] : pr[0]; pair = { j: o, v: iou(sil[i], sil[o]) }; }
  let flag = null;
  const ruled = (pair && pair.j === bj) || (p1j === bj);
  if (bj >= 0 && best >= .9 && !ruled) {
    const cd = cornerDiff(L, i, bj), as = asym(L, i);
    flag = { j: bj, v: best, corner: cd, asym: as, ok: cd >= 2 || as };
  }
  return { pair, best, bj, p1: p1j >= 0 ? { j: p1j, v: p1best } : null, flag };
}

// ------------------------------------------------------------------ per-frame monitor
// Round 3: a startle may open fast. For the first 100 ms of spring time after entering SURPRISED or SHOCKED,
// an opening step up to 50 % is allowed; a shrinking step, a step over 50 % or anything after 100 ms still counts.
const STARTLE = new Set([3, 20]);
export function measure(L: Lab, now: number, m: AfterMetrics) {
  const AS = L.AS, mon = L.mon, stats = L.stats;
  const blinkNow = AS.blink.ph !== 0;
  // Review conflict 3: a change in shut is blink-class, so it exempts that eye only. The open eye still counts.
  const shutNow = [AS.eyes[0].shut.x, AS.eyes[1].shut.x];
  const shutMove = shutNow.map((v, e) => Math.abs(v - mon.prevShut[e]) > .002);
  if (!mon.skipNext && !blinkNow && !mon.prevBlink && !L.ui.sleep) {
    for (let e = 0; e < 2; e++) {
      if (shutMove[e] || mon.prevShutMove[e]) continue;
      const a = m.area[e], p = mon.prevArea[e], den = Math.max(a, p);
      if (den > 40) {
        const st = Math.abs(a - p) / den;
        if (STARTLE.has(AS.expr) && AS.entryT <= .1 + 1e-6 && a > p && st <= .5) continue;
        if (st > mon.maxStep) { mon.maxStep = st; mon.worst = mon.label || L.EXPR[AS.expr].name; }
      }
    }
  }
  mon.skipNext = false; mon.prevBlink = blinkNow; mon.prevArea = m.area.slice(); mon.prevShut = shutNow; mon.prevShutMove = shutMove;
  if (m.gap !== null && !L.ui.legacy && dev(L).k === 1) mon.minGap = Math.min(mon.minGap, m.gap);
  mon.pupilOut = Math.max(mon.pupilOut, m.pupilOut);
  const cut = now - 60000;
  for (const k of ["gaze", "blinks", "fidgets"] as const) while (stats[k].length && stats[k][0] < cut) stats[k].shift();
}
export function resetMonitors(L: Lab) { Object.assign(L.mon, { maxStep: 0, worst: "", minGap: 1e9, pupilOut: 0, swapsReq: 0, swapsBlink: 0, exitBlinks: 0, skipNext: true }); }

// A4: mouth-led faces. No eye-only cue exists, so criterion 1 waits on Kevin accepting the amendment.
export const MOUTH_LED = new Set([21]);
// A1: overlay faces are scored on the overlay. The narrowest stroke or row shift of the glyph on the 1-bit OLED
// (R × 0.45), from the same constants renderAfter draws with.
export function overlayInfo(L: Lab, i: number) {
  const P = L.AP[i], sc = Math.min(P.scale[0], P.scale[1]), R = L.GP.R * DEVICES.mono.k;
  if (P.overlay === "glitch") return { what: "smallest row shift", px: Math.round(.1 * R), closes: "pupil clipped to the lids" };
  if (P.overlay === "spiral") return { what: "spiral stroke", px: 2 * .05 * R * sc, closes: "spiral scales with the opening" };
  if (P.overlay === "x") return { what: "X stroke", px: 2 * .07 * R * sc, closes: "X scales with the opening" };
  return null;
}
