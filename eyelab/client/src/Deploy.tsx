// The lab's Deploy section: pick Kevin's bots from a LAN scan, then one button runs the
// pipeline in server/deploy.ts (generate, parity, version, build, commit, flash, render
// check) and this shows each step live from GET /api/deploy/stream. The stream replays the
// whole run, so a reload mid-deploy picks up where it was.

import { useEffect, useReducer, useRef, useState } from "react";
import type { BotInfo, DeployEvent, RenderResult, StepId, StepStatus } from "../../shared/deploy";
import * as api from "./api";

interface StepView { id: StepId; title: string; status: StepStatus; summary: string; ms: number | null; lines: string[] }
type Done = Extract<DeployEvent, { type: "done" }>;
interface RunView {
  id: string; dryRun: boolean; bots: string[]; envs: string[]; logFile: string;
  steps: StepView[]; waiting: { step: StepId; bot: string; message: string } | null; done: Done | null; error: string | null;
}

function apply(v: RunView | null, e: DeployEvent): RunView | null {
  if (e.type === "run") return { id: e.id, dryRun: e.dryRun, bots: e.bots, envs: e.envs, logFile: e.logFile, steps: e.steps.map((s) => ({ ...s, status: "pending", summary: "", ms: null, lines: [] })), waiting: null, done: null, error: null };
  if (!v) return v;
  const upd = (id: StepId, f: (s: StepView) => StepView) => ({ ...v, steps: v.steps.map((s) => (s.id === id ? f(s) : s)) });
  switch (e.type) {
    case "step-start": return upd(e.step, (s) => ({ ...s, status: "running" }));
    case "log": return upd(e.step, (s) => ({ ...s, lines: [...s.lines, e.line] }));
    case "step-end": return upd(e.step, (s) => ({ ...s, status: e.status, summary: e.summary, ms: e.ms }));
    case "waiting": return { ...upd(e.step, (s) => ({ ...s, status: "waiting" })), waiting: { step: e.step, bot: e.bot, message: e.message } };
    case "resumed": return { ...upd(e.step, (s) => ({ ...s, status: "running" })), waiting: null };
    case "done": return { ...v, done: e, waiting: null };
    case "error": return { ...v, error: e.message };
    default: return v;
  }
}

/** Reads `data:` lines from a server-sent event stream (as kpow-dash's ClaudePanel does). */
async function* sseEvents(body: ReadableStream<Uint8Array>): AsyncGenerator<DeployEvent> {
  const reader = body.getReader();
  const decoder = new TextDecoder();
  let buf = "";
  for (;;) {
    const { value, done } = await reader.read();
    if (done) return;
    buf += decoder.decode(value, { stream: true });
    let cut;
    while ((cut = buf.indexOf("\n\n")) !== -1) {
      const frame = buf.slice(0, cut);
      buf = buf.slice(cut + 2);
      const data = frame.split("\n").filter((l) => l.startsWith("data:")).map((l) => l.slice(5).trimStart()).join("\n");
      if (data) yield JSON.parse(data) as DeployEvent;
    }
  }
}

const CHIP: Record<StepStatus, string> = { pending: "pending", running: "running", ok: "ok", warn: "warn", fail: "fail", skip: "skipped", waiting: "waiting" };
const secs = (ms: number | null) => (ms === null ? "" : ms >= 60_000 ? `${Math.floor(ms / 60_000)}m ${Math.round((ms % 60_000) / 1000)}s` : `${(ms / 1000).toFixed(1)} s`);
const msOf = (us: number) => `${(us / 1000).toFixed(1)} ms`;

