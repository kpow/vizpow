// The "before" side: a port of the firmware's eye renderer as it shipped before the
// parametric model (renderBotFace in bot_eyes.h, the bot_faces.h table and its lerp,
// blink, look-around and the IMU tracker). It also draws the mouth for both sides.
// Ported verbatim from the artifact eye lab; keep the integer math as it is.

import { Gfx, PI, arduinoRandom, clamp, cons, idiv, lround } from "./raster";
import type { Face } from "./faces";
import { MOUTH_TYPES } from "./faces";

// ------------------------------------------------------------------ firmware tables (bot_faces.h)
export const EM = { NORMAL: 0, CARET: 1, HEART: 2, X: 3, SPIRAL: 4, STAR: 5, CLOSED: 6, DIAMOND: 7, HALF: 8, DOT: 9, CURVED: 10, WINK: 11, GLITCH: 12 };
export const EM_NAME = ["EYE_NORMAL", "EYE_CARET", "EYE_HEART", "EYE_X", "EYE_SPIRAL", "EYE_STAR", "EYE_CLOSED", "EYE_DIAMOND", "EYE_HALF", "EYE_DOT", "EYE_CURVED", "EYE_WINK", "EYE_GLITCH"];
export const MT = { NONE: 0, LINE: 1, SMILE: 2, FROWN: 3, OPEN_O: 4, GRIN: 5, WAVY: 6, SMIRK: 7, TEETH: 8, TONGUE: 9, ZIGZAG: 10, POUT: 11, WHISTLE: 12, FLAT_FROWN: 13 };

type Row = [string, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number];
// The BotExpression table as it shipped with the 13 eye modes. Brow and mouth columns here are
// what the seed script wrote into faces/; at run time the face files supply brows and mouths.
export const ROWS: Row[] = [
// name,        W, H, sp,  pr,pox,poy, boy,bl,bt, bal,bar,bv, mouth,        mw,moy,mc, mode,        ms
['NEUTRAL',    50,45,44, 13,  0,  0, -12,30,6,   0,  0,1, MT.SMILE,     18,62, 6, EM.NORMAL, 300],
['HAPPY',      52,50,44, 14,  0, -2, -18,28,5,  10, 10,1, MT.SMILE,     24,58,12, EM.NORMAL, 250],
['SAD',        48,40,44, 14,  0,  8, -10,28,6, -18,-18,1, MT.FROWN,     16,65, 8, EM.NORMAL, 400],
['SURPRISED',  55,55,46,  9,  0,  0, -20,26,5,  12, 12,1, MT.OPEN_O,    14,62,14, EM.NORMAL, 150],
['CHILL',      48,38,44, 14,  0,  2,  -8,28,5,  -3, -3,1, MT.SMILE,     22,56,10, EM.HALF,   400],
['ANGRY',      52,32,44, 12,  0,  2,  -6,32,7,  22, 22,1, MT.LINE,      20,62, 0, EM.NORMAL, 200],
['LOVE',       50,45,46, 13,  0,  0, -14,28,5,   8,  8,0, MT.SMILE,     20,60,10, EM.HEART,  300],
['DIZZY',      50,45,44, 13,  0,  0, -10,26,5,  -8, 12,1, MT.WAVY,      18,62, 6, EM.SPIRAL, 200],
['THINKING',   50,45,44, 12, 10,-10, -14,28,6,   0, 15,1, MT.LINE,      10,62, 0, EM.NORMAL, 350],
['EXCITED',    52,48,46, 14,  0,  0, -18,26,5,  12, 12,0, MT.SMILE,     26,56,14, EM.STAR,   200],
['MISCHIEF',   50,45,44, 13, 12,  2, -12,28,6,  -6, 14,1, MT.SMIRK,     18,62, 8, EM.NORMAL, 300],
['SKEPTICAL',  50,45,44, 13,  6,  2, -12,28,6, -10, 18,1, MT.LINE,      14,62, 0, EM.NORMAL, 300],
['WORRIED',    52,50,44, 11,  0,  4, -16,26,5, -14,-14,1, MT.FROWN,     12,64, 6, EM.NORMAL, 350],
['CONFUSED',   50,45,44, 12, -8, -4, -12,26,5,  10, -8,1, MT.WAVY,      14,62, 4, EM.NORMAL, 350],
['PROUD',      50,35,44, 12,  0, -4, -10,30,6,   6,  6,1, MT.SMILE,     22,60,10, EM.NORMAL, 350],
['SHY',        48,42,44, 10,-10,  8, -10,24,5,  -4,  4,1, MT.SMILE,     12,62, 4, EM.NORMAL, 400],
['ANNOYED',    52,28,44, 12,  8,  2,  -4,30,7,  10, 10,1, MT.LINE,      16,60, 0, EM.NORMAL, 250],
['FOCUSED',    48,38,44, 14,  0,  0,  -8,28,6,   4,  4,1, MT.NONE,       0,62, 0, EM.NORMAL, 300],
['WINKING',    50,45,44, 13,  0,  0, -14,28,5,   6, 10,1, MT.SMILE,     20,60, 8, EM.WINK,   200],
['DEVIOUS',    50,30,44, 12,  6,  2,  -6,30,6,  12, 12,1, MT.TEETH,     22,60,10, EM.HALF,   300],
['SHOCKED',    55,52,46,  4,  0,  0, -20,26,5,  12, 12,1, MT.OPEN_O,    16,62,16, EM.DOT,    100],
['KISSING',    48,42,44, 12,  0, -2, -12,26,5,   6,  6,1, MT.POUT,      10,60, 8, EM.NORMAL, 250],
['NERVOUS',    50,48,44, 11, -4,  2, -14,26,5, -10,-10,1, MT.ZIGZAG,    18,64, 5, EM.NORMAL, 200],
['GLITCHING',  52,45,44, 13,  0,  0, -12,28,5,   0,  0,0, MT.WAVY,      20,62, 6, EM.GLITCH, 100],
['SASSY',      50,45,44, 13,  8, -2, -12,28,6,  -4, 14,1, MT.TONGUE,    20,60,10, EM.NORMAL, 250],
];

