// The eye lab page: before/after device canvases, the face grid, look pad, scorecard and
// tuning panel, laid out and worded as the artifact lab was. Edits change the shared model
// in place (LabRunner) and are saved to eyelab/faces/ by the Saver.

import { useEffect, useReducer, useRef, useState } from "react";
import type { ReactNode } from "react";
import type { EyeParams, Face, Globals, PairKey, Ticks } from "../../shared/faces";
import { PRESETS, clone, faceFileName } from "../../shared/faces";
import { EM_NAME } from "../../shared/before";
import { f2, hex4, near } from "../../shared/raster";
import { padEngaged } from "../../shared/state";
import { IS_P1, MOUTH_LED, iouInfo, overlayInfo } from "../../shared/measure";
import * as api from "./api";
import { LabRunner } from "./runner";
import { Saver, facePath } from "./saver";
import type { SavePath } from "./saver";
import { CRIT, GSPEC, NOTES, PSPEC, cf, fmt } from "./text";

/** Everything the page works on, created once the face files have loaded. */
interface Model { runner: LabRunner; saver: Saver; faces: Face[]; ticks: Ticks[] }

const pad2 = (i: number) => String(i).padStart(2, "0");
const span = (ok: boolean, txt: ReactNode) => <span className={ok ? "good" : "bad"}>{txt}</span>;

export default function App() {
  const [model, setModel] = useState<Model | null>(null);
  const [loadError, setLoadError] = useState("");
  const [, bump] = useReducer((n: number) => n + 1, 0);

  useEffect(() => {
    let live = true;
    api.getFaces().then(({ faces, globals }) => {
      if (!live) return;
      const runner = new LabRunner(faces, globals);
      const ticks = faces.map((f) => f.scorecard.slice() as Ticks);
      const m: Model = { runner, saver: null as unknown as Saver, faces, ticks };
      m.saver = new Saver((path) => buildDoc(m, path), bump);
      m.saver.init(faces, globals);
      runner.subscribe(bump);
      runner.publish({ saver: m.saver, faces, ticks, getFace: (i: number) => buildFace(m, i) });
      setModel(m);
    }, (e: Error) => { if (live) setLoadError(`Faces could not load: ${e.message}. Start the server with npm run dev in eyelab/, then reload.`); });
    return () => { live = false; };
  }, []);

  return (
    <div className="wrap">
      <header>
        <h1>vizBot Eye Lab</h1>
        <p className="sub"><b>Round 4: all 25 faces.</b> Before: <code>renderBotFace()</code> from <code>vizbot/bot_eyes.h</code> with the <code>bot_faces.h</code> table and blend, as shipped. After: the parametric model from <code>docs/eye-art-direction.md</code> with the art director's reviews applied: round 2 for the 8 P1 faces (one lid-line closing path for blinks and winks, a calmer idle budget), round 3's startle window, and round 4 for the 17 P2 faces (<code>docs/eye-art-review-r4.md</code>: exit blinks, slower springs out of the size extremes, bolder overlays). Both sides are drawn at device pixels in RGB565, one simulated device frame at a time.</p>
      </header>
      {model ? <Lab m={model} /> : <p className="savestat" data-s={loadError ? "error" : "pending"} role="status"><span className="dotst" aria-hidden="true"></span><span>{loadError || "Loading faces…"}</span></p>}
    </div>
  );
}

// ---------------------------------------------------------------- documents the Saver writes
function buildFace(m: Model, i: number): Face {
  const f = m.faces[i];
  return { index: f.index, name: f.name, kevinTuned: f.kevinTuned, eye: m.runner.L.AP[i], brow: f.brow, mouth: f.mouth, scorecard: m.ticks[i] };
}
function buildDoc(m: Model, path: SavePath): Face | Globals {
  if (path === "globals") { const G = m.runner.L.GP; return { R: G.R, gap: G.gap, arcW: G.arcW, spin: G.spin }; }
  return buildFace(m, Number(path.slice(5)));
}

