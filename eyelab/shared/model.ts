// The "after" eye model's drawing half (brief §3, reviews r2–r4): springs, the per-eye
// geometry, the one-span-per-column eye raster, pupils and overlays as cut-outs, brows,
// the ambient stroke and background. This is the reference the firmware's
// bot_eye_model.h is diffed against, so the math is the artifact's, unchanged.

import { Frame, PI, bump, clamp, lerp, rgb565, smooth } from "./raster";
import type { Device, Lab } from "./state";

export class Spring {
  x: number; v: number; f: number; z: number; t: number;
  constructor(x: number, f: number, z: number) { this.x = x; this.v = 0; this.f = f; this.z = z; this.t = x; }
  step(t: number, dt: number) {
    this.t = t;
    const w = 2 * PI * this.f, n = Math.max(1, Math.ceil(dt / 0.008)), h = dt / n;
    for (let i = 0; i < n; i++) { this.v += (w * w * (t - this.x) - 2 * this.z * w * this.v) * h; this.x += this.v * h; }
    if (!Number.isFinite(this.x) || !Number.isFinite(this.v)) { this.x = t; this.v = 0; }
    return this.x;
  }
  snap(t: number) { this.x = t; this.v = 0; this.t = t; }
}
const PEAK: Record<string, number> = {};
export function peakOf(f: number, z: number) {
  const key = f.toFixed(3) + "|" + z.toFixed(3);
  if (PEAK[key]) return PEAK[key];
  const s = new Spring(0, f, z); s.v = 1; let m = 0;
  for (let i = 0; i < 2000; i++) { s.step(0, 0.001); m = Math.max(m, Math.abs(s.x)); }
  return (PEAK[key] = m);
}
export const F_POP = 1 / (2 * PI * 0.12); // critically damped bump that peaks at 120 ms

/** One eye's geometry for a frame, from eyeGeom. */
export interface EyeGeom {
  e: number; side: number; inS: number; ex: number; ey: number; R: number; Rb: number; Rw: number; Hh: number;
  ltRaw: number; lb: number; sl: number; sag: number; bow: number; shut: number; pup: number; cup: number; arcW: number; lt: number; bE: number;
}
export interface AfterMetrics { area: number[]; gap: number | null; pupilOut: number; G: EyeGeom[] }

