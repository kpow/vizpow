// Drives the shared lab model in the browser: the fixed-rate frame loop, the two canvases,
// face selection, the P1 tour and replay, the look pad, and the display toggles. React reads
// its state to draw the panels; the loop itself never goes through React. Also publishes
// window.__eyelab for test scripts, with the artifact lab's API.

import type { Face, Globals } from "../../shared/faces";
import { Frame, LUT, clamp } from "../../shared/raster";
import { createLab, renderAfterInto, renderBeforeInto, stepBefore } from "../../shared/lab";
import type { Lab } from "../../shared/state";
import { dev } from "../../shared/state";
import type { AfterMetrics } from "../../shared/model";
import { afterAtRest, afterSetExpr, requestSwap, stepAfter } from "../../shared/motion";
import { P1, iouInfo, measure, resetMonitors } from "../../shared/measure";
import { near } from "../../shared/raster";
import { EYE_KEYS } from "../../shared/faces";

export interface Canvases { cvB: HTMLCanvasElement; cvA: HTMLCanvasElement; dimB: HTMLElement; dimA: HTMLElement }
type TourState = { mode: "p1"; i: number; next: number } | { mode: "pairs"; pairs: number[][]; i: number; phase: number; next: number };
export interface SetExprOpts { instant?: boolean; fromTour?: boolean; label?: string }
type UiKey = "device" | "scale" | "fps" | "speed" | "faceColor" | "idleLife" | "showBrows" | "showMouth" | "showPupils" | "mono" | "ambient" | "sleep" | "legacy" | "catchlight" | "pendant";

export class LabRunner {
  L: Lab;
  simNow = 0;
  selected = 0;
  lastFrom = 0;
  tourState: TourState | null = null;
  tourMode: "p1" | "pairs" = "p1";
  tourStatus = "";
  schedule: { at: number; fn: () => void }[] = [];
  linked: Record<string, boolean> = {};
  lastMetrics: AfterMetrics | null = null;
  colsB = new Set<number>();
  colsA = new Set<number>();
  pushed = 0;
  skipped = 0;
  forceRender = true;
  private frB: Frame | null = null;
  private frA: Frame | null = null;
  private imgB: ImageData | null = null;
  private imgA: ImageData | null = null;
  private cv: Canvases | null = null;
  private lastReal = 0;
  private acc = 0;
  private uiTick = 0;
  private raf = 0;
  private listeners = new Set<() => void>();

  constructor(faces: Face[], globals: Globals) {
    this.L = createLab(faces, globals);
    try { if (window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches) this.L.ui.idle = false; } catch { /* no matchMedia */ }
    this.setLinks();
  }

  /** React subscribes here; fired on selection changes and every 4th frame for the readouts. */
  subscribe(fn: () => void) { this.listeners.add(fn); return () => { this.listeners.delete(fn); }; }
  emit() { for (const fn of this.listeners) fn(); }

  attach(cv: Canvases) {
    this.cv = cv; this.frB = null;
    this.ensureFrames(); this.layoutCanvases();
    const onResize = () => this.layoutCanvases();
    window.addEventListener("resize", onResize);
    this.raf = requestAnimationFrame((t) => { this.lastReal = t; this.raf = requestAnimationFrame(this.tick); });
    return () => { window.removeEventListener("resize", onResize); cancelAnimationFrame(this.raf); this.cv = null; };
  }

  // ------------------------------------------------------------------ frame render
  get frames() { return [this.frB, this.frA]; }
  ensureFrames() {
    const D = dev(this.L), cv = this.cv;
    if (!cv) return;
    if (!this.frB || this.frB.w !== D.w || this.frB.h !== D.h) {
      this.frB = new Frame(D.w, D.h); this.frA = new Frame(D.w, D.h);
      cv.cvB.width = cv.cvA.width = D.w; cv.cvB.height = cv.cvA.height = D.h;
      this.imgB = cv.cvB.getContext("2d")!.createImageData(D.w, D.h); this.imgA = cv.cvA.getContext("2d")!.createImageData(D.w, D.h);
      this.layoutCanvases();
    }
  }
  private blit(fr: Frame, img: ImageData, cv: HTMLCanvasElement) {
    const out = new Uint32Array(img.data.buffer);
    for (let i = 0; i < fr.px.length; i++) out[i] = LUT[fr.px[i]];
    cv.getContext("2d")!.putImageData(img, 0, 0);
  }
  renderBefore(now: number) {
    if (!this.frB || !this.cv) return;
    this.colsB = renderBeforeInto(this.L, this.frB, now);
    this.blit(this.frB, this.imgB!, this.cv.cvB);
  }
  renderAfterFrame(now: number): AfterMetrics | null {
    if (!this.frA || !this.cv) return null;
    const { m, cols } = renderAfterInto(this.L, this.frA, now);
    this.colsA = cols;
    this.blit(this.frA, this.imgA!, this.cv.cvA);
    return m;
  }
  layoutCanvases() {
    const cv = this.cv; if (!cv) return;
    const D = dev(this.L), ui = this.L.ui;
    for (const [c, dim] of [[cv.cvB, cv.dimB], [cv.cvA, cv.dimA]] as const) {
      const box = c.parentElement!, avail = Math.max(60, box.clientWidth - 18);
      let w: number, h: number, label: string;
      if (ui.scale === "actual") { const s = 96 / D.ppi; w = D.w * s; h = D.h * s; label = `actual size at ${D.ppi} ppi (assumes 96 CSS px per inch)`; }
      else {
        let s = ui.scale === "auto" ? Math.floor(avail / D.w) : +ui.scale;
        if (ui.scale === "auto") s = Math.min(s, Math.max(1, Math.floor((window.innerHeight * .62) / D.h)), D.w <= 128 ? 6 : 3);
        s = Math.max(1, s);
        while (s > 1 && D.w * s > avail) s--;
        w = D.w * s; h = D.h * s; label = `${s}×`;
      }
      c.style.width = w + "px"; c.style.height = h + "px";
      dim.textContent = `${D.label} · ${label}`;
    }
  }