/** One BotExpression row, as the before renderer and FaceState read it. */
export interface Expr {
  name: string;
  eyeWhiteW: number; eyeWhiteH: number; eyeSpacing: number; pupilRadius: number; pupilOffsetX: number; pupilOffsetY: number;
  browOffsetY: number; browLength: number; browThickness: number; browAngleL: number; browAngleR: number; browVisible: boolean;
  mouthType: number; mouthWidth: number; mouthOffsetY: number; mouthCurve: number; eyeMode: number; transitionMs: number;
}
export const rowToExpr = (r: Row): Expr => ({ name: r[0], eyeWhiteW: r[1], eyeWhiteH: r[2], eyeSpacing: r[3], pupilRadius: r[4], pupilOffsetX: r[5], pupilOffsetY: r[6],
  browOffsetY: r[7], browLength: r[8], browThickness: r[9], browAngleL: r[10], browAngleR: r[11], browVisible: !!r[12],
  mouthType: r[13], mouthWidth: r[14], mouthOffsetY: r[15], mouthCurve: r[16], eyeMode: r[17], transitionMs: r[18] });

/** The expression table the lab runs: the shipped eye columns, with brows and mouths from the face files. */
export function exprTable(faces: Face[]): Expr[] {
  return faces.map((f) => {
    const base = rowToExpr(ROWS[f.index] || ROWS[0]);
    return {
      ...base, name: f.name,
      browOffsetY: f.brow.offsetY, browLength: f.brow.length, browThickness: f.brow.thickness,
      browAngleL: f.brow.angleL, browAngleR: f.brow.angleR, browVisible: f.brow.visible,
      mouthType: MOUTH_TYPES.indexOf(f.mouth.type), mouthWidth: f.mouth.width, mouthOffsetY: f.mouth.offsetY, mouthCurve: f.mouth.curve,
    };
  });
}

// ------------------------------------------------------------------ tween.h port
export const easeOutQuad = (t: number) => t * (2 - t);
export const easeInOutQuad = (t: number) => (t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t);
interface Tween { from: number; to: number; dur: number; ease: (t: number) => number; start: number; set: (v: number) => void }
export class Tweens {
  s = new Map<string, Tween>();
  start(key: string, from: number, to: number, dur: number, ease: (t: number) => number, now: number, set: (v: number) => void) { this.s.set(key, { from, to, dur, ease, start: now, set }); }
  active(key: string) { return this.s.has(key); }
  cancel(key: string) { this.s.delete(key); }
  update(now: number) {
    for (const [k, t] of this.s) {
      const el = now - t.start;
      if (el >= t.dur) { t.set(t.to); this.s.delete(k); }
      else t.set(t.from + (t.to - t.from) * t.ease(el / t.dur));
    }
  }
}