// ------------------------------------------------------------------ after raster: one vertical span per column
function bufs(L: Lab, w: number, h: number) {
  const B = L.buf;
  if (w !== B.w || h !== B.h) { B.w = w; B.h = h; B.M = [new Uint8Array(w * h), new Uint8Array(w * h)]; B.PM = new Uint8Array(w * h); B.CL = new Uint8Array(w * h); }
  B.M[0].fill(0); B.M[1].fill(0); B.PM.fill(0); B.CL.fill(0);
}
export function rasterEye(mask: Uint8Array, W: number, H: number, g: EyeGeom) {
  // g: ex, ey, Rb, Rw, Hh, lt, lb, sl, sag, bow, shut, cup, inS, arcW
  const Hf = 2 * g.Hh, dir = 1 - 2 * g.cup;
  const x0 = Math.floor(g.ex - g.Rw), x1 = Math.ceil(g.ex + g.Rw);
  let area = 0;
  for (let x = Math.max(0, x0); x <= Math.min(W - 1, x1); x++) {
    const u = (x + .5 - g.ex) / g.Rw; if (u <= -1 || u >= 1) continue;
    const uu = u * u, w = Math.sqrt(1 - uu); // the one sqrt per column
    const eT = g.ey - g.Hh * w, eB = g.ey + g.Hh * w, inner = g.inS * u;
    const tL = g.ey - g.Hh + g.lt * Hf + g.sl * g.Hh * inner + g.sag * Hf * (1 - uu);
    const bL = g.ey + g.Hh - g.lb * Hf * ((1 - g.bow) + g.bow * 1.1 * w);
    let top = Math.max(eT, tL), bot = Math.min(eB, bL);
    if (top > bot) { const m = clamp((tL + bL) / 2, eT, eB); top = bot = m; }
    if (g.shut > .001) {
      const ua = u / .92; let aT: number, aB: number;
      if (ua > -1 && ua < 1) {
        const q = 1 - ua * ua, ht = .5 * g.arcW * g.Rb * (.55 + .45 * q), yc = g.ey + g.Hh * (.05 + dir * (.32 * q - .16));
        aT = yc - ht; aB = yc + ht;
      } else { aT = aB = g.ey + g.Hh * (.05 - dir * .16); }
      top = lerp(top, aT, g.shut); bot = lerp(bot, aB, g.shut);
    }
    const ya = Math.max(0, Math.ceil(top - .5)), yb = Math.min(H - 1, Math.floor(bot - .5));
    for (let y = ya; y <= yb; y++) { mask[y * W + x] = 1; area++; }
  }
  return area;
}
export function eyeGeom(L: Lab, e: number, k: number, cx: number, cy: number, now: number, opt?: { rest: Record<string, unknown> }): EyeGeom {
  // opt: {rest:P} for static silhouettes, else live springs
  const AS = L.AS, ui = L.ui, GP = L.GP;
  const R = GP.R * k, side = e ? 1 : -1, inS = e ? -1 : 1;
  let v: (k2: string) => number;
  if (opt && opt.rest) { const P = opt.rest as Record<string, number[]>; v = (k2) => P[k2][e]; }
  else { const S = AS.eyes[e] as Record<string, { x: number }>; v = (k2) => S[k2].x; }
  const shut = clamp(v("shut"), 0, 1), cup = clamp(v("cup"), 0, 1);
  const live = !(opt && opt.rest);
  const bE = live ? AS.blink.b * clamp(1 - (shut - .35) / .3, 0, 1) : 0;
  const sc = Math.max(.5, v("scale")) * (live ? 1 + AS.popS.x : 1);
  const Rb = R * sc, Rw = Rb * (1 + .04 * bE);
  const a0 = v("aspect"), asp = Math.max(.3, a0 + (live ? AS.popA.x + a0 * AS.bStr.x : 0));
  const Hh = Rb * asp;
  const legacy = live && ui.legacy;
  const dist = legacy ? Rw - .125 * R : Rw + GP.gap * R / 2;
  const bob = live && ui.sleep && !ui.pendant ? .03 * R * Math.sin(2 * PI * .2 * now / 1000) : 0;
  const lt = clamp(v("lidTop"), 0, 1), lb = clamp(v("lidBot"), 0, 1);
  // One closing path for blinks and shut (review conflict 1, M1): the lids meet 58 % down,
  // then the eye morphs into the shut arc, so the held frame is a lid line, never an empty eye.
  const close = Math.max(bE, live ? shut : 0), lidK = smooth(bump(close, 0, .7)), arcK = live ? smooth(bump(close, .4, 1)) : shut;
  const ltB = lt < .58 ? lt + (.58 - lt) * lidK : lt;
  const lbB = lb + (Math.max(lb, .42) - lb) * lidK;
  return { e, side, inS, ex: cx + side * dist, ey: cy + bob, R, Rb, Rw, Hh, ltRaw: lt, lb: lbB, sl: v("slant"), sag: Math.max(0, v("sag")),
    bow: clamp(v("bow"), 0, 1), shut: arcK, pup: 1 - Math.max(lidK, arcK), cup, arcW: GP.arcW, lt: ltB, bE };
}
function strokeSpans(fr: Frame, mask: Uint8Array, k: number, c: number) {
  const W = fr.w, H = fr.h, rad = Math.max(1, Math.round(5 * k));
  const top = new Int16Array(W).fill(-1), bot = new Int16Array(W).fill(-1);
  for (let x = 0; x < W; x++) { for (let y = 0; y < H; y++) if (mask[y * W + x]) { if (top[x] < 0) top[x] = y; bot[x] = y; } }
  const st = new Int16Array(W).fill(32767), sb = new Int16Array(W).fill(-1);
  for (let x = 0; x < W; x++) {
    if (top[x] < 0) continue;
    for (let dx = -rad; dx <= rad; dx++) {
      const xx = x + dx; if (xx < 0 || xx >= W) continue;
      const ext = Math.floor(Math.sqrt(rad * rad - dx * dx));
      st[xx] = Math.min(st[xx], top[x] - ext); sb[xx] = Math.max(sb[xx], bot[x] + ext);
    }
  }
  for (let x = 0; x < W; x++) if (sb[x] >= 0) fr.vline(x, st[x], sb[x] - st[x] + 1, c);
}
function fillPoly(W: number, H: number, pts: number[][], out: Uint8Array, clipMask: Uint8Array, ellipse: { ex: number; ey: number; rx: number; ry: number } | null) {
  let minX = 1e9, maxX = -1e9;
  for (const p of pts) { minX = Math.min(minX, p[0]); maxX = Math.max(maxX, p[0]); }
  let outside = 0; const ys: number[] = [];
  for (let x = Math.max(0, Math.floor(minX)); x <= Math.min(W - 1, Math.ceil(maxX)); x++) {
    const xc = x + .5; ys.length = 0;
    for (let i = 0, n = pts.length; i < n; i++) {
      const a = pts[i], b = pts[(i + 1) % n];
      if ((a[0] <= xc && b[0] > xc) || (b[0] <= xc && a[0] > xc)) ys.push(a[1] + (xc - a[0]) * (b[1] - a[1]) / (b[0] - a[0]));
    }
    ys.sort((p, q) => p - q);
    for (let j = 0; j + 1 < ys.length; j += 2) {
      const ya = Math.max(0, Math.ceil(ys[j] - .5)), yb = Math.min(H - 1, Math.floor(ys[j + 1] - .5));
      for (let y = ya; y <= yb; y++) {
        const p = y * W + x;
        if (ellipse) { const dx = (xc - ellipse.ex) / ellipse.rx, dy = (y + .5 - ellipse.ey) / ellipse.ry; if (dx * dx + dy * dy > 1) outside++; }
        if (clipMask[p]) out[p] = 1;
      }
    }
  }
  return outside;
}
function capsule(W: number, H: number, ax: number, ay: number, bx: number, by: number, r: number, out: Uint8Array, clip: Uint8Array) {
  const x0 = Math.max(0, Math.floor(Math.min(ax, bx) - r)), x1 = Math.min(W - 1, Math.ceil(Math.max(ax, bx) + r));
  const y0 = Math.max(0, Math.floor(Math.min(ay, by) - r)), y1 = Math.min(H - 1, Math.ceil(Math.max(ay, by) + r));
  const dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy || 1e-6, r2 = r * r;
  for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
    const px = x + .5 - ax, py = y + .5 - ay, t = clamp((px * dx + py * dy) / L2, 0, 1), qx = px - t * dx, qy = py - t * dy;
    if (qx * qx + qy * qy <= r2) { const p = y * W + x; if (clip[p]) out[p] = 1; }
  }
}
function pupilOutline(rx: number, ry: number, heart: number, star: number, rot: number) {
  const N = 44, e0 = Math.max(0, 1 - heart - star), pts: number[][] = [], c = Math.cos(rot), s = Math.sin(rot);
  for (let i = 0; i < N; i++) {
    const a = i / N * 2 * PI, ca = Math.cos(a), sa = Math.sin(a);
    let x = ca * e0, y = sa * e0;
    if (heart > 0) { const t = a + PI / 2, sn = Math.sin(t); x += heart * sn * sn * sn; y += heart * (-(13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t)) / 16 - .15); }
    if (star > 0) { const sx = Math.sign(ca) * Math.pow(Math.abs(ca), 2.6) * 1.12, sy = Math.sign(sa) * Math.pow(Math.abs(sa), 2.6) * 1.12; x += star * (sx * c - sy * s); y += star * (sx * s + sy * c); }
    pts.push([x * rx, y * ry]);
  }
  return pts;
}
function browSpans(fr: Frame, x0: number, y0: number, x1: number, y1: number, half: number, c: number) {
  for (let x = Math.ceil(x0 - .5); x <= Math.floor(x1 - .5); x++) {
    const t = (x + .5 - x0) / ((x1 - x0) || 1), yc = lerp(y0, y1, clamp(t, 0, 1));
    const ya = Math.ceil(yc - half - .5), yb = Math.floor(yc + half - .5);
    fr.vline(x, ya, yb - ya + 1, c);
  }
}
// Draw the after face. Returns metrics for the scorecard.
export function renderAfter(L: Lab, fr: Frame, D: Device, now: number, fc: number, mouthFn: () => void): AfterMetrics {
  const AS = L.AS, ui = L.ui, GP = L.GP;
  const W = fr.w, H = fr.h, k = D.k, cx = D.cx, cy = D.cy, stroke = ui.ambient && !D.mono;
  bufs(L, W, H);
  const { M, PM, CL, MU, GB } = L.buf;
  const P = L.AP[AS.expr], G = [eyeGeom(L, 0, k, cx, cy, now), eyeGeom(L, 1, k, cx, cy, now)];
  const area = [0, 0]; let pupilOut = 0;
  for (let e = 0; e < 2; e++) {
    const g = G[e];
    area[e] = rasterEye(M[e], W, H, g);
    if (AS.overlay === "glitch") { // shift rows of the silhouette, not paint bars
      MU[e] = M[e].slice(); GB[e] = [];
      const tmp = new Uint8Array(W);
      for (const b of AS.glitch[e].bands) {
        const ya = Math.max(0, Math.round(g.ey - g.Hh + b.y0 * 2 * g.Hh)), yb = Math.min(H - 1, Math.round(ya + b.h * 2 * g.Hh)), dx = Math.round(b.dx * g.R);
        GB[e].push([ya, yb, dx]);
        for (let y = ya; y <= yb; y++) {
          const o = y * W; tmp.fill(0);
          for (let x = 0; x < W; x++) { const s = x - dx; if (s >= 0 && s < W) tmp[x] = M[e][o + s]; }
          M[e].set(tmp, o);
        }
      }
      // shifted rows must not cross the gap: clip each eye to its own side of the face centre
      const lim = Math.max(1, Math.round(GP.gap * g.R / 2));
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        if (e === 0 ? x + .5 > cx - lim : x + .5 < cx + lim) M[e][y * W + x] = 0;
      }
    }
  }
  // merge / gap check
  let lMax = -1, rMin = W;
  for (let x = 0; x < W; x++) for (let y = 0; y < H; y++) { if (M[0][y * W + x]) lMax = Math.max(lMax, x); if (M[1][y * W + x] && x < rMin) rMin = x; }
  const gap = (lMax >= 0 && rMin < W) ? rMin - lMax - 1 : null;
  if (stroke) { const U = new Uint8Array(W * H); for (let p = 0; p < W * H; p++) U[p] = M[0][p] | M[1][p]; strokeSpans(fr, U, k, 0); }
  for (let p = 0; p < W * H; p++) if (M[0][p] || M[1][p]) fr.px[p] = fc;
  // pupils / overlays (cut-outs in the background colour, clipped to the silhouette)
  const t = now / 1000;
  for (let e = 0; e < 2; e++) {
    const g = G[e], S = AS.eyes[e];
    if (!ui.pupils) continue;
    const heart = clamp(S.heart.x, 0, 1), star = clamp(S.star.x, 0, 1 - heart);
    const pulse = P.pulse && !ui.pendant ? 1 + .06 * Math.sin(7 * t) : 1;
    let rx = Math.max(0, S.pw.x) * g.Rb * g.pup * pulse, ry = Math.max(0, S.ph.x) * g.Rb * g.pup * pulse; // pupils shrink as the lids close
    const ov = AS.overlay;
    if (ov === "x" || ov === "spiral") { rx = ry = .42 * g.Rb; }
    let qx = g.inS * S.inset.x * g.Rb + AS.gxOut * g.Rb, qy = S.pdy.x * g.Rb + AS.gyOut * g.Rb;
    const ext = 1 + .15 * star, ax = Math.max(1, g.Rb - rx * ext - .06 * g.R), ay = Math.max(1, g.Hh - ry * ext - .06 * g.R), kk = Math.hypot(qx / ax, qy / ay);
    if (kk > 1) { qx /= kk; qy /= kk; }
    const pcx = g.ex + qx, pcy = g.ey + qy;
    if ((ov === "x" || ov === "spiral") && g.pup < .3) continue; // r4 O2: glyphs close with the lids, like pupils
    if (ov === "x") {
      const a = .42 * g.Rb * g.pup / Math.SQRT2, r = .07 * g.Rb;
      capsule(W, H, pcx - a, pcy - a, pcx + a, pcy + a, r, PM, M[e]); capsule(W, H, pcx - a, pcy + a, pcx + a, pcy - a, r, PM, M[e]);
    } else if (ov === "spiral") {
      const sx = g.ex + AS.gxOut * g.Rb * .5, sy = g.ey + AS.gyOut * g.Rb * .5, rmax = .7 * Math.min(g.Rb, g.Hh) * g.pup, tmax = 1.75 * 2 * PI, ph = g.side * AS.spiralA, r = .05 * g.Rb;
      let px = sx, py = sy;
      for (let th = .12; th <= tmax + 1e-6; th += .12) {
        const rr = rmax * th / tmax, a = g.side * th + ph, nx = sx + rr * Math.cos(a), ny = sy + rr * Math.sin(a);
        capsule(W, H, px, py, nx, ny, r, PM, M[e]); px = nx; py = ny;
      }
    } else if (rx >= .5 && ry >= .5) {
      const pts = pupilOutline(rx, ry, heart, star, AS.spinA).map((p) => [pcx + p[0], pcy + p[1]]);
      pupilOut += fillPoly(W, H, pts, PM, AS.overlay === "glitch" ? MU[e]! : M[e], { ex: g.ex, ey: g.ey, rx: g.Rw, ry: g.Hh });
      const pr = Math.min(rx, ry);
      if (ui.catchlight && !D.mono && pr >= 7 && heart < .3 && star < .3) {
        const lx = pcx - .35 * pr, ly = pcy - .35 * pr, lr = .3 * pr;
        for (let y = Math.floor(ly - lr); y <= Math.ceil(ly + lr); y++) for (let x = Math.floor(lx - lr); x <= Math.ceil(lx + lr); x++) {
          if (x < 0 || y < 0 || x >= W || y >= H) continue;
          const dx = x + .5 - lx, dy = y + .5 - ly; if (dx * dx + dy * dy <= lr * lr) { const p = y * W + x; if (PM[p]) CL[p] = 1; }
        }
      }
    }
  }
  if (AS.overlay === "glitch" && ui.pupils) { // r4 O1: the pupil rides the shifted rows, so the eye reads as sliced
    const tmp = new Uint8Array(W);
    for (let e = 0; e < 2; e++) for (const [ya, yb, dx] of GB[e]) for (let y = ya; y <= yb; y++) {
      const o = y * W; tmp.fill(0);
      for (let x = 0; x < W; x++) { if (e === 0 ? x + .5 > cx : x + .5 < cx) { tmp[x] = PM[o + x]; continue; } const s = x - dx; if (s >= 0 && s < W && (e === 0 ? s + .5 <= cx : s + .5 >= cx)) tmp[x] = PM[o + s]; }
      PM.set(tmp, o);
    }
    for (let p = 0; p < W * H; p++) if (PM[p] && !(M[0][p] || M[1][p])) PM[p] = 0;
  }
  for (let p = 0; p < W * H; p++) { if (PM[p]) fr.px[p] = CL[p] ? fc : 0; }
  // brows: follow the un-blinked top lid, dip 0.04 R at full close, never vanish
  if (ui.brows && AS.brows) {
    for (const pass of stroke ? [0, 1] : [1]) for (let e = 0; e < 2; e++) {
      const g = G[e], Bw = AS.brow[e];
      const ang = (Bw.ang.x + 20 * AS.eyes[e].slant.x) * PI / 180, len = Bw.len.x * k, th = Math.max(1, Bw.thick.x * k);
      const by = (g.ey - g.Hh) + .5 * g.ltRaw * 2 * g.Hh - .24 * g.R + .04 * g.R * AS.blink.b;
      const x0 = g.ex - len, x1 = g.ex + len;
      let y0: number, y1: number;
      if (e === 0) { y0 = by + Math.sin(-ang) * len; y1 = by + Math.sin(ang) * len; }
      else { y0 = by + Math.sin(ang) * len; y1 = by - Math.sin(ang) * len; }
      const half = th / 2 / Math.cos(ang), sk = pass ? 0 : 5 * k;
      if (Math.min(y0, y1) - half < 0) continue; // r4: a brow that would leave the screen is not drawn
      browSpans(fr, x0 - sk, y0, x1 + sk, y1, half + sk, pass ? fc : 0);
    }
  }
  if (ui.mouth) mouthFn();
  return { area, gap, pupilOut, G };
}

// ------------------------------------------------------------------ ambient background stand-in (pixel mode)
const HUE = new Uint16Array(256);
for (let i = 0; i < 256; i++) {
  const h = i / 256 * 6, c = 190, x = Math.round(c * (1 - Math.abs(h % 2 - 1)));
  const [r, g, b] = h < 1 ? [c, x, 0] : h < 2 ? [x, c, 0] : h < 3 ? [0, c, x] : h < 4 ? [0, x, c] : h < 5 ? [x, 0, c] : [c, 0, x];
  HUE[i] = rgb565(r + 20, g + 20, b + 20);
}
export function drawAmbient(fr: Frame, now: number) {
  const t = now / 1000, bs = Math.max(2, Math.round(fr.w / 24));
  for (let by = 0; by * bs < fr.h; by++) for (let bx = 0; bx * bs < fr.w; bx++) {
    const v = Math.sin(bx * .45 + t * .9) + Math.sin(by * .38 - t * 1.2) + Math.sin((bx + by) * .25 + t * .6);
    fr.fillRect(bx * bs, by * bs, bs, bs, HUE[Math.floor((v + 3) / 6 * 255) & 255]);
  }
}
