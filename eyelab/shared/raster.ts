// Device-pixel raster shared by both renderers: an RGB565 frame buffer with the
// TFT_eSPI/LovyanGFX-style primitives the firmware calls, plus the small math helpers
// the eye code uses. Ported verbatim from the artifact eye lab; the firmware is diffed
// against this, so the integer rounding here is load-bearing.

export const clamp = (v: number, a: number, b: number) => (v < a ? a : v > b ? b : v);
export const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
export const lround = (v: number) => (v < 0 ? -Math.round(-v) : Math.round(v));
export const idiv = (a: number, b: number) => Math.trunc(a / b);
export const rnd = (a: number, b: number) => a + Math.random() * (b - a);
export const arduinoRandom = (a: number, b: number) => a + Math.floor(Math.random() * (b - a)); // random(min,max): [min,max)
export const smooth = (x: number) => (x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x));
export const bump = (p: number, a: number, b: number) => clamp((p - a) / (b - a), 0, 1);
export const cons = (x: number, a: number, b: number) => (x < a ? a : x > b ? b : x); // Arduino constrain
export const hex4 = (c: number) => "0x" + c.toString(16).toUpperCase().padStart(4, "0");
export const f2 = (v: number) => (Math.abs(v) < 0.005 ? 0 : v).toFixed(2);
export const near = (a: number, b: number) => Math.abs(a - b) < 1e-6;
export const PI = Math.PI;

// RGB565 -> RGBA (little-endian Uint32)
export const LUT = new Uint32Array(65536);
for (let c = 0; c < 65536; c++) {
  const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  const R = (r * 527 + 23) >> 6, G = (g * 259 + 33) >> 6, B = (b * 527 + 23) >> 6;
  LUT[c] = ((255 << 24) | (B << 16) | (G << 8) | R) >>> 0;
}
export const rgb565 = (r: number, g: number, b: number) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

// ------------------------------------------------------------------ raster (device pixels, RGB565)
export class Frame {
  w: number;
  h: number;
  px: Uint16Array;
  constructor(w: number, h: number) { this.w = w; this.h = h; this.px = new Uint16Array(w * h); }
  clear(c: number) { this.px.fill(c); }
  pset(x: number, y: number, c: number) { if (x >= 0 && y >= 0 && x < this.w && y < this.h) this.px[y * this.w + x] = c; }
  hline(x: number, y: number, w: number, c: number) {
    if (y < 0 || y >= this.h || w <= 0) return;
    const a = Math.max(0, x), b = Math.min(this.w, x + w), o = y * this.w;
    for (let i = a; i < b; i++) this.px[o + i] = c;
  }
  vline(x: number, y: number, h: number, c: number) {
    if (x < 0 || x >= this.w || h <= 0) return;
    const a = Math.max(0, y), b = Math.min(this.h, y + h);
    for (let j = a; j < b; j++) this.px[j * this.w + x] = c;
  }
  fillRect(x: number, y: number, w: number, h: number, c: number) { for (let j = 0; j < h; j++) this.hline(x, y + j, w, c); }
  // Adafruit/LovyanGFX-style midpoint circle fill
  fillCircle(x0: number, y0: number, r: number, c: number) {
    this.vline(x0, y0 - r, 2 * r + 1, c);
    let f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r, px = x, py = y;
    while (x < y) {
      if (f >= 0) { y--; ddy += 2; f += ddy; }
      x++; ddx += 2; f += ddx;
      if (x < y + 1) { this.vline(x0 + x, y0 - y, 2 * y + 1, c); this.vline(x0 - x, y0 - y, 2 * y + 1, c); }
      if (y !== py) { this.vline(x0 + py, y0 - px, 2 * px + 1, c); this.vline(x0 - py, y0 - px, 2 * px + 1, c); py = y; }
      px = x;
    }
  }
  // TFT_eSPI / LovyanGFX-style ellipse fill
  fillEllipse(x0: number, y0: number, rx: number, ry: number, c: number) {
    if (rx < 0 || ry < 0) return;
    if (rx === 0) { this.vline(x0, y0 - ry, 2 * ry + 1, c); return; }
    if (ry === 0) { this.hline(x0 - rx, y0, 2 * rx + 1, c); return; }
    const rx2 = rx * rx, ry2 = ry * ry, fx2 = 4 * rx2, fy2 = 4 * ry2;
    let x: number, y: number, s: number;
    for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++) {
      this.hline(x0 - x, y0 - y, 2 * x + 1, c); this.hline(x0 - x, y0 + y, 2 * x + 1, c);
      if (s >= 0) { s += fx2 * (1 - y); y--; }
      s += ry2 * (4 * x + 6);
    }
    for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++) {
      this.hline(x0 - x, y0 - y, 2 * x + 1, c); this.hline(x0 - x, y0 + y, 2 * x + 1, c);
      if (s >= 0) { s += fy2 * (1 - x); x--; }
      s += rx2 * (4 * y + 6);
    }
  }
  line(x0: number, y0: number, x1: number, y1: number, c: number) {
    const steep = Math.abs(y1 - y0) > Math.abs(x1 - x0);
    if (steep) { [x0, y0] = [y0, x0]; [x1, y1] = [y1, x1]; }
    if (x0 > x1) { [x0, x1] = [x1, x0]; [y0, y1] = [y1, y0]; }
    const dx = x1 - x0, dy = Math.abs(y1 - y0), ys = y0 < y1 ? 1 : -1;
    let err = idiv(dx, 2);
    for (; x0 <= x1; x0++) {
      if (steep) this.pset(y0, x0, c); else this.pset(x0, y0, c);
      err -= dy; if (err < 0) { y0 += ys; err += dx; }
    }
  }
  fillTriangle(x0: number, y0: number, x1: number, y1: number, x2: number, y2: number, c: number) {
    if (y0 > y1) { [y0, y1] = [y1, y0]; [x0, x1] = [x1, x0]; }
    if (y1 > y2) { [y2, y1] = [y1, y2]; [x2, x1] = [x1, x2]; }
    if (y0 > y1) { [y0, y1] = [y1, y0]; [x0, x1] = [x1, x0]; }
    if (y0 === y2) {
      let a = x0, b = x0;
      if (x1 < a) a = x1; else if (x1 > b) b = x1;
      if (x2 < a) a = x2; else if (x2 > b) b = x2;
      this.hline(a, y0, b - a + 1, c); return;
    }
    const dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
    let sa = 0, sb = 0, y: number;
    const last = (y1 === y2) ? y1 : y1 - 1;
    for (y = y0; y <= last; y++) {
      let a = x0 + idiv(sa, dy01), b = x0 + idiv(sb, dy02);
      sa += dx01; sb += dx02; if (a > b) [a, b] = [b, a];
      this.hline(a, y, b - a + 1, c);
    }
    sa = dx12 * (y - y1); sb = dx02 * (y - y0);
    for (; y <= y2; y++) {
      let a = x1 + idiv(sa, dy12), b = x0 + idiv(sb, dy02);
      sa += dx12; sb += dx02; if (a > b) [a, b] = [b, a];
      this.hline(a, y, b - a + 1, c);
    }
  }
}