// ------------------------------------------------------------------ BotFaceState port (bot_faces.h:430-557)
const lerpI = (a: number, b: number, t: number) => a + (((b - a) * t) >> 8);
const LERP_FIELDS = ["eyeWhiteW", "eyeWhiteH", "eyeSpacing", "pupilRadius", "pupilOffsetX", "pupilOffsetY", "browOffsetY", "browLength", "browThickness", "browAngleL", "browAngleR", "mouthWidth", "mouthOffsetY", "mouthCurve"] as const;
const ALL_FIELDS = [...LERP_FIELDS, "browVisible", "mouthType", "eyeMode"] as const;

/** The blended face the before renderer draws (and the after side's mouth). */
export interface FaceDraw {
  eyeWhiteW: number; eyeWhiteH: number; eyeSpacing: number; pupilRadius: number; pupilOffsetX: number; pupilOffsetY: number;
  browOffsetY: number; browLength: number; browThickness: number; browAngleL: number; browAngleR: number; browVisible: boolean;
  mouthType: number; mouthWidth: number; mouthOffsetY: number; mouthCurve: number; eyeMode: number;
  dynamicPupilX: number; dynamicPupilY: number; blinkAmount: number;
}

export class FaceState implements FaceDraw {
  eyeWhiteW = 0; eyeWhiteH = 0; eyeSpacing = 0; pupilRadius = 0; pupilOffsetX = 0; pupilOffsetY = 0;
  browOffsetY = 0; browLength = 0; browThickness = 0; browAngleL = 0; browAngleR = 0; browVisible = true;
  mouthType = 0; mouthWidth = 0; mouthOffsetY = 0; mouthCurve = 0; eyeMode = 0;
  dynamicPupilX = 0; dynamicPupilY = 0; blinkAmount = 0; transitionProgress = 1; transitioning = false;
  currentExpr = 0; targetExpr = 0;
  constructor(private EXPR: Expr[], private tweens: Tweens) {}
  init() { this.dynamicPupilX = 0; this.dynamicPupilY = 0; this.blinkAmount = 0; this.transitionProgress = 1; this.transitioning = false; this.load(0); }
  load(i: number) {
    if (i >= this.EXPR.length) i = 0;
    const e = this.EXPR[i] as unknown as Record<string, number | boolean>, me = this as unknown as Record<string, number | boolean>;
    for (const k of ALL_FIELDS) me[k] = e[k];
    this.currentExpr = i; this.targetExpr = i; this.transitionProgress = 1; this.transitioning = false;
  }
  transitionTo(i: number, ms: number, now: number) {
    if (i >= this.EXPR.length) i = 0;
    if (i === this.targetExpr && !this.transitioning) return;
    if (!ms) ms = this.EXPR[i].transitionMs;
    this.currentExpr = this.targetExpr; // the previous *table entry*, not the blended state
    this.targetExpr = i; this.transitionProgress = 0; this.transitioning = true;
    this.tweens.start("transitionProgress", 0, 1, ms, easeOutQuad, now, (v) => { this.transitionProgress = v; });
  }
  update() {
    if (!this.transitioning) return;
    if (this.transitionProgress >= 1) { this.load(this.targetExpr); return; }
    const t = Math.trunc(this.transitionProgress * 255) & 255;
    const from = this.EXPR[this.currentExpr], to = this.EXPR[this.targetExpr];
    const me = this as unknown as Record<string, number>;
    for (const k of LERP_FIELDS) me[k] = lerpI(from[k], to[k], t);
    const past = t >= 128;
    this.browVisible = past ? to.browVisible : from.browVisible;
    this.mouthType = past ? to.mouthType : from.mouthType;
    this.eyeMode = past ? to.eyeMode : from.eyeMode;
  }
}