  // ------------------------------------------------------------------ main loop
  private tick = (ts: number) => {
    const L = this.L, ui = L.ui;
    const dReal = Math.min(250, ts - this.lastReal); this.lastReal = ts;
    const frameMs = 1000 / ui.fps;
    this.acc += dReal;
    if (this.acc >= frameMs) {
      const steps = Math.min(3, Math.floor(this.acc / frameMs)); this.acc -= steps * frameMs; if (this.acc > frameMs) this.acc = 0;
      this.ensureFrames();
      for (let s = 0; s < steps; s++) {
        this.simNow += frameMs * ui.speed;
        for (let j = this.schedule.length - 1; j >= 0; j--) if (this.simNow >= this.schedule[j].at) { const fn = this.schedule[j].fn; this.schedule.splice(j, 1); fn(); }
        this.tourStep(this.simNow);
        stepBefore(L, this.simNow);
        stepAfter(L, this.simNow, frameMs * ui.speed / 1000);
      }
      this.renderBefore(this.simNow);
      if (ui.pendant && !this.forceRender && afterAtRest(L)) this.skipped++;
      else { const m = this.renderAfterFrame(this.simNow); if (m) { this.lastMetrics = m; this.pushed++; measure(L, this.simNow, m); } }
      this.forceRender = false;
      if ((++this.uiTick & 3) === 0) this.emit();
    }
    this.raf = requestAnimationFrame(this.tick);
  };

  setExpr(i: number, opts: SetExprOpts = {}) {
    const L = this.L, now = this.simNow, f = L.BF.face;
    if (i !== f.targetExpr || f.transitioning) this.lastFrom = f.targetExpr;
    if (opts.instant) { L.tweens.cancel("transitionProgress"); f.load(i); afterSetExpr(L, i, now, true); L.mon.skipNext = true; }
    else { f.transitionTo(i, 0, now); afterSetExpr(L, i, now, false); }
    if (i === 0 || this.selected === 0) L.stats.since = now;
    this.selected = i;
    if (!opts.fromTour) L.lastInteract = now;
    L.mon.label = opts.label || "";
    this.setLinks();
    this.emit();
  }

  // ------------------------------------------------------------------ tour
  tourStart() {
    resetMonitors(this.L);
    if (this.tourMode === "p1") this.tourState = { mode: "p1", i: 0, next: this.simNow };
    else {
      const pairs: number[][] = []; for (const a of P1) for (const b of P1) if (a !== b) pairs.push([a, b]);
      this.tourState = { mode: "pairs", pairs, i: 0, phase: 0, next: this.simNow };
    }
    this.emit();
  }
  tourStop(msg?: string) { this.tourState = null; this.tourStatus = msg || ""; this.emit(); }
  private tourStep(now: number) {
    const T = this.tourState, L = this.L; if (!T || now < T.next) return;
    const name = (i: number) => L.EXPR[i].name;
    if (T.mode === "p1") {
      const e = P1[T.i % P1.length]; this.setExpr(e, { fromTour: true });
      this.tourStatus = `P1 ${T.i % P1.length + 1}/8: ${name(e)}`; T.i++; T.next = now + 2600; return;
    }
    if (T.i >= T.pairs.length) { this.tourStop(`Done: 56 pairs. Worst area step ${Math.round(L.mon.maxStep * 100)}% (${L.mon.worst || "n/a"}).`); return; }
    const [a, b] = T.pairs[T.i];
    if (T.phase === 0) { this.setExpr(a, { instant: true, fromTour: true }); T.phase = 1; T.next = now + 700; }
    else { this.setExpr(b, { fromTour: true, label: `${name(a)}→${name(b)}` }); this.tourStatus = `pair ${T.i + 1}/56: ${name(a)} → ${name(b)}`; T.phase = 0; T.i++; T.next = now + 1900; }
  }
  replay() {
    const to = this.selected, from = this.lastFrom === to ? 0 : this.lastFrom; if (from === to) return;
    if (this.tourState) this.tourStop();
    const name = (i: number) => this.L.EXPR[i].name;
    this.setExpr(from, { instant: true });
    this.schedule.push({ at: this.simNow + 500, fn: () => this.setExpr(to, { label: `${name(from)}→${name(to)}` }) });
    this.tourStatus = `Replaying ${name(from)} → ${name(to)}`;
    this.emit();
  }