/** vizmini/gfx_mono.h mapping: design-space coords scaled + translated, optional 1-bit colour. */
export interface GfxMap { mono?: boolean; s: number; ox: number; oy: number; dcx: number; dcy: number }

// The six primitives bot_eyes.h calls. With a map it behaves like vizmini/gfx_mono.h:
// design-space coords scaled + translated, colour 0x0000 clears, anything else lights.
export class Gfx {
  f: Frame;
  m: GfxMap | null;
  last: number;
  cols: Set<number>;
  constructor(fr: Frame, map?: GfxMap) { this.f = fr; this.m = map || null; this.last = 0xFFFF; this.cols = new Set(); }
  col(c: number) { if (this.m && this.m.mono) c = c === 0 ? 0 : 0xFFFF; this.last = c; this.cols.add(c); return c; }
  sx(x: number) { return this.m ? this.m.ox + lround((x - this.m.dcx) * this.m.s) : x; }
  sy(y: number) { return this.m ? this.m.oy + lround((y - this.m.dcy) * this.m.s) : y; }
  sr(r: number) { return this.m ? lround(r * this.m.s) : r; }
  fillEllipse(cx: number, cy: number, rx: number, ry: number, c: number) {
    if (this.m) { rx = this.sr(rx); ry = this.sr(ry); if (rx < 1 || ry < 1) return; }
    this.f.fillEllipse(this.sx(cx), this.sy(cy), rx, ry, this.col(c));
  }
  fillCircle(cx: number, cy: number, r: number, c: number) {
    if (this.m) {
      const s = this.sr(r);
      if (s < 1) { this.f.pset(this.sx(cx), this.sy(cy), this.last); return; } // gfx_mono.h:58 quirk: pixel before colour
      r = s;
    }
    this.f.fillCircle(this.sx(cx), this.sy(cy), r, this.col(c));
  }
  drawLine(x0: number, y0: number, x1: number, y1: number, c: number) { this.f.line(this.sx(x0), this.sy(y0), this.sx(x1), this.sy(y1), this.col(c)); }
  fillRect(x: number, y: number, w: number, h: number, c: number) {
    if (this.m) { const sw = this.sr(w), sh = this.sr(h); if (sw < 1 || sh < 1) return; this.f.fillRect(this.sx(x), this.sy(y), sw, sh, this.col(c)); return; }
    this.f.fillRect(x, y, w, h, this.col(c));
  }
  fillTriangle(x0: number, y0: number, x1: number, y1: number, x2: number, y2: number, c: number) {
    this.f.fillTriangle(this.sx(x0), this.sy(y0), this.sx(x1), this.sy(y1), this.sx(x2), this.sy(y2), this.col(c));
  }
  drawFastVLine(x: number, y: number, h: number, c: number) {
    if (this.m) { const sh = this.sr(h); if (sh < 1) return; this.f.vline(this.sx(x), this.sy(y), sh, this.col(c)); return; }
    this.f.vline(x, y, h, this.col(c));
  }
}