// BotBlinkState (bot_eyes.h:44-120)
export class BlinkState {
  next = 0; blinking = false; dbl = false; start = 0;
  init(now: number) { this.next = now + arduinoRandom(3000, 7000); this.blinking = false; this.dbl = false; }
  update(now: number) {
    const D = 150, GAP = 100;
    if (!this.blinking) {
      if (now >= this.next) { this.blinking = true; this.start = now; this.dbl = arduinoRandom(0, 100) < 15; }
      return 0;
    }
    const el = now - this.start, tri = (e: number) => { const t = e / (D / 2); return t < 1 ? t : 2 - t; };
    if (!this.dbl) {
      if (el >= D) { this.blinking = false; this.next = now + arduinoRandom(3000, 7000); return 0; }
      return tri(el);
    }
    if (el >= D + GAP + D) { this.blinking = false; this.next = now + arduinoRandom(3000, 7000); return 0; }
    if (el < D) return tri(el);
    if (el < D + GAP) return 0;
    return tri(el - D - GAP);
  }
}
// BotLookAround (bot_eyes.h:126-164)
export class LookAround {
  x = 0; y = 0; next = 0;
  constructor(private tweens: Tweens) {}
  init(now: number) { this.x = 0; this.y = 0; this.next = now + arduinoRandom(1000, 3000); }
  update(now: number): [number, number] {
    if (now >= this.next && !this.tweens.active("lookX")) {
      let tx = arduinoRandom(-20, 21), ty = arduinoRandom(-10, 11);
      if (arduinoRandom(0, 100) < 15) { tx = 0; ty = 0; }
      const dur = arduinoRandom(200, 500);
      this.tweens.start("lookX", this.x, tx, dur, easeInOutQuad, now, (v) => { this.x = v; });
      this.tweens.start("lookY", this.y, ty, dur, easeInOutQuad, now, (v) => { this.y = v; });
      this.next = now + dur + arduinoRandom(800, 2500);
    }
    return [Math.trunc(this.x), Math.trunc(this.y)];
  }
}
// BotIMUTracker (bot_eyes.h:170-195), fed by the look pad
export class IMUTracker {
  x = 0; y = 0;
  init() { this.x = 0; this.y = 0; }
  update(px: number, py: number): [number, number] {
    const rx = clamp(px * 24, -24, 24), ry = clamp(py * 24, -24, 24);
    this.x += (rx - this.x) * 0.2; this.y += (ry - this.y) * 0.2;
    return [Math.trunc(this.x), Math.trunc(this.y)];
  }
}