// ---------------------------------------------------------------- the lab
function Lab({ m }: { m: Model }) {
  const { runner: r, saver } = m, L = r.L, ui = L.ui;
  const cvB = useRef<HTMLCanvasElement>(null), cvA = useRef<HTMLCanvasElement>(null);
  const dimB = useRef<HTMLDivElement>(null), dimA = useRef<HTMLDivElement>(null);
  const padEl = useRef<HTMLDivElement>(null);
  const [copyStatus, setCopyStatus] = useState("");
  const [exportText, setExportText] = useState("");
  const [armed, setArmed] = useState(false);
  const armTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const exportRef = useRef<HTMLTextAreaElement>(null);
  const c1Ref = useRef<HTMLInputElement>(null);

  useEffect(() => r.attach({ cvB: cvB.current!, cvA: cvA.current!, dimB: dimB.current!, dimA: dimA.current! }), [r]);

  const sel = r.selected, E = L.EXPR, P = L.AP[sel], B = saver.baselines[sel]?.eye, GB = saver.globalsBase;
  const touchSel = (delay?: number) => saver.touch(facePath(sel), delay);
  const t = m.ticks[sel];

  // KISSING criterion 1 is neither pass nor fail until ticked (A4).
  useEffect(() => { if (c1Ref.current) c1Ref.current.indeterminate = MOUTH_LED.has(sel) && !t[0]; });

  // ---------------------------------------------------------------- edits
  function onParam(k: string, e: number, v: number) {
    const p = P as unknown as Record<string, number | [number, number]>;
    if (e < 0) p[k] = v;
    else {
      const pk = k as PairKey;
      if (r.linked[k]) P[pk] = [v, v]; else P[pk][e] = v;
      if (k === "heart" || k === "star") { const o = k === "heart" ? "star" : "heart"; for (let j = 0; j < 2; j++) if (P.heart[j] + P.star[j] > 1) P[o][j] = Math.max(0, 1 - P[pk][j]); }
      if (["pw", "ph", "pdy", "heart", "star"].includes(k)) P.preset = "";
    }
    r.paramsChanged(); touchSel();
  }
  function onLink(k: PairKey, on: boolean) {
    r.linked[k] = on;
    if (on) { P[k][1] = P[k][0]; r.paramsChanged(); touchSel(); } else r.emit();
  }
  function onGlobal(k: keyof Globals, v: number) { L.GP[k] = v; r.paramsChanged(); saver.touch("globals"); }
  function onPreset(v: string) {
    if (!v) return;
    const pr = PRESETS[v];
    for (const k of ["pw", "ph", "pdy", "heart", "star"] as const) { const d = (k in pr) ? pr[k]! : (k === "pdy" ? P.pdy[0] : 0); P[k] = Array.isArray(d) ? [d[0], d[1]] : [d, d]; }
    P.preset = v; r.paramsChanged(); touchSel();
  }
  function onOverlay(v: EyeParams["overlay"]) { P.overlay = v; r.swapOverlay(v); r.paramsChanged(); touchSel(0); }
  function onGaze(v: EyeParams["gaze"]) { P.gaze = v; L.AS.gaze.next = r.simNow; r.paramsChanged(); touchSel(0); }
  function onTick(n: number, on: boolean) { const a = m.ticks[sel].slice() as Ticks; a[n] = on ? 1 : 0; m.ticks[sel] = a; saver.touch(facePath(sel), 0); r.emit(); }

  function applyFace(face: Face) {
    const i = face.index;
    L.AP[i] = clone(face.eye); m.ticks[i] = face.scorecard.slice() as Ticks; m.faces[i] = { ...m.faces[i], kevinTuned: face.kevinTuned };
  }
  function resetExpr() {
    const i = sel;
    saver.reset(facePath(i), () => api.resetFace(i)).then((res) => {
      applyFace(res.face); r.setLinks(); r.paramsChanged(); r.syncOverlay();
      setCopyStatus(`${E[i].name} reset to the last commit (${faceFileName(i, E[i].name)}).`);
    }, (e: Error) => setCopyStatus(e.message));
  }
  function disarm() { clearTimeout(armTimer.current); setArmed(false); }
  async function resetAll() {
    if (!armed) { setArmed(true); armTimer.current = setTimeout(() => setArmed(false), 4000); return; }
    disarm();
    const errs: string[] = [];
    await Promise.all(m.faces.map((f) => saver.reset(facePath(f.index), () => api.resetFace(f.index)).then((res) => applyFace(res.face), (e: Error) => { errs.push(e.message); })));
    await saver.reset("globals", () => api.resetGlobals()).then((res) => { Object.assign(L.GP, res.globals); }, (e: Error) => { errs.push(e.message); });
    r.setLinks(); r.paramsChanged(); r.syncOverlay();
    setCopyStatus(errs.length ? `Reset ${m.faces.length + 1 - errs.length} of ${m.faces.length + 1} files. ${errs[0]}${errs.length > 1 ? ` (and ${errs.length - 1} more)` : ""}` : `All ${m.faces.length} expressions and globals reset to the last commit.`);
  }

  // ---------------------------------------------------------------- export
  function cStruct(i: number) {
    const P = L.AP[i], GP = L.GP, pair = (k: PairKey) => `{${cf(P[k][0])}, ${cf(P[k][1])}}`;
    const OV = { none: "EYE_OVERLAY_NONE", x: "EYE_OVERLAY_X", spiral: "EYE_OVERLAY_SPIRAL", glitch: "EYE_OVERLAY_GLITCH" };
    const state = saver.baselines[i] === null ? "not committed yet" : saver.faceEdited(i, buildFace(m, i)) ? "edited since the last commit" : "as committed";
    return `// BotEyeParams for ${i}: ${E[i].name} (vizBot eye lab export)
// Field names are the brief's section-3 parameter names. [0] = screen-left eye, [1] = screen-right.
// Sizes are x R (eye half-width). Globals: R ${GP.R} px, gap ${GP.gap} R, arcW ${GP.arcW} R, spin ${GP.spin} rad/s.
// Brows, mouth and transitionMs's table entry stay in BotExpression (bot_faces.h).
// From eyelab/faces/${faceFileName(i, E[i].name)}, ${state}.
/* ${i}: ${E[i].name} */ {
  .aspect = ${pair("aspect")}, .scale = ${pair("scale")},
  .lidTop = ${pair("lidTop")}, .slant = ${pair("slant")}, .sag = ${pair("sag")},
  .lidBot = ${pair("lidBot")}, .bow = ${pair("bow")},
  .shut = ${pair("shut")}, .cup = ${pair("cup")},
  .pw = ${pair("pw")}, .ph = ${pair("ph")},
  .inset = ${pair("inset")}, .pdy = ${pair("pdy")},
  .heart = ${pair("heart")}, .star = ${pair("star")},
  .gazeX = ${cf(P.gazeX)}, .gazeY = ${cf(P.gazeY)},
  .pop = ${cf(P.pop)}, .transitionMs = ${Math.round(P.transitionMs)},
  .overlay = ${OV[P.overlay]}, .gaze = GAZE_${P.gaze.toUpperCase()},
},`;
  }
  function jsonTable() {
    return JSON.stringify({
      source: "vizBot eye lab, eyelab/faces",
      unsavedEdits: [...saver.dirty],
      globals: L.GP,
      expressions: L.AP.map((p, i) => Object.assign({ index: i, name: E[i].name, p1: IS_P1.has(i), kevinTuned: m.faces[i].kevinTuned, editedSinceCommit: saver.faceEdited(i, buildFace(m, i)) }, p)),
    }, null, 1);
  }
  function copyText(txt: string, what: string) {
    setExportText(txt);
    const fallback = () => { const out = exportRef.current!; out.value = txt; out.focus(); out.select(); setCopyStatus(`Clipboard blocked. ${what} is selected in the box below: press Ctrl+C or Cmd+C.`); };
    try {
      if (!navigator.clipboard || !navigator.clipboard.writeText) { fallback(); return; }
      navigator.clipboard.writeText(txt).then(() => setCopyStatus(`${what} copied.`), fallback);
    } catch { fallback(); }
  }

  // ---------------------------------------------------------------- scorecard set-ups
  function setupCrit(n: number) {
    if (n === 0) { r.setUi("mono", false); r.setUi("device", "lcd169"); r.setUi("scale", "actual"); r.setUi("showBrows", false); r.setUi("showMouth", false); r.setUi("showPupils", true); }
    if (n === 1) { r.setUi("showPupils", false); r.setUi("showBrows", false); r.setUi("showMouth", false); }
    if (n === 2) { r.setUi("mono", true); r.setUi("scale", "auto"); }
    if (n === 3) { r.tourMode = "pairs"; if (r.tourState) r.tourStop(); r.tourStart(); }
    if (n === 4) { r.setUi("ambient", true); }
    if (n === 5) {
      r.setUi("idleLife", true); r.setUi("pendant", false); r.setUi("sleep", false); if (sel !== 0) r.setExpr(0);
      const s = L.stats; s.since = r.simNow; s.gaze.length = s.blinks.length = s.fidgets.length = 0;
    }
  }
  function restoreView() {
    r.setUi("showBrows", true); r.setUi("showMouth", true); r.setUi("showPupils", true); r.setUi("mono", false); r.setUi("ambient", false);
    r.setUi("scale", "auto"); r.setUi("sleep", false); r.setUi("legacy", false);
  }

  // ---------------------------------------------------------------- look pad
  const pad = L.pad;
  function padFromEvent(ev: React.PointerEvent) { const b = padEl.current!.getBoundingClientRect(); r.padSet(((ev.clientX - b.left) / b.width - .5) * 2 / .92, ((ev.clientY - b.top) / b.height - .5) * 2 / .88); }
  function padKey(ev: React.KeyboardEvent) {
    const st = .1; let used = true;
    if (ev.key === "ArrowLeft") r.padSet(pad.x - st, pad.y); else if (ev.key === "ArrowRight") r.padSet(pad.x + st, pad.y);
    else if (ev.key === "ArrowUp") r.padSet(pad.x, pad.y - st); else if (ev.key === "ArrowDown") r.padSet(pad.x, pad.y + st);
    else if (ev.key === "Escape" || ev.key === "Home") r.padSet(0, 0); else used = false;
    if (used) ev.preventDefault();
  }

  // ---------------------------------------------------------------- readouts
  const f = L.BF.face, AS = L.AS, E0 = AS.eyes[0], E1 = AS.eyes[1], lm = r.lastMetrics;
  const pr = (a: { x: number }, b: { x: number }) => near(+f2(a.x), +f2(b.x)) ? f2(a.x) : `${f2(a.x)}|${f2(b.x)}`;
  const roBefore = `${EM_NAME[f.eyeMode]} · white ${f.eyeWhiteW}×${f.eyeWhiteH} · spacing ±${f.eyeSpacing} · pupil r${f.pupilRadius} at (${f.pupilOffsetX + f.dynamicPupilX}, ${f.pupilOffsetY + f.dynamicPupilY}) · blink ${f.blinkAmount.toFixed(2)}${f.transitioning ? ` · blend ${E[f.currentExpr].name}→${E[f.targetExpr].name} ${Math.round(f.transitionProgress * 100)}%` : ""}`;
  const roAfter = `lidTop ${pr(E0.lidTop, E1.lidTop)} · slant ${pr(E0.slant, E1.slant)} · shut ${pr(E0.shut, E1.shut)} · pupil ${pr(E0.pw, E1.pw)} R · gaze (${f2(AS.gxOut)}, ${f2(AS.gyOut)}) · blink ${AS.blink.b.toFixed(2)}`
    + (lm ? ` · area ${lm.area[0]}|${lm.area[1]} px · gap ${lm.gap === null ? "n/a" : lm.gap + " px"}` : "")
    + (AS.overlay !== "none" ? ` · overlay ${AS.overlay}` : "") + (AS.idle.fid ? ` · fidget ${AS.idle.fid.name}` : "") + (AS.idle.look ? ` · look ${AS.idle.look.preset || AS.idle.look.mood}` : "")
    + (ui.pendant ? ` · pushed ${r.pushed} · skipped ${r.skipped}` : "");

  // ---------------------------------------------------------------- tuning state
  let edits = 0;
  const rows: ReactNode[] = [];
  let grp = "", group: ReactNode[] = [];
  const flushGroup = () => { if (grp) rows.push(<div className="pgroup" key={grp}><p className="gname">{grp}</p>{group}</div>); group = []; };
  for (const [g, k, mn, mx, st, per, note] of PSPEC) {
    if (g !== grp) { flushGroup(); grp = g; }
    if (per) {
      const pk = k as PairKey, v = P[pk], b = B?.[pk];
      const ed = !!b && (!near(v[0], b[0]) || !near(v[1], b[1]));
      if (ed) edits++;
      const def = `${note}${b ? ` · HEAD ${near(b[0], b[1]) ? fmt(k, b[0]) : fmt(k, b[0]) + " / " + fmt(k, b[1])}` : ""}`;
      group.push(<div className={"prow" + (ed ? " edited" : "")} id={`row-${k}`} key={k}>
        <div className="pname"><code>{k}</code><span className="def" id={`def-${k}`}>{def}</span></div>
        {[0, 1].map((s) => { const id = s ? "R" : "L"; return (
          <div className="cell" key={id}><span className="side">{id}</span><input type="range" id={`p-${k}-${id}`} min={mn} max={mx} step={st} aria-label={`${k} ${s ? "right" : "left"} eye`} value={v[s]} onChange={(ev) => onParam(k, s, +ev.target.value)} /><output id={`o-${k}-${id}`} htmlFor={`p-${k}-${id}`}>{fmt(k, v[s])}</output></div>); })}
        <label className="lnk" htmlFor={`ln-${k}`}><input type="checkbox" id={`ln-${k}`} checked={!!r.linked[k]} onChange={(ev) => onLink(pk, ev.target.checked)} /> link</label>
      </div>);
    } else {
      const sk = k as "gazeX" | "gazeY" | "pop" | "transitionMs", v = P[sk], b = B?.[sk];
      const ed = b !== undefined && !near(v, b);
      if (ed) edits++;
      group.push(<div className={"prow single" + (ed ? " edited" : "")} id={`row-${k}`} key={k}>
        <div className="pname"><code>{k}</code><span className="def" id={`def-${k}`}>{`${note}${b !== undefined ? ` · HEAD ${fmt(k, b)}` : ""}`}</span></div>
        <div className="cell"><span className="side"></span><input type="range" id={`p-${k}`} min={mn} max={mx} step={st} aria-label={k} value={v} onChange={(ev) => onParam(k, -1, +ev.target.value)} /><output id={`o-${k}`} htmlFor={`p-${k}`}>{fmt(k, v)}</output></div><span></span>
      </div>);
    }
  }
  flushGroup();
  if (B) for (const key of ["overlay", "gaze"] as const) if (P[key] !== B[key]) edits++;
  const anyBase = saver.baselines.some((b) => b), allLoaded = saver.globalsBase !== undefined;
  const total = m.faces.reduce((n, _, j) => n + (saver.faceEdited(j, buildFace(m, j)) ? 1 : 0), 0);
  const editLine = !allLoaded ? `${edits} edited here` : !anyBase ? "faces/ is not in a commit yet: nothing to compare with"
    : `${edits} edited here · ${total} of ${m.faces.length} expressions edited since the last commit${saver.baselines[sel] === null ? " · this face is not committed yet" : ""}`;
  const st = saver.status();

  // ---------------------------------------------------------------- scorecard measures
  const N = NOTES[sel] || ["", ""];
  const OVi = overlayInfo(L, sel);
  const meas: ReactNode[] = CRIT.map(() => null);
  if (MOUTH_LED.has(sel)) meas[0] = t[0] ? <>ticked: A4 accepted, {span(true, "pass")} as a mouth-led face</> : <><span className="flag">pending Kevin</span> · mouth-led (A4): neither pass nor fail until Kevin accepts or rejects A4. Tick it to accept.</>;
  {
    const I = iouInfo(L, sel);
    if (OVi) {
      meas[1] = <>scored on the overlay (A1) · {OVi.what} {span(OVi.px >= 2, OVi.px.toFixed(1) + " OLED px")} · swaps under blink {L.mon.swapsBlink}/{L.mon.swapsReq} · closes with the lids: {OVi.closes} <span className="status">(rest silhouette nearest {E[I.bj].name} {I.best.toFixed(3)}: not scored)</span></>;
    } else {
      const parts: ReactNode[] = [];
      if (I.pair) parts.push(<>pair {E[I.pair.j].name} {span(I.pair.v < .9, I.pair.v.toFixed(3))}</>);
      if (I.p1) parts.push(<>nearest P1 {E[I.p1.j].name} {span(I.p1.v < .9, I.p1.v.toFixed(3))}</>);
      if (I.flag) {
        const F = I.flag, why = [`lid corners differ ${F.corner.toFixed(1)} OLED px`]; if (F.asym) why.push("asymmetric");
        parts.push(<>nearest {E[F.j].name} {F.v.toFixed(3)} <span className="flag">eyeball</span> ({why.join(", ")}: {F.ok ? span(true, "passes if it reads distinct") : span(false, "check side by side")})</>);
      } else if (!(I.pair && I.pair.j === I.bj) && !(I.p1 && I.p1.j === I.bj)) parts.push(<>nearest {E[I.bj].name} {span(I.best < .9, I.best.toFixed(3))}</>);
      meas[1] = <>{parts.map((p, n) => <span key={n}>{n ? " · " : ""}{p}</span>)} <span className="status">(overlay faces excluded)</span></>;
    }
  }
  meas[2] = <>{ui.mono ? "Showing 128×64 1-bit. Check lid corners and pupils by eye." : "Press Set up to switch to 1-bit."}{OVi ? <> · overlay (A1): {OVi.what} {span(OVi.px >= 2, OVi.px.toFixed(1) + " px")}</> : null}</>;
  {
    const mon = L.mon, stp = Math.round(mon.maxStep * 100);
    meas[3] = <>max area step {span(stp <= 25, stp + "%")}{mon.worst ? " (" + mon.worst + ")" : ""} · min gap {mon.minGap > 1e8 ? "n/a" : span(mon.minGap >= 1, mon.minGap + " px")} · pupil outside outline {span(mon.pupilOut === 0, mon.pupilOut + " px")} · swaps under blink {mon.swapsBlink}/{mon.swapsReq} · exit blinks {mon.exitBlinks} (count as blinks, A2)</>;
  }
  {
    const list = (s: Set<number>) => [...s].map(hex4).join(", ");
    meas[4] = <>before draws {list(r.colsB)} · after draws {list(r.colsA)}{ui.ambient ? " · on ambient" : ""}</>;
  }
  if (sel !== 0) meas[5] = "Select 00 NEUTRAL to measure.";
  else if (!ui.idle) meas[5] = "Idle life is off.";
  else {
    const stats = L.stats, now = r.simNow;
    const el = Math.min(60, (now - stats.since) / 1000);
    const ev = stats.gaze.concat(stats.blinks, stats.fidgets).filter((x) => x >= stats.since).sort((a, b) => a - b);
    let gapMax = 0, prev = Math.max(stats.since, now - 60000);
    for (const x of ev) { gapMax = Math.max(gapMax, x - prev); prev = x; }
    gapMax = Math.max(gapMax, now - prev);
    const g = stats.gaze.filter((x) => x >= stats.since).length, b = stats.blinks.filter((x) => x >= stats.since).length, fi = stats.fidgets.filter((x) => x >= stats.since).length;
    const full = el >= 59.9;
    meas[5] = <>{full ? "last 60 s" : el.toFixed(0) + " s of 60"}: gaze moves {full ? span(g >= 10 && g <= 25, g) : g} · blinks {full ? span(b >= 9 && b <= 23, b) : b} · longest still gap {span(gapMax <= 7000, (gapMax / 1000).toFixed(1) + " s")} · fidgets {span(fi <= Math.ceil(el / 8), fi)}</>;
  }

  const chk = (id: string, key: Parameters<LabRunner["setUi"]>[0], val: boolean, label: ReactNode) => (
    <label htmlFor={id}><input type="checkbox" id={id} checked={val} onChange={(ev) => r.setUi(key, ev.target.checked)} /> {label}</label>
  );

  return (
    <>
      <section className="bar" aria-label="Display settings">
        <label htmlFor="device">Device{" "}
          <select id="device" value={ui.device} onChange={(ev) => r.setUi("device", ev.target.value)}>
            <option value="lcd169">1.69" · 240×280</option>
            <option value="lcd13">1.3" · 240×240</option>
            <option value="cores3">CoreS3 · 320×240</option>
            <option value="chip">Touch-UI scale ×0.25 · 60×70</option>
          </select></label>
        <label htmlFor="scale">Scale{" "}
          <select id="scale" value={ui.scale} onChange={(ev) => r.setUi("scale", ev.target.value)}>
            <option value="auto">auto (integer)</option>
            <option value="1">1×</option><option value="2">2×</option><option value="3">3×</option><option value="4">4×</option><option value="6">6×</option>
            <option value="actual">actual size (panel ppi)</option>
          </select></label>
        <label htmlFor="fps">Frame rate{" "}
          <select id="fps" value={String(ui.fps)} onChange={(ev) => r.setUi("fps", ev.target.value)}>
            <option value="20">20 fps (vizBot)</option>
            <option value="25">25 fps (vizMini)</option>
            <option value="10">10 fps (slow pendant)</option>
            <option value="60">60 fps (not a device rate)</option>
          </select></label>
        <label htmlFor="speed">Time{" "}
          <select id="speed" value={String(ui.speed)} onChange={(ev) => r.setUi("speed", ev.target.value)}>
            <option value="1">1×</option><option value="0.5">0.5×</option><option value="0.25">0.25×</option>
          </select></label>
        <label htmlFor="faceColor"><code>botFaceColor</code>{" "}
          <select id="faceColor" value={String(ui.fc)} onChange={(ev) => r.setUi("faceColor", ev.target.value)}>
            <option value="65535">white 0xFFFF</option>
            <option value="2047">cyan 0x07FF</option>
            <option value="64896">amber 0xFD80 (#FFB000)</option>
            <option value="64376">pink 0xFB78 (#FF6EC7)</option>
            <option value="2016">green 0x07E0</option>
            <option value="63519">magenta 0xF81F</option>
            <option value="65504">yellow 0xFFE0</option>
          </select></label>
      </section>

      <section className="bar" aria-label="View toggles">
        <span className="grp">
          {chk("idleLife", "idleLife", ui.idle, "idle life")}
          {chk("showBrows", "showBrows", ui.brows, "brows")}
          {chk("showMouth", "showMouth", ui.mouth, "mouth")}
          {chk("showPupils", "showPupils", ui.pupils, "pupils")}
          {chk("mono", "mono", ui.mono, "1-bit 128×64 (vizMini ×0.45)")}
          {chk("ambient", "ambient", ui.ambient, "ambient bg + 5 px stroke")}
          {chk("sleep", "sleep", ui.sleep, "sleep state")}
        </span>
        <span className="grp">
          {chk("legacy", "legacy", ui.legacy, "after: legacy 12 px overlap")}
          {chk("catchlight", "catchlight", ui.catchlight, "after: catchlight (P2)")}
          {chk("pendant", "pendant", ui.pendant, "after: pendant mode")}
        </span>
      </section>

      <section className="stage" aria-label="Before and after">
        <figure className="panel">
          <figcaption><span className="tag">Before</span> <span>firmware today</span> <span className="src">bot_eyes.h · bot_faces.h lerp</span></figcaption>
          <div className="screen"><canvas className="dev" id="cvBefore" ref={cvB} aria-label="Before: current firmware face"></canvas><div className="dim" id="dimBefore" ref={dimB}></div></div>
          <p className="readout mono" id="roBefore">{roBefore}</p>
        </figure>
        <figure className="panel">
          <figcaption><span className="tag after">After</span> <span>round 4 model</span> <span className="src">column spans · springs · RGB565</span></figcaption>
          <div className="screen"><canvas className="dev" id="cvAfter" ref={cvA} aria-label="After: parametric face, round 4"></canvas><div className="dim" id="dimAfter" ref={dimA}></div></div>
          <p className="readout mono" id="roAfter">{roAfter}</p>
        </figure>
      </section>

      <div className="grid2">
        <div className="col">
          <section className="card" aria-labelledby="hExpr">
            <h2 id="hExpr">Expression · firmware index</h2>
            <div id="exprGrid" role="group" aria-label="Expressions">
              {E.map((e, i) => {
                const tuned = saver.faceEdited(i, buildFace(m, i));
                return (
                  <button key={i} className="ex" id={`ex-${i}`} type="button" aria-pressed={i === sel ? "true" : "false"} onClick={() => { if (r.tourState) r.tourStop(); r.setExpr(i); }}>
                    <span className="ix mono">{pad2(i)}</span><span className="nm">{e.name}</span>
                    <span className="badges">{IS_P1.has(i) ? <span className="p1" title="P1 in the brief">P1</span> : null}<span className="tuned" id={`tb-${i}`} hidden={!tuned} title={tuned ? "Edited since the last commit" : ""}>tuned</span></span>
                  </button>
                );
              })}
            </div>
            <div className="row">
              <button id="tourBtn" className="primary" onClick={() => { if (r.tourState) r.tourStop(); else r.tourStart(); }}>{r.tourState ? "Stop tour" : "Tour P1"}</button>
              <label htmlFor="tourMode"><select id="tourMode" value={r.tourMode} onChange={(ev) => { r.tourMode = ev.target.value as "p1" | "pairs"; r.emit(); }}>
                <option value="p1">P1 loop (8, 2.6 s each)</option>
                <option value="pairs">all 56 P1→P1 pairs</option>
              </select></label>
              <button id="replayBtn" onClick={() => r.replay()}>Replay last transition</button>
              <span className="status" id="tourStatus" aria-live="polite">{r.tourStatus}</span>
            </div>
          </section>

          <section className="card" aria-labelledby="hLook">
            <h2 id="hLook">Look pad · stands in for the IMU</h2>
            <div className="lookwrap">
              <div id="lookPad" ref={padEl} tabIndex={0} role="application" aria-label="Look pad. Drag, or use arrow keys to aim the pupils. Escape centres."
                onPointerDown={(ev) => { pad.drag = true; try { padEl.current!.setPointerCapture(ev.pointerId); } catch { /* capture is optional */ } padFromEvent(ev); ev.preventDefault(); }}
                onPointerMove={(ev) => { if (pad.drag) padFromEvent(ev); }}
                onPointerUp={() => { pad.drag = false; }} onPointerCancel={() => { pad.drag = false; }}
                onDoubleClick={() => r.padSet(0, 0)} onKeyDown={padKey}>
                <div id="padDot" style={{ left: (50 + pad.x * 46) + "%", top: (50 + pad.y * 44) + "%" }}></div>
              </div>
              <div>
                <p className="note">Drag to aim. The pupils stay there until you centre it.</p>
                <p className="status">Before: <code>BotIMUTracker</code> (24 px max, 0.2 per-frame low-pass). After: pursuit spring 2.0 Hz, ζ 0.9, gaze ±0.35 R / ±0.25 R.</p>
                <p className="status mono" id="padRead">{`x ${f2(pad.x)} · y ${f2(pad.y)}${padEngaged(L) ? " · tracking" : ""}`}</p>
                <button id="padCenter" onClick={() => r.padSet(0, 0)}>Centre</button>
              </div>
            </div>
          </section>

          <section className="card" aria-labelledby="hScore" id="scoreCard">
            <h2 id="hScore">Scorecard · brief §6, as amended in rounds 2–4</h2>
            <div id="scoreHead">
              <div className="score-head"><span className="mono">{pad2(sel)}</span><span className="big">{E[sel].name}</span>{IS_P1.has(sel) ? <span className="p1">P1</span> : <span className="prio2">P2: the first firmware pass ships the 8 P1s</span>}</div>
              <p className="note"><b>Today:</b> {N[0]}</p><p className="note"><b>Round 4 target:</b> {N[1]}</p>
            </div>
            <ol className="crit" id="critList">
              {CRIT.map((c, n) => (
                <li key={c[2]}><input type="checkbox" id={`sc-${c[2]}`} ref={n === 0 ? c1Ref : undefined} checked={!!t[n]} onChange={(ev) => onTick(n, ev.target.checked)} />
                  <label htmlFor={`sc-${c[2]}`}>{n + 1}. {c[0]}<span className="pass">{c[1]}</span></label>
                  <button type="button" id={`setup-${c[2]}`} onClick={() => setupCrit(n)}>Set up</button><span className="meas mono" id={`m-${c[2]}`}>{meas[n]}</span></li>
              ))}
            </ol>
            <div className="row">
              <button id="restoreView" onClick={restoreView}>Restore default view</button>
              <button id="resetMon" onClick={() => r.resetMeasurements()}>Reset measurements</button>
              <span className="status" id="storeStatus">Ticks save to the face file with your tweaks.</span>
            </div>
          </section>
        </div>

        <section className="card" aria-labelledby="hTune">
          <div className="tunehead">
            <h2 id="hTune">Tuning · after side · <span id="tuneName" className="mono">{`${pad2(sel)} ${E[sel].name}`}</span></h2>
            <span className="edits" id="editCount">{editLine}</span>
          </div>
          <p className="savestat" id="saveStatus" data-s={st.s} role="status" aria-live="polite"><span className="dotst" aria-hidden="true"></span><span id="saveText">{st.text}</span></p>
          <p className="status">Sizes are × R (eye half-width) unless marked. L is the screen-left eye. Linked rows move both eyes together. Rows that differ from the last commit are tinted and say "edited". Your tweaks save to <code>eyelab/faces/</code> on their own about a second after you stop moving a slider.</p>
          <div className="selrow">
            <label htmlFor="selPreset">pupil preset <select id="selPreset" value={P.preset || ""} onChange={(ev) => onPreset(ev.target.value)}>
              <option value="">(custom)</option>
              {Object.keys(PRESETS).map((k) => <option key={k} value={k}>{k}</option>)}
            </select></label>
            <label htmlFor="selOverlay">overlay <select id="selOverlay" value={P.overlay} onChange={(ev) => onOverlay(ev.target.value as EyeParams["overlay"])}>
              <option value="none">none</option><option value="x">X</option><option value="spiral">spiral</option><option value="glitch">glitch</option>
            </select></label>
            <label htmlFor="selGaze">gaze pattern <select id="selGaze" value={P.gaze} onChange={(ev) => onGaze(ev.target.value as EyeParams["gaze"])}>
              <option value="idle">idle saccades</option><option value="thinking">thinking orbit</option><option value="nervous">nervous darts</option>
              <option value="worried">nervous, half rate</option><option value="shy">shy</option><option value="focused">focused</option>
              <option value="annoyed">annoyed flick</option><option value="shocked">shocked tremble</option>
            </select></label>
          </div>
          <div id="globals" className="pgroup">
            <p className="gname">Globals (all expressions)</p>
            {GSPEC.map(([k, mn, mx, stp, note]) => {
              const ed = !!GB && !near(L.GP[k], GB[k]);
              return (
                <div className={"prow single" + (ed ? " edited" : "")} id={`row-g-${k}`} key={k}><div className="pname"><code>{k}</code><span className="def">{`${note}${GB ? ` · HEAD ${fmt(k, GB[k])}` : ""}`}</span></div>
                  <div className="cell"><span className="side"></span><input type="range" id={`g-${k}`} min={mn} max={mx} step={stp} aria-label={k} value={L.GP[k]} onChange={(ev) => onGlobal(k, +ev.target.value)} /><output id={`og-${k}`} htmlFor={`g-${k}`}>{fmt(k, L.GP[k])}</output></div><span></span></div>
              );
            })}
          </div>
          <div id="params">{rows}</div>
          <div className="row">
            <button id="copyC" className="primary" onClick={() => copyText(cStruct(sel), `C initializer for ${E[sel].name}`)}>Copy C struct</button>
            <button id="copyJson" onClick={() => copyText(jsonTable(), `JSON table (${m.faces.length} expressions)`)}>Copy JSON (all {m.faces.length})</button>
            <button id="resetExpr" onClick={resetExpr}>Reset this expression</button>
            <button id="resetAll" className={armed ? "armed" : undefined} onClick={resetAll}>{armed ? `Click again to reset all ${m.faces.length}` : "Reset all"}</button>
            <span className="status" id="copyStatus" aria-live="polite">{copyStatus}</span>
          </div>
          <label htmlFor="exportOut" className="status" style={{ display: "block", marginTop: 8 }}>Export (select and copy if the clipboard is blocked)</label>
          <textarea id="exportOut" ref={exportRef} readOnly spellCheck={false} value={exportText}></textarea>
          <p className="legend">Spring groups (§4): shape params use f = 0.75 / T, ζ 0.85, where T is the expression's transitionMs. Pupil pw, ph, heart, star use 3.0 Hz, ζ 0.6. Saccades 6.0 Hz, ζ 1.0. Pendant mode sets ζ 1 everywhere. Tempo guidance (A3, not a pass rule): below about 170 ms a shape spring is a teleport at 20 fps from the narrow faces, so fast faces lean on the startle window and pop, not on shorter springs. Exit blink (A2): leaving SHOCKED or ANNOYED starts a blink at once, even inside the next face's no-blink window.</p>
        </section>
      </div>
    </>
  );
}