export default function Deploy({ saveStatus }: { saveStatus: { s: string; text: string } | null }) {
  const [run, dispatch] = useReducer((v: RunView | null, e: DeployEvent | null) => (e ? apply(v, e) : null), null);
  const [gen, setGen] = useState(0); // bump to (re)open the stream
  const [bots, setBots] = useState<BotInfo[] | null>(null);
  const [scanning, setScanning] = useState(false);
  const [scanError, setScanError] = useState("");
  const [chosen, setChosen] = useState<Set<string>>(new Set());
  const [msg, setMsg] = useState("");
  const [open, setOpen] = useState<Partial<Record<StepId, boolean>>>({});

  // The stream: replays the current or last run, then follows it. Reconnects if it drops mid-run.
  useEffect(() => {
    const ctl = new AbortController();
    let finished = false, retry: ReturnType<typeof setTimeout> | undefined;
    (async () => {
      try {
        const res = await fetch("/api/deploy/stream", { signal: ctl.signal });
        if (!res.ok || !res.body) throw new Error(`HTTP ${res.status}`);
        for await (const e of sseEvents(res.body)) {
          if (e.type === "ping") continue;
          if (e.type === "idle") { finished = true; continue; }
          if (e.type === "run") setOpen({});
          dispatch(e);
          if (e.type === "done") finished = true;
        }
      } catch { /* server away or aborted */ }
      if (!finished && !ctl.signal.aborted) retry = setTimeout(() => setGen((g) => g + 1), 2000);
    })();
    return () => { ctl.abort(); clearTimeout(retry); };
  }, [gen]);

  async function rescan() {
    setScanning(true); setScanError("");
    try {
      const { bots: list } = await api.getBots();
      setBots(list);
      setChosen((c) => new Set([...c].filter((n) => list.some((b) => b.name === n && b.selectable))));
    } catch (e) { setScanError(`Scan failed: ${(e as Error).message}`); }
    setScanning(false);
  }
  useEffect(() => { rescan(); }, []);

  const running = !!run && !run.done && !run.error;
  const unsaved = !!saveStatus && saveStatus.s !== "saved";
  async function start(dryRun: boolean) {
    setMsg("");
    try {
      await api.startDeploy({ bots: dryRun ? [] : [...chosen], dryRun });
      setGen((g) => g + 1);
    } catch (e) { setMsg((e as Error).message); }
  }
  async function cancel() { try { await api.cancelDeploy(); } catch (e) { setMsg((e as Error).message); } }
  async function ack() { try { const r = await api.ackDeploy(); if (!r.ok) setMsg("Nothing is waiting."); } catch (e) { setMsg((e as Error).message); } }
  const toggle = (name: string, on: boolean) => setChosen((c) => { const n = new Set(c); if (on) n.add(name); else n.delete(name); return n; });

  return (
    <section className="card deploy" aria-labelledby="hDeploy">
      <h2 id="hDeploy">Deploy · faces to firmware to your bots</h2>
      <p className="note">Generates <code>bot_eye_params.h</code> from the face files, proves the firmware model draws what the lab draws, bumps <code>FIRMWARE_VERSION</code>, builds, commits (never pushes), flashes the chosen bots over wifi and reads their render cost back. It stops at the first failure. <b>Dry run</b> does the first two steps and keeps no file changes.</p>
      <div className="deploy-grid">
        <div>
          <div className="tunehead">
            <h3>Bots on the LAN</h3>
            <button onClick={rescan} disabled={scanning}>{scanning ? "Scanning…" : "Rescan"}</button>
          </div>
          {scanError && <p className="status bad">{scanError}</p>}
          {bots && !bots.length && !scanning && <p className="status">No vizBot answered. Check they are on wifi, then rescan.</p>}
          <ul className="bots">
            {(bots ?? []).map((b) => (
              <li key={`${b.name}@${b.ip}`} className={b.selectable ? undefined : "off"}>
                <label>
                  <input type="checkbox" disabled={!b.selectable || running} checked={chosen.has(b.name)} onChange={(e) => toggle(b.name, e.target.checked)} />
                  <b>{b.name}</b>
                  <span className="mono">{b.board}</span>
                  <span className="mono">v{b.firmware}</span>
                  <span className="mono dimtxt">{b.ip}</span>
                </label>
                {b.reason && <span className="why">{b.reason}</span>}
              </li>
            ))}
          </ul>
          <div className="row">
            <button className="primary" disabled={running || unsaved || !chosen.size} onClick={() => start(false)}>Deploy{chosen.size ? ` to ${chosen.size} bot${chosen.size === 1 ? "" : "s"}` : ""}</button>
            <button disabled={running || unsaved} onClick={() => start(true)}>Dry run</button>
            {running && <button className="armed" onClick={cancel}>Cancel</button>}
          </div>
          <p className="status" aria-live="polite">
            {msg ? <span className="bad">{msg}</span> : unsaved ? `Wait for the face files to save (${saveStatus!.text}).` : !chosen.size ? "Choose a bot to deploy, or try a dry run." : ""}
          </p>
        </div>
        <div>
          {!run ? <p className="status">No deploy since the server started.</p> : (
            <>
              <div className="tunehead">
                <h3>{run.dryRun ? "Dry run" : `Deploy${run.bots.length ? ` to ${run.bots.join(", ")}` : run.envs.length ? ` (build ${run.envs.join(", ")} only)` : ""}`}</h3>
                <span className="status mono">{run.id.replace("T", " ").slice(0, 19)}</span>
              </div>
              <ol className="steps">
                {run.steps.map((s) => {
                  const isOpen = open[s.id] ?? (s.status === "running" || s.status === "fail" || s.status === "waiting");
                  return (
                    <li key={s.id}>
                      <div className="sthead">
                        <span className={`chip ${s.status}`}>{CHIP[s.status]}</span>
                        <span className="sttitle">{s.title}</span>
                        <span className="num dimtxt">{secs(s.ms)}</span>
                        {s.lines.length > 0 && <button className="logbtn" aria-expanded={isOpen} onClick={() => setOpen((o) => ({ ...o, [s.id]: !isOpen }))}>{isOpen ? "Hide log" : `Log (${s.lines.length})`}</button>}
                      </div>
                      {s.summary && <p className={`stsum ${s.status === "fail" ? "bad" : ""}`}>{s.summary}</p>}
                      {run.waiting && run.waiting.step === s.id && (
                        <div className="waitbox" role="alert">
                          <p>{run.waiting.message}</p>
                          <button className="primary" onClick={ack}>Done, it is back on</button>
                        </div>
                      )}
                      {isOpen && s.lines.length > 0 && <StepLog lines={s.lines} />}
                    </li>
                  );
                })}
              </ol>
              {run.error && <p className="stsum bad">Pipeline error: {run.error}</p>}
              {run.done && <DoneBox done={run.done} logFile={run.logFile} />}
            </>
          )}
        </div>
      </div>
    </section>
  );
}