// ------------------------------------------------------------------ renderBotFace port (bot_eyes.h:397-1037)
function drawThickLine(g: Gfx, x0: number, y0: number, x1: number, y1: number, th: number, c: number) {
  for (let t = -idiv(th, 2); t <= idiv(th, 2); t++) {
    const dx = x1 - x0, dy = y1 - y0; let len = Math.sqrt(dx * dx + dy * dy); if (len < 1) len = 1;
    const nx = Math.trunc(-dy / len * t), ny = Math.trunc(dx / len * t);
    g.drawLine(x0 + nx, y0 + ny, x1 + nx, y1 + ny, c);
  }
}
function drawHeart(g: Gfx, cx: number, cy: number, size: number, c: number) {
  const r = idiv(size * 5, 12), ox = idiv(size, 3);
  g.fillCircle(cx - ox, cy - idiv(r, 3), r, c); g.fillCircle(cx + ox, cy - idiv(r, 3), r, c);
  g.fillTriangle(cx - size + 2, cy, cx + size - 2, cy, cx, cy + size, c);
}
function drawStar(g: Gfx, cx: number, cy: number, o: number, i: number, c: number) {
  g.fillTriangle(cx, cy - o, cx - i, cy, cx + i, cy, c); g.fillTriangle(cx, cy + o, cx - i, cy, cx + i, cy, c);
  g.fillTriangle(cx - o, cy, cx, cy - i, cx, cy + i, c); g.fillTriangle(cx + o, cy, cx, cy - i, cx, cy + i, c);
}
function drawSpiral(g: Gfx, cx: number, cy: number, radius: number, c: number, phase: number) {
  const maxA = 3 * PI, steps = 40;
  for (let i = 0; i < steps; i++) {
    const t = i / steps, a = t * maxA + phase, r = t * radius;
    g.fillCircle(cx + Math.trunc(Math.cos(a) * r), cy + Math.trunc(Math.sin(a) * r), 2, c);
  }
}
function drawXEye(g: Gfx, cx: number, cy: number, size: number, c: number) { const h = idiv(size, 2); drawThickLine(g, cx - h, cy - h, cx + h, cy + h, 5, c); drawThickLine(g, cx - h, cy + h, cx + h, cy - h, 5, c); }
function drawCaretEye(g: Gfx, cx: number, cy: number, w: number, h: number, fc: number, bg: number) {
  g.fillEllipse(cx, cy, w, h, fc);
  for (let x = -w + 4; x <= w - 4; x++) { const t = x / (w - 4); const y = -Math.trunc(h * 0.35 * (1 - t * t)); g.fillCircle(cx + x, cy + y, 2, bg); }
}
function drawClosedEye(g: Gfx, cx: number, cy: number, w: number, h: number, fc: number, bg: number) { g.fillEllipse(cx, cy, w, h, fc); const hw = w - 4; drawThickLine(g, cx - hw, cy, cx + hw, cy, 5, bg); }
function drawDiamondEye(g: Gfx, cx: number, cy: number, s: number, c: number) { g.fillTriangle(cx, cy - s, cx - s, cy, cx + s, cy, c); g.fillTriangle(cx, cy + s, cx - s, cy, cx + s, cy, c); }
function drawHalfEye(g: Gfx, cx: number, cy: number, w: number, h: number, fc: number, bg: number) {
  g.fillEllipse(cx, cy, w, h, fc);
  const lb = cy - Math.trunc(h * 0.2);
  g.fillRect(cx - w - 1, cy - h - 1, w * 2 + 2, lb - (cy - h) + 1, bg);
  for (let x = -w + 3; x <= w - 3; x++) { const t = x / (w - 3); g.fillCircle(cx + x, lb + Math.trunc(3 * (1 - t * t)), 2, fc); }
}
function drawCurvedEye(g: Gfx, cx: number, cy: number, w: number, h: number, fc: number, bg: number) {
  g.fillEllipse(cx, cy, w, h, fc);
  const cut = cy + Math.trunc(h * 0.1);
  g.fillRect(cx - w - 1, cy - h - 1, w * 2 + 2, cut - (cy - h) + 1, bg);
  g.fillEllipse(cx, cy + 4, w - 4, Math.trunc(h * 0.6), bg);
}
function drawGlitchEye(g: Gfx, cx: number, cy: number, w: number, h: number, fc: number, bg: number, now: number) {
  g.fillEllipse(cx, cy, w, h, fc);
  for (let i = 0; i < 3; i++) {
    const sy = cy - idiv(h, 2) + idiv(h * (i + 1), 4), sh = idiv(h, 6);
    const off = Math.trunc(Math.sin((now + i * 300) / 200) * 8);
    g.fillRect(cx - w + off, sy, w * 2, sh, bg);
  }
}

