// The "after" eye model's behaviour half (brief §4, reviews r2–r4): blinks on one closing
// path, overlay/brow swaps under a blink, gaze patterns, idle looks and fidgets on NEUTRAL,
// mood pop on entry, exit blinks, and the per-frame spring step. Ported from the artifact
// lab unchanged, with the Lab state passed in instead of closed over.

import type { EyeParams } from "./faces";
import { EYE_KEYS, PRESETS, PUP_KEYS, SHAPE_KEYS } from "./faces";
import { PI, bump, clamp, rnd, smooth } from "./raster";
import { F_POP, peakOf } from "./model";
import type { Lab, Why } from "./state";
import { padEngaged } from "./state";

export const curP = (L: Lab): EyeParams => L.AP[L.AS.expr];
const shutMin = (L: Lab) => Math.min(L.AS.eyes[0].shut.x, L.AS.eyes[1].shut.x);

function blinkAllowed(L: Lab, now: number) {
  const P = curP(L);
  if (L.ui.sleep) return false;
  if (shutMin(L) >= .5) return false;
  if (P.noBlinkMs && now - L.AS.enteredAt < P.noBlinkMs) return false;
  return true;
}
export function blinkStart(L: Lab, now: number, why: Why) {
  const B = L.AS.blink, ui = L.ui;
  if (B.ph !== 0) return false;
  if (why !== "swap" && why !== "exit" && !blinkAllowed(L, now)) return false;
  B.ph = 1; B.t = now; B.last = now; L.stats.blinks.push(now); // A2: an exit blink is a blink like any other
  if (why === "exit") L.mon.exitBlinks++;
  // any blink restarts the auto interval (review conflict 4, M3); auto blinks set it in updateBlink
  if (why !== "auto") B.next = now + (ui.pendant ? rnd(4000, 9000) : rnd(2600, 6800) / (curP(L).blinkRate || 1));
  if (why === "auto" && !ui.pendant && Math.random() < .18) B.queue.push({ at: now + 320, why: "double" });
  return true;
}
function applySwap(L: Lab) {
  const AS = L.AS;
  if (!AS.pending) return;
  AS.overlay = AS.pending.overlay; AS.brows = AS.pending.brows; AS.pending = null; L.mon.swapsBlink++;
}
export function requestSwap(L: Lab, now: number, next: { overlay: string; brows: boolean }) {
  const AS = L.AS;
  if (next.overlay === AS.overlay && next.brows === AS.brows) { AS.pending = null; return; }
  L.mon.swapsReq++;
  AS.pending = next;
  if (shutMin(L) >= .5 || L.ui.sleep) { applySwap(L); return; }
  const B = AS.blink;
  if (B.ph === 2) applySwap(L);
  else if (B.ph === 3) B.queue.unshift({ at: now, why: "swap" });
  else if (B.ph === 0) blinkStart(L, now, "swap");
}
function updateBlink(L: Lab, now: number) {
  const AS = L.AS, ui = L.ui, B = AS.blink, P = curP(L);
  if (B.ph === 0) {
    if (B.queue.length && now >= B.queue[0].at) { const q = B.queue.shift()!; blinkStart(L, now, q.why); }
    else if (ui.idle && now >= B.next) {
      B.next = now + (ui.pendant ? rnd(4000, 9000) : rnd(2600, 6800) / (P.blinkRate || 1));
      blinkStart(L, now, "auto");
    }
  }
  if (B.ph === 0 && AS.pending && shutMin(L) >= .5) applySwap(L);
  const hold = Math.max(30, 1000 / ui.fps + 1);
  for (let guard = 0; guard < 5; guard++) {
    if (B.ph === 1) { const e = now - B.t; if (e >= 80) { B.ph = 2; B.t += 80; applySwap(L); continue; } B.b = smooth(e / 80); break; }
    if (B.ph === 2) { const e = now - B.t; if (e >= hold) { B.ph = 3; B.t += hold; AS.bStr.v += 0.03 / peakOf(F_POP, 1); continue; } B.b = 1; break; }
    if (B.ph === 3) { const e = now - B.t; if (e >= 140) { B.ph = 0; B.b = 0; break; } B.b = 1 - smooth(e / 140); break; }
    B.b = 0; break;
  }
}
function newSacc(L: Lab, now: number, x: number, y: number, count?: boolean) {
  const g = L.AS.gaze, d = Math.hypot(x - g.tx, y - g.ty);
  g.tx = x; g.ty = y; g.mode = "sacc";
  if (count !== false && d > .01) L.stats.gaze.push(now);
  if (d > .25 && now - L.AS.blink.last > 1500 && Math.random() < .25) blinkStart(L, now, "saccade");
}
interface FidgetFrame { eye?: Record<string, number>; pupil?: string | null; gaze?: { x: number; y: number; mode: "sacc" | "pursuit" } }
export const FIDGETS: Record<string, { dur: number; start?: (L: Lab, now: number) => void; f: (p: number) => FidgetFrame }> = {
  eyeroll: { dur: 1700, f: (p) => { const a = -PI / 2 - p * 2 * PI; return { eye: { lidTop: .22 }, pupil: "small", gaze: { x: Math.cos(a) * .24, y: Math.sin(a) * .16, mode: "pursuit" } }; } },
  peek: { dur: 2300, f: (p): FidgetFrame => p < .55 ? { eye: { lidTop: .42, lidBot: .2 }, gaze: { x: .3, y: 0, mode: "sacc" } } : { eye: { scale: 1.06 }, pupil: "dot", gaze: { x: 0, y: 0, mode: "sacc" } } },
  glance: { dur: 4200, f: (p) => { const seq = [[-.26, -.04], [.26, -.04], [.18, -.13], [-.1, .08], [0, 0]], s = seq[Math.min(4, Math.floor(p * 5))]; return { gaze: { x: s[0], y: s[1], mode: "sacc" } }; } },
  triple: { dur: 1100, start: (L, now) => { L.AS.blink.queue.push({ at: now, why: "fidget" }, { at: now + 370, why: "fidget" }, { at: now + 740, why: "fidget" }); }, f: () => ({}) },
  yawn: { dur: 3000, f: (p) => { const c = bump(p, 0, .25) * (1 - bump(p, .62, .72)), w = bump(p, .68, .76) * (1 - bump(p, .85, 1)); return { eye: { shut: c, cup: .3 * c, aspect: .92 + .1 * w }, pupil: w > 0 ? "small" : null, gaze: { x: 0, y: -.08 * c, mode: "pursuit" } }; } },
};
export function startFidget(L: Lab, now: number, name: string, forced?: boolean) {
  const F = FIDGETS[name];
  L.AS.idle.fid = { name, t0: now, dur: F.dur, p: 0, forced: !!forced };
  L.AS.idle.lastFid = name; L.stats.fidgets.push(now);
  if (F.start) F.start(L, now);
}
function updateIdle(L: Lab, now: number) {
  const I = L.AS.idle, ui = L.ui;
  if (I.fid) { const p = (now - I.fid.t0) / I.fid.dur; if (p >= 1) { I.fid = null; I.nextFid = now + rnd(8000, 16000); } else I.fid.p = p; }
  const neutral = L.AS.expr === 0 && ui.idle && !ui.pendant && !ui.sleep && !padEngaged(L);
  if (!neutral) { if (I.fid && !I.fid.forced) I.fid = null; I.look = null; return; }
  if (I.look && now >= I.look.until) { I.look = null; I.nextLook = now + rnd(9000, 22000); }
  if (!I.fid && !I.look && now >= I.nextFid) {
    const names = ["eyeroll", "peek", "glance", "triple"].filter((n) => n !== I.lastFid);
    if (now - L.lastInteract > 300000 && I.lastFid !== "yawn") names.push("yawn");
    startFidget(L, now, names[Math.floor(Math.random() * names.length)]);
  }
  if (!I.fid && !I.look && now >= I.nextLook) {
    if (Math.random() < .3) I.look = { kind: "mood", mood: Math.random() < .5 ? "curious" : "bored", until: now + 2600 };
    else I.look = { kind: "pupil", preset: ["dot", "big", "small"][Math.floor(Math.random() * 3)], until: now + rnd(1800, 4500) };
  }
}
function updateGaze(L: Lab, now: number) {
  const AS = L.AS, ui = L.ui, pad = L.pad;
  const P = curP(L), g = AS.gaze, bx = P.gazeX, by = P.gazeY, lp = ui.pendant;
  const fx = AS.idle.fid ? FIDGETS[AS.idle.fid.name].f(AS.idle.fid.p) : null;
  if (padEngaged(L)) { g.tx = pad.x * .35; g.ty = pad.y * .25; g.mode = "pursuit"; return; }
  if (ui.sleep) { g.tx = 0; g.ty = 0; g.mode = "pursuit"; return; }
  if (fx && fx.gaze) {
    if (fx.gaze.mode === "sacc") { if (Math.abs(fx.gaze.x - g.tx) > .005 || Math.abs(fx.gaze.y - g.ty) > .005) newSacc(L, now, fx.gaze.x, fx.gaze.y); }
    else { g.tx = fx.gaze.x; g.ty = fx.gaze.y; g.mode = "pursuit"; }
    return;
  }
  if (!ui.idle) { g.tx = bx; g.ty = by; g.mode = "sacc"; return; }
  const pat = lp && P.gaze !== "idle" ? "idle" : P.gaze;
  switch (pat) {
    case "thinking": { const w = 2 * PI * .15 * now / 1000; g.tx = bx + .14 + .12 * Math.cos(w); g.ty = by - .12 + .08 * Math.sin(w); g.mode = "pursuit"; break; }
    case "nervous": case "worried":
      if (now >= g.next) { newSacc(L, now, bx + rnd(-.15, .15), by + rnd(-.1, .1)); g.next = now + rnd(260, 500) * (pat === "worried" ? 2 : 1); }
      break;
    case "shy":
      if (now >= g.next) { g.up = !g.up; if (g.up) { newSacc(L, now, bx - .08, by - .06); g.next = now + 450; } else { newSacc(L, now, bx - .2, by + .18); g.next = now + 3000; } }
      break;
    case "focused":
      if (now >= g.next) { g.micro = !g.micro; const a = rnd(0, 2 * PI); newSacc(L, now, g.micro ? bx + .05 * Math.cos(a) : bx, g.micro ? by + .05 * Math.sin(a) : by); g.next = now + rnd(6000, 10000); }
      break;
    case "annoyed":
      if (now >= g.next) { g.side = -g.side; newSacc(L, now, bx + g.side * .28, by); g.next = now + 4000; }
      break;
    case "shocked": g.tx = bx; g.ty = by; g.mode = "sacc"; break;
    default:
      if (now >= g.next) {
        const c = Math.random() < .15;
        newSacc(L, now, c ? bx : bx + rnd(-.22, .22), c ? by : by + rnd(-.14, .14));
        g.next = now + (lp ? rnd(8000, 15000) : rnd(2800, 6600));
      }
  }
}
export function targets(L: Lab, now: number) {
  const P = curP(L), T = {} as Record<string, [number, number]>;
  for (const k of EYE_KEYS) T[k] = [P[k][0], P[k][1]];
  let pupil: string | null = null;
  const I = L.AS.idle;
  if (I.look) {
    if (I.look.kind === "pupil") pupil = I.look.preset!;
    else if (I.look.mood === "curious") { T.lidTop = [0, .18]; T.scale = [1.06, .95]; }
    else T.lidTop = [.44, .44];
  }
  if (I.fid) {
    const r = FIDGETS[I.fid.name].f(I.fid.p);
    if (r.eye) for (const k in r.eye) T[k] = [r.eye[k], r.eye[k]];
    if (r.pupil) pupil = r.pupil;
  }
  if (now < L.AS.smallUntil) pupil = "small";
  if (pupil) { const pr = PRESETS[pupil]; for (const k of ["pw", "ph"] as const) { const v = pr[k]!; T[k] = Array.isArray(v) ? [v[0], v[1]] : [v, v]; } }
  if (L.ui.sleep) { T.shut = [1, 1]; T.cup = [0, 0]; }
  return T;
}
export function afterSetExpr(L: Lab, i: number, now: number, instant: boolean) {
  const AS = L.AS, ui = L.ui;
  const prevExpr = AS.expr;
  AS.expr = i; AS.enteredAt = now; AS.entryT = 0;
  const P = L.AP[i], E = L.EXPR[i];
  AS.idle.look = null;
  const fid = AS.idle.fid;
  if (!(fid && fid.forced && now - fid.t0 < fid.dur)) AS.idle.fid = null; // an expired forced eyeroll must not leak into the next face
  AS.idle.nextFid = now + rnd(8000, 16000); AS.idle.nextLook = now + rnd(9000, 22000);
  AS.gaze.next = now + 250; AS.gaze.up = false; AS.gaze.micro = false;
  AS.smallUntil = P.entry === "small" ? now + 150 : 0;
  if (instant) {
    const T = targets(L, now);
    for (let e = 0; e < 2; e++) {
      for (const k of EYE_KEYS) AS.eyes[e][k].snap(T[k][e]);
      AS.brow[e].ang.snap(e ? E.browAngleR : E.browAngleL); AS.brow[e].len.snap(E.browLength); AS.brow[e].thick.snap(E.browThickness);
    }
    AS.gaze.tx = P.gazeX; AS.gaze.ty = P.gazeY; AS.gx.snap(P.gazeX); AS.gy.snap(P.gazeY);
    AS.gxOut = clamp(P.gazeX, -.35, .35); AS.gyOut = clamp(P.gazeY, -.25, .25); // first frame shows the new gaze
    AS.popS.snap(0); AS.popA.snap(0); AS.bStr.snap(0);
    AS.overlay = P.overlay; AS.brows = E.browVisible; AS.pending = null;
    if (P.entry === "eyeroll") startFidget(L, now, "eyeroll", true);
    return;
  }
  const pop = P.pop, zP = ui.pendant ? 1 : .6;
  AS.popS.v += .06 * pop / peakOf(AS.popS.f, 1);
  AS.popA.v += -.04 * pop / peakOf(AS.popA.f, ui.pendant ? 1 : AS.popA.z);
  const pk = peakOf(3, zP);
  for (let e = 0; e < 2; e++) for (const k of ["pw", "ph"] as const) AS.eyes[e][k].v += .15 * pop * P[k][e] / pk;
  if (L.AP[prevExpr].exitBlink && prevExpr !== i && AS.blink.ph === 0) blinkStart(L, now, "exit"); // r4 M5: a stare ends with a blink
  requestSwap(L, now, { overlay: P.overlay, brows: E.browVisible });
  if (P.entry === "eyeroll") startFidget(L, now, "eyeroll", true);
}
export function stepAfter(L: Lab, now: number, dt: number) {
  const AS = L.AS, ui = L.ui, GP = L.GP;
  dt = Math.min(dt, .05); AS.entryT += dt;
  const P = curP(L), E = L.EXPR[AS.expr], lp = ui.pendant;
  updateIdle(L, now); updateGaze(L, now); updateBlink(L, now);
  const T = targets(L, now);
  const fS = 0.75 / (P.transitionMs / 1000), zS = lp ? 1 : .85, zP = lp ? 1 : .6;
  for (let e = 0; e < 2; e++) {
    const S = AS.eyes[e];
    for (const k of SHAPE_KEYS) { S[k].f = fS; S[k].z = zS; S[k].step(T[k][e], dt); }
    for (const k of PUP_KEYS) { S[k].f = 3; S[k].z = zP; S[k].step(T[k][e], dt); }
    const Bw = AS.brow[e];
    for (const sp of [Bw.ang, Bw.len, Bw.thick]) { sp.f = fS; sp.z = zS; }
    Bw.ang.step(e ? E.browAngleR : E.browAngleL, dt); Bw.len.step(E.browLength, dt); Bw.thick.step(E.browThickness, dt);
  }
  const g = AS.gaze;
  for (const sp of [AS.gx, AS.gy]) { if (g.mode === "pursuit") { sp.f = 2; sp.z = lp ? 1 : .9; } else { sp.f = 6; sp.z = 1; } }
  AS.gx.step(clamp(g.tx, -.35, .35), dt); AS.gy.step(clamp(g.ty, -.25, .25), dt);
  AS.popS.step(0, dt); AS.popA.z = lp ? 1 : .45; AS.popA.step(0, dt); AS.bStr.step(0, dt);
  let tx = AS.gx.x, ty = AS.gy.x;
  if (P.gaze === "shocked" && !lp && ui.idle && !padEngaged(L)) { tx += rnd(-.03, .03); ty += rnd(-.03, .03); }
  AS.gxOut = clamp(tx, -.35, .35); AS.gyOut = clamp(ty, -.25, .25);
  if (!lp) {
    if (Math.max(AS.eyes[0].star.x, AS.eyes[1].star.x) > .5) AS.spinA += GP.spin * dt;
    AS.spiralA += 2 * PI * .5 * dt;
  }
  for (let e = 0; e < 2; e++) {
    const gl = AS.glitch[e];
    if (AS.overlay === "glitch" && now >= gl.next && !(lp && gl.bands.length)) {
      gl.bands = [0, 1, 2].map(() => ({ y0: rnd(0, .8), h: rnd(.1, .28), dx: (Math.random() < .5 ? -1 : 1) * rnd(.1, .2) }));
      gl.next = now + rnd(120, 250);
    }
  }
}
export function afterAtRest(L: Lab) {
  const AS = L.AS, R = L.GP.R;
  if (AS.blink.ph !== 0 || AS.pending || AS.blink.queue.length) return false;
  const all = [AS.gx, AS.gy, AS.popS, AS.popA, AS.bStr];
  for (let e = 0; e < 2; e++) { for (const k of EYE_KEYS) all.push(AS.eyes[e][k]); all.push(AS.brow[e].ang, AS.brow[e].len, AS.brow[e].thick); }
  for (const s of all) if (Math.abs(s.t - s.x) * R > .25 || Math.abs(s.v) * R > .5) return false;
  if ((AS.overlay === "spiral" || curP(L).pulse) && !L.ui.pendant) return false;
  return !L.ui.ambient;
}