/** A step's log, kept scrolled to the bottom while it grows unless you scroll up. */
function StepLog({ lines }: { lines: string[] }) {
  const ref = useRef<HTMLPreElement>(null);
  const stick = useRef(true);
  useEffect(() => { const el = ref.current; if (el && stick.current) el.scrollTop = el.scrollHeight; }, [lines.length]);
  return (
    <pre className="steplog" ref={ref} tabIndex={0} onScroll={(e) => { const el = e.currentTarget; stick.current = el.scrollHeight - el.scrollTop - el.clientHeight < 24; }}>
      {lines.join("\n")}
    </pre>
  );
}

function DoneBox({ done, logFile }: { done: Done; logFile: string }) {
  return (
    <div className={`donebox ${done.ok ? "ok" : "fail"}`}>
      <p><b>{done.summary}</b>{done.version && <> New version <code>v{done.version}</code>.</>}{done.commit && <> Commit <code>{done.commit}</code> (not pushed).</>}</p>
      {done.renders.length > 0 && (
        <div className="tablewrap"><table className="renders">
          <thead><tr><th scope="col">Bot</th><th scope="col">Firmware</th><th scope="col">Avg</th><th scope="col">Max</th><th scope="col">Renders</th><th scope="col">Skipped</th><th scope="col">Result</th></tr></thead>
          <tbody>{done.renders.map((r: RenderResult) => (
            <tr key={r.bot}>
              <td>{r.bot}</td><td className="num">v{r.firmware}</td>
              <td className="num">{r.faceRender ? msOf(r.faceRender.avgUs) : "–"}</td><td className="num">{r.faceRender ? msOf(r.faceRender.maxUs) : "–"}</td>
              <td className="num">{r.faceRender?.renders ?? "–"}</td><td className="num">{r.faceRender?.skipped ?? "–"}</td>
              <td><span className={`chip ${r.status}`}>{CHIP[r.status]}</span></td>
            </tr>
          ))}</tbody>
        </table></div>
      )}
      <p className="status">Log: <code>{logFile.replace(/^\/Users\/[^/]+/, "~")}</code></p>
    </div>
  );
}