export function drawMouth(g: Gfx, f: FaceDraw, mcx: number, mcy: number, stroke: boolean, fc: number) {
  const S = 5, K = 0;
  const curveDots = (yf: (t: number) => number, r: number, c: number) => { for (let x = -f.mouthWidth; x <= f.mouthWidth; x++) { const t = x / f.mouthWidth; g.fillCircle(mcx + x, mcy + yf(t), r, c); } };
  const smile = (t: number) => Math.trunc(f.mouthCurve * (1 - t * t));
  switch (f.mouthType) {
    case MT.NONE: break;
    case MT.LINE:
      if (stroke) drawThickLine(g, mcx - f.mouthWidth, mcy, mcx + f.mouthWidth, mcy, 3 + S * 2, K);
      drawThickLine(g, mcx - f.mouthWidth, mcy, mcx + f.mouthWidth, mcy, 3, fc); break;
    case MT.SMILE: if (stroke) curveDots(smile, 2 + S, K); curveDots(smile, 2, fc); break;
    case MT.FROWN: { const fr = (t: number) => -Math.trunc(f.mouthCurve * (1 - t * t)); if (stroke) curveDots(fr, 2 + S, K); curveDots(fr, 2, fc); break; }
    case MT.OPEN_O:
      if (stroke) g.fillCircle(mcx, mcy, f.mouthCurve + S, K);
      g.fillCircle(mcx, mcy, f.mouthCurve, fc); g.fillCircle(mcx, mcy, f.mouthCurve - 3, K); break;
    case MT.GRIN:
      if (stroke) curveDots(smile, 2 + S, K); curveDots(smile, 2, fc);
      drawThickLine(g, mcx - f.mouthWidth + 4, mcy + 2, mcx + f.mouthWidth - 4, mcy + 2, 2, K); break;
    case MT.WAVY: { const wv = (t: number) => Math.trunc(Math.sin(t * PI * 3) * f.mouthCurve); if (stroke) curveDots(wv, 2 + S, K); curveDots(wv, 2, fc); break; }
    case MT.SMIRK: { const sm = (t: number) => { const n = (t + 1) / 2; return Math.trunc(f.mouthCurve * (1 - n) * (1 - n)); }; if (stroke) curveDots(sm, 2 + S, K); curveDots(sm, 2, fc); break; }
    case MT.TEETH: {
      if (stroke) curveDots(smile, 2 + S, K); curveDots(smile, 2, fc);
      const tt = mcy + 2, th = 8, tw = idiv(f.mouthWidth * 3, 2);
      if (stroke) g.fillRect(mcx - tw - S, tt - S, tw * 2 + S * 2, th + S * 2, K);
      g.fillRect(mcx - tw, tt, tw * 2, th, fc);
      for (let tx = mcx - tw + 6; tx < mcx + tw; tx += 7) g.drawFastVLine(tx, tt, th, K);
      break;
    }
    case MT.TONGUE: {
      const r = f.mouthCurve + 2;
      if (stroke) g.fillCircle(mcx, mcy, r + S, K);
      g.fillCircle(mcx, mcy, r, fc); g.fillCircle(mcx, mcy, r - 3, K);
      g.fillRect(mcx - r - 1, mcy - r - 1, r * 2 + 2, r, 0);
      drawThickLine(g, mcx - f.mouthWidth, mcy, mcx + f.mouthWidth, mcy, 3, fc);
      g.fillEllipse(mcx, mcy + idiv(r, 2), idiv(f.mouthWidth, 2), idiv(r, 3) + 1, 0xF8A0);
      break;
    }
    case MT.ZIGZAG: {
      const sw = idiv(f.mouthWidth * 2, 6), sx = mcx - f.mouthWidth;
      for (const pass of stroke ? [0, 1] : [1]) for (let i = 0; i < 6; i++) {
        const x0 = sx + i * sw, x1 = x0 + sw, y0 = mcy + (i % 2 === 0 ? -f.mouthCurve : f.mouthCurve), y1 = mcy + (i % 2 === 0 ? f.mouthCurve : -f.mouthCurve);
        drawThickLine(g, x0, y0, x1, y1, pass ? 3 : 3 + S * 2, pass ? fc : K);
      }
      break;
    }
    case MT.POUT: { const r = f.mouthCurve; if (stroke) g.fillCircle(mcx, mcy, r + S, K); g.fillCircle(mcx, mcy, r, fc); g.fillCircle(mcx, mcy, r - 3, K); break; }
    case MT.WHISTLE: { if (stroke) g.fillCircle(mcx, mcy, 5 + S, K); g.fillCircle(mcx, mcy, 5, fc); g.fillCircle(mcx, mcy, 3, K); break; }
    case MT.FLAT_FROWN: {
      const hw = f.mouthWidth, dip = idiv(f.mouthCurve, 2);
      if (stroke) { drawThickLine(g, mcx - hw, mcy, mcx, mcy + dip, 3 + S * 2, K); drawThickLine(g, mcx, mcy + dip, mcx + hw, mcy, 3 + S * 2, K); }
      drawThickLine(g, mcx - hw, mcy, mcx, mcy + dip, 3, fc); drawThickLine(g, mcx, mcy + dip, mcx + hw, mcy, 3, fc); break;
    }
  }
}

export interface BeforeOpts { fc: number; stroke: boolean; now: number; brows: boolean; mouth: boolean; pupils: boolean }