  // ------------------------------------------------------------------ controls
  /** A display toggle, with the side effects the artifact's change handlers had. */
  setUi(key: UiKey, v: string | boolean) {
    const ui = this.L.ui, L = this.L;
    switch (key) {
      case "device": ui.device = String(v); this.ensureFrames(); this.layoutCanvases(); break;
      case "scale": ui.scale = String(v); this.layoutCanvases(); break;
      case "fps": ui.fps = +v; break;
      case "speed": ui.speed = +v; break;
      case "faceColor": ui.fc = +v; break;
      case "idleLife": ui.idle = !!v; L.stats.since = this.simNow; break;
      case "showBrows": ui.brows = !!v; break;
      case "showMouth": ui.mouth = !!v; break;
      case "showPupils": ui.pupils = !!v; break;
      case "mono": ui.mono = !!v; this.ensureFrames(); this.layoutCanvases(); break;
      case "ambient": ui.ambient = !!v; break;
      case "sleep": ui.sleep = !!v; break;
      case "legacy": ui.legacy = !!v; break;
      case "catchlight": ui.catchlight = !!v; break;
      case "pendant": ui.pendant = !!v; this.pushed = this.skipped = 0; break;
    }
    this.forceRender = true;
    this.emit();
  }
  padSet(x: number, y: number) {
    const pad = this.L.pad;
    pad.x = clamp(x, -1, 1); pad.y = clamp(y, -1, 1); this.L.lastInteract = this.simNow;
    this.emit();
  }
  resetMeasurements() {
    const stats = this.L.stats;
    resetMonitors(this.L); stats.since = this.simNow; stats.gaze.length = stats.blinks.length = stats.fidgets.length = 0;
    this.emit();
  }

  // ------------------------------------------------------------------ params
  /** Params changed: silhouettes and the next frame are stale. */
  paramsChanged() { this.L.silCache = null; this.forceRender = true; this.emit(); }
  setLinks() { const P = this.L.AP[this.selected]; for (const k of EYE_KEYS) this.linked[k] = near(P[k][0], P[k][1]); }
  /** Keep the shown face in step when params change outside the overlay select (reset). */
  syncOverlay() {
    const L = this.L, P = L.AP[L.AS.expr];
    if (P.overlay !== L.AS.overlay && !L.AS.pending) requestSwap(L, this.simNow, { overlay: P.overlay, brows: L.EXPR[L.AS.expr].browVisible });
  }
  swapOverlay(overlay: string) {
    const L = this.L;
    if (this.selected === L.AS.expr) requestSwap(L, this.simNow, { overlay, brows: L.EXPR[this.selected].browVisible });
  }

  /** The artifact's window.__eyelab, for test scripts. */
  publish(extra: Record<string, unknown>) {
    const self = this, L = this.L;
    (window as unknown as { __eyelab: unknown }).__eyelab = {
      AS: L.AS, BF: L.BF, AP: L.AP, GP: L.GP, ui: L.ui, mon: L.mon, stats: L.stats, lab: L,
      measure: (now: number, m: AfterMetrics) => measure(L, now, m),
      resetMonitors: () => resetMonitors(L),
      setExpr: (i: number, o?: SetExprOpts) => self.setExpr(i, o),
      stepBefore: (now: number) => stepBefore(L, now),
      stepAfter: (now: number, dt: number) => stepAfter(L, now, dt),
      renderBefore: (now: number) => self.renderBefore(now),
      renderAfterFrame: (now: number) => self.renderAfterFrame(now),
      ensureFrames: () => self.ensureFrames(),
      iouInfo: (i: number) => iouInfo(L, i),
      get frames() { return self.frames; },
      get now() { return self.simNow; },
      set now(v: number) { self.simNow = v; },
      ...extra,
    };
  }
}