export function renderBotFace(g: Gfx, face: FaceDraw, bg: number, cx: number, cy: number, o: BeforeOpts) {
  const S = 5, K = 0, fc = o.fc, stroke = o.stroke;
  const fpx = face.pupilOffsetX + face.dynamicPupilX, fpy = face.pupilOffsetY + face.dynamicPupilY;
  const L = cx - face.eyeSpacing, R = cx + face.eyeSpacing, W = face.eyeWhiteW, H = face.eyeWhiteH, pr = face.pupilRadius;
  let eH = H;
  if (face.blinkAmount > 0.01) { eH = Math.trunc(H * (1 - face.blinkAmount)); if (eH < 2) eH = 2; }
  const whites = (h: number) => { if (stroke) { g.fillEllipse(L, cy, W + S, h + S, K); g.fillEllipse(R, cy, W + S, h + S, K); } g.fillEllipse(L, cy, W, h, fc); g.fillEllipse(R, cy, W, h, fc); };
  const pclamp = (): [number, number] => { const mx = W - pr - 4, my = eH - pr - 4; return [cons(fpx, -mx, mx), cons(fpy, -my, my)]; };
  const pupils = o.pupils;
  switch (face.eyeMode) {
    case EM.NORMAL: whites(eH); if (eH > 8 && pupils) { const [x, y] = pclamp(); g.fillCircle(L + x, cy + y, pr, K); g.fillCircle(R + x, cy + y, pr, K); } break;
    case EM.CARET:
      if (stroke) { g.fillEllipse(L, cy, W + S, H + S, K); g.fillEllipse(R, cy, W + S, H + S, K); }
      drawCaretEye(g, L, cy, W, H, fc, bg); drawCaretEye(g, R, cy, W, H, fc, bg); break;
    case EM.HEART: whites(eH); if (eH > 8 && pupils) { const [x, y] = pclamp(); drawHeart(g, L + x, cy + y, pr + 4, K); drawHeart(g, R + x, cy + y, pr + 4, K); } break;
    case EM.X: whites(eH); if (eH > 8 && pupils) { const [x, y] = pclamp(); drawXEye(g, L + x, cy + y, pr + 2, K); drawXEye(g, R + x, cy + y, pr + 2, K); } break;
    case EM.SPIRAL: {
      whites(eH);
      if (pupils) { const ph = (o.now % 2000) / 2000 * 2 * PI, r = Math.min(W, eH) - 6; drawSpiral(g, L, cy, r, K, ph); drawSpiral(g, R, cy, r, K, -ph); }
      break;
    }
    case EM.STAR: whites(eH); if (eH > 8 && pupils) { const [x, y] = pclamp(); const so = pr + 4, si = idiv(so * 2, 5); drawStar(g, L + x, cy + y, so, si, K); drawStar(g, R + x, cy + y, so, si, K); } break;
    case EM.CLOSED:
      if (stroke) { g.fillEllipse(L, cy, W + S, H + S, K); g.fillEllipse(R, cy, W + S, H + S, K); }
      drawClosedEye(g, L, cy, W, H, fc, bg); drawClosedEye(g, R, cy, W, H, fc, bg); break;
    case EM.DIAMOND: whites(eH); if (eH > 8 && pupils) { const [x, y] = pclamp(); drawDiamondEye(g, L + x, cy + y, pr, K); drawDiamondEye(g, R + x, cy + y, pr, K); } break;
    case EM.HALF:
      if (stroke) { g.fillEllipse(L, cy, W + S, eH + S, K); g.fillEllipse(R, cy, W + S, eH + S, K); }
      drawHalfEye(g, L, cy, W, eH, fc, bg); drawHalfEye(g, R, cy, W, eH, fc, bg);
      if (eH > 8 && pupils) { const mx = W - pr - 4, x = cons(fpx, -mx, mx); g.fillCircle(L + x, cy + 4, pr, K); g.fillCircle(R + x, cy + 4, pr, K); }
      break;
    case EM.DOT:
      whites(eH);
      if (eH > 8 && pupils) { g.fillCircle(L, cy, 4, K); g.fillCircle(R, cy, 4, K); g.fillCircle(L + 2, cy - 2, 1, fc); g.fillCircle(R + 2, cy - 2, 1, fc); }
      break;
    case EM.CURVED:
      if (stroke) { g.fillEllipse(L, cy, W + S, H + S, K); g.fillEllipse(R, cy, W + S, H + S, K); }
      drawCurvedEye(g, L, cy, W, H, fc, bg); drawCurvedEye(g, R, cy, W, H, fc, bg); break;
    case EM.WINK:
      if (stroke) { g.fillEllipse(L, cy, W + S, eH + S, K); g.fillEllipse(R, cy, W + S, H + S, K); }
      g.fillEllipse(L, cy, W, eH, fc);
      if (eH > 8 && pupils) { const [x, y] = pclamp(); g.fillCircle(L + x, cy + y, pr, K); }
      drawClosedEye(g, R, cy, W, H, fc, bg); break;
    case EM.GLITCH:
      if (stroke) { g.fillEllipse(L, cy, W + S, eH + S, K); g.fillEllipse(R, cy, W + S, eH + S, K); }
      drawGlitchEye(g, L, cy, W, eH, fc, bg, o.now); drawGlitchEye(g, R, cy, W, eH, fc, bg, o.now); break;
  }
  if (o.brows && face.browVisible && face.blinkAmount < 0.8) {
    const by = cy - eH + face.browOffsetY, bl = face.browLength, bt = face.browThickness;
    const rl = face.browAngleL * PI / 180, rr = face.browAngleR * PI / 180;
    const l0 = by + Math.trunc(Math.sin(-rl) * bl), l1 = by + Math.trunc(Math.sin(rl) * bl);
    if (stroke) drawThickLine(g, L - bl, l0, L + bl, l1, bt + S * 2, K);
    drawThickLine(g, L - bl, l0, L + bl, l1, bt, fc);
    const r0 = by + Math.trunc(Math.sin(rr) * bl), r1 = by + Math.trunc(Math.sin(-rr) * bl);
    if (stroke) drawThickLine(g, R - bl, r0, R + bl, r1, bt + S * 2, K);
    drawThickLine(g, R - bl, r0, R + bl, r1, bt, fc);
  }
  if (o.mouth) drawMouth(g, face, cx, cy + face.mouthOffsetY, stroke, fc);
}
// renderBotFaceAt (bot_mode.h) scaling of a face copy
export function scaledFace(src: FaceDraw, s: number): FaceDraw {
  const f: FaceDraw = { ...pickFace(src) }, sc = (v: number) => lround(v * s);
  f.eyeWhiteW = sc(f.eyeWhiteW); f.eyeWhiteH = sc(f.eyeWhiteH); f.eyeSpacing = sc(f.eyeSpacing);
  f.pupilRadius = Math.max(2, sc(f.pupilRadius)); f.pupilOffsetX = sc(f.pupilOffsetX); f.pupilOffsetY = sc(f.pupilOffsetY);
  f.dynamicPupilX = sc(f.dynamicPupilX); f.dynamicPupilY = sc(f.dynamicPupilY);
  f.browOffsetY = sc(f.browOffsetY); f.browLength = sc(f.browLength); f.browThickness = Math.max(1, sc(f.browThickness));
  f.mouthWidth = sc(f.mouthWidth); f.mouthOffsetY = sc(f.mouthOffsetY); f.mouthCurve = sc(f.mouthCurve);
  return f;
}
const pickFace = (s: FaceDraw): FaceDraw => ({
  eyeWhiteW: s.eyeWhiteW, eyeWhiteH: s.eyeWhiteH, eyeSpacing: s.eyeSpacing, pupilRadius: s.pupilRadius, pupilOffsetX: s.pupilOffsetX, pupilOffsetY: s.pupilOffsetY,
  browOffsetY: s.browOffsetY, browLength: s.browLength, browThickness: s.browThickness, browAngleL: s.browAngleL, browAngleR: s.browAngleR, browVisible: s.browVisible,
  mouthType: s.mouthType, mouthWidth: s.mouthWidth, mouthOffsetY: s.mouthOffsetY, mouthCurve: s.mouthCurve, eyeMode: s.eyeMode,
  dynamicPupilX: s.dynamicPupilX, dynamicPupilY: s.dynamicPupilY, blinkAmount: s.blinkAmount,
});
const ZGLYPH = [0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111];
export function drawZzz(g: Gfx, cx: number, cy: number, now: number, fc: number) {
  const t = (now % 3000) / 3000;
  for (let i = 0; i < 3; i++) {
    const ph = (t + i * 0.33) % 1; if (ph >= 0.8) continue;
    const zx = cx + 50 + i * 12 + Math.trunc(Math.sin(ph * PI * 2) * 4), zy = cy - 40 - Math.trunc(ph * 50), sz = 1 + i;
    for (let r = 0; r < 7; r++) for (let c = 0; c < 5; c++) if (ZGLYPH[r] & (16 >> c)) g.fillRect(zx + c * sz, zy + r * sz, sz, sz, fc);
  }
}

