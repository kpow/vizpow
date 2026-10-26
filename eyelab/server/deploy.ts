// The deploy pipeline behind the lab's Deploy button: generate bot_eye_params.h, prove the
// firmware model matches the lab (parity), bump FIRMWARE_VERSION, build with PlatformIO,
// commit, OTA-flash Kevin's chosen bots and read their face render cost back. Each step
// streams its log; the run stops at the first failure. One run at a time; the last run's
// events stay in memory so a reloaded page picks the stream back up, and every run also
// goes to ~/Library/Logs/eyelab/deploy-<timestamp>.log.

import { createWriteStream, existsSync, mkdirSync } from "node:fs";
import type { WriteStream } from "node:fs";
import { readFile, stat, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import type { BotInfo, DeployEvent, DeployRequest, RenderResult, StepEnd, StepId } from "../shared/deploy";
import { describeParity, runParity } from "../parity/parity";
import { BOARD_ENV, allowlist, envBoard, getState, scanBots, uploadFirmware } from "./bots";
import { HttpError, ROOT } from "./faces";
import { Cancelled, runProc } from "./proc";

const REPO = join(ROOT, "..");
const PIO = join(homedir(), ".platformio", "penv", "bin", "pio"); // never Homebrew pio
const LOG_DIR = join(homedir(), "Library", "Logs", "eyelab");
const HEADER = "vizbot/bot_eye_params.h";
const CONFIG = "vizbot/config.h";
const COMMIT_PATHS = [HEADER, CONFIG, "eyelab/faces", "tools/gen_eye_params.py"];
const DEPLOY_SUBJECT = "eyelab: deploy v";
/** A face render over half the 20 fps frame budget is a warning, not a failure. */
const RENDER_WARN_US = 25_000;
/** Test hook: EYELAB_NO_FLASH=1 refuses any run that would flash a bot. */
const NO_FLASH = process.env.EYELAB_NO_FLASH === "1";

const TITLES: Record<StepId, string> = {
  generate: "Generate bot_eye_params.h",
  parity: "Parity: firmware model vs lab",
  version: "Bump FIRMWARE_VERSION",
  build: "Build (PlatformIO)",
  commit: "Commit",
  flash: "Flash (OTA)",
  render: "Render check",
};

class Run {
  events: DeployEvent[] = [];
  finished = false;
  ac = new AbortController();
  private waiters = new Set<(woke: boolean) => void>();
  private ackFn: (() => void) | null = null;
  private log: WriteStream;

  constructor(public id: string, public logFile: string) {
    mkdirSync(LOG_DIR, { recursive: true });
    this.log = createWriteStream(logFile, { flags: "a" });
  }

  push(e: DeployEvent) {
    this.events.push(e);
    this.log.write(logLine(e));
    this.wake(true);
  }
  wake(woke: boolean) { for (const w of this.waiters) w(woke); this.waiters.clear(); }
  /** Resolves true on the next event, false after ms with none. */
  next(ms: number): Promise<boolean> {
    return new Promise((resolve) => {
      const t = setTimeout(() => { this.waiters.delete(done); resolve(false); }, ms);
      const done = (woke: boolean) => { clearTimeout(t); resolve(woke); };
      this.waiters.add(done);
    });
  }
  finish() { this.finished = true; this.log.end(); this.wake(true); }

  /** Pauses the run until POST /api/deploy/ack (or cancel). */
  waitAck(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.ac.signal.aborted) return reject(new Cancelled());
      const onAbort = () => { this.ackFn = null; reject(new Cancelled()); };
      this.ac.signal.addEventListener("abort", onAbort, { once: true });
      this.ackFn = () => { this.ac.signal.removeEventListener("abort", onAbort); this.ackFn = null; resolve(); };
    });
  }
  ack(): boolean { if (!this.ackFn) return false; this.ackFn(); return true; }
}

function logLine(e: DeployEvent): string {
  const t = new Date().toTimeString().slice(0, 8);
  switch (e.type) {
    case "run": return `[${t}] run ${e.id}${e.dryRun ? " (dry run)" : ""} bots=${e.bots.join(",") || "-"} envs=${e.envs.join(",") || "-"}\n`;
    case "step-start": return `[${t}] == ${TITLES[e.step]}\n`;
    case "log": return `[${t}] [${e.step}] ${e.line}\n`;
    case "step-end": return `[${t}] == ${TITLES[e.step]}: ${e.status.toUpperCase()} ${e.summary} (${(e.ms / 1000).toFixed(1)} s)\n`;
    case "waiting": return `[${t}] [${e.step}] WAITING: ${e.message}\n`;
    case "resumed": return `[${t}] [${e.step}] resumed\n`;
    case "done": return `[${t}] done: ${e.ok ? "OK" : e.cancelled ? "CANCELLED" : "FAILED"} ${e.summary}\n`;
    case "error": return `[${t}] error: ${e.message}\n`;
    default: return "";
  }
}

let current: Run | null = null;

export const currentRun = () => current;
export const isRunning = () => !!current && !current.finished;

/** Validates the request against a fresh scan and the allowlist, then starts the run in the background. */
export async function startDeploy(body: unknown): Promise<{ id: string; logFile: string }> {
  if (isRunning()) throw new HttpError(409, "a deploy is already running");
  const req = (body ?? {}) as Partial<DeployRequest>;
  const dryRun = req.dryRun === true;
  const names = Array.isArray(req.bots) ? [...new Set(req.bots)] : [];
  if (!names.every((n): n is string => typeof n === "string")) throw new HttpError(400, "bots must be a list of deviceName strings");
  const envs = Array.isArray(req.envs) ? [...new Set(req.envs)] : [];
  for (const e of envs) if (typeof e !== "string" || !envBoard(e)) throw new HttpError(400, `unknown env ${String(e)}; one of ${Object.values(BOARD_ENV).join(", ")}`);

  let bots: BotInfo[] = [];
  if (!dryRun) {
    const allowed = await allowlist();
    const refused = names.filter((n) => !allowed.has(n));
    if (refused.length) throw new HttpError(403, `${refused.join(", ")} ${refused.length === 1 ? "is" : "are"} not on the allowlist (eyelab/config/bots.json); the pipeline flashes Kevin's bots only`);
    if (names.length && NO_FLASH) throw new HttpError(400, "flashing is off on this server (EYELAB_NO_FLASH=1)");
    if (!names.length && !envs.length) throw new HttpError(400, "choose at least one bot");
    if (names.length) {
      if (isRunning()) throw new HttpError(409, "a deploy is already running");
      const found = await scanBots();
      for (const n of names) {
        const b = found.find((x) => x.name === n);
        if (!b) throw new HttpError(400, `${n} did not answer the LAN scan; check it is on, then rescan`);
        if (!b.selectable) throw new HttpError(400, `${n}: ${b.reason ?? "cannot be flashed"}`);
        bots.push(b);
      }
    }
  }
  if (isRunning()) throw new HttpError(409, "a deploy is already running");

  const id = new Date().toISOString().replace(/[:.]/g, "-").replace("Z", "");
  const run = new Run(id, join(LOG_DIR, `deploy-${id}.log`));
  current = run;
  const allEnvs = [...new Set([...bots.map((b) => BOARD_ENV[b.board]), ...envs])];
  const steps: StepId[] = dryRun ? ["generate", "parity"] : ["generate", "parity", "version", "build", "commit", "flash", "render"];
  run.push({ type: "run", id, startedAt: new Date().toISOString(), dryRun, bots: bots.map((b) => b.name), envs: allEnvs, steps: steps.map((s) => ({ id: s, title: TITLES[s] })), logFile: run.logFile });
  void pipeline(run, { dryRun, bots, envs: allEnvs }).catch((err) => {
    run.push({ type: "error", message: err instanceof Error ? err.message : String(err) });
  }).finally(() => run.finish());
  return { id, logFile: run.logFile };
}

export function cancelDeploy(): boolean {
  if (!isRunning()) return false;
  current!.ac.abort();
  return true;
}
export function ackDeploy(): boolean { return isRunning() && current!.ack(); }

// ---------------------------------------------------------------- the pipeline

class StepFailed extends Error {}
type StepResult = { status?: StepEnd; summary: string };

interface Ctx {
  run: Run;
  dryRun: boolean;
  bots: BotInfo[];
  envs: string[];
  version: string | null;
  commit: string | null;
  flashed: BotInfo[];
  renders: RenderResult[];
}

const rel = (p: string) => join(REPO, p);
const git = (args: string[], ctx: Ctx, log?: (l: string) => void) => runProc("git", args, { cwd: REPO, capture: true, onLine: log, signal: ctx.run.ac.signal });

/** Waits ms, or rejects at once on cancel. */
function sleep(ms: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal.aborted) return reject(new Cancelled());
    const onAbort = () => { clearTimeout(t); reject(new Cancelled()); };
    const t = setTimeout(() => { signal.removeEventListener("abort", onAbort); resolve(); }, ms);
    signal.addEventListener("abort", onAbort, { once: true });
  });
}

async function step(ctx: Ctx, id: StepId, fn: (log: (l: string) => void) => Promise<StepResult>) {
  const { run } = ctx;
  const t0 = Date.now();
  run.push({ type: "step-start", step: id });
  const log = (line: string) => run.push({ type: "log", step: id, line });
  try {
    const r = await fn(log);
    run.push({ type: "step-end", step: id, status: r.status ?? "ok", summary: r.summary, ms: Date.now() - t0 });
    if (r.status === "fail") throw new StepFailed(r.summary);
  } catch (err) {
    if (err instanceof StepFailed) throw err;
    const msg = err instanceof Cancelled || run.ac.signal.aborted ? "cancelled" : err instanceof Error ? err.message : String(err);
    for (const l of msg.split("\n").slice(1)) log(l);
    run.push({ type: "step-end", step: id, status: "fail", summary: msg.split("\n")[0], ms: Date.now() - t0 });
    throw new StepFailed(msg);
  }
}

async function pipeline(run: Run, o: { dryRun: boolean; bots: BotInfo[]; envs: string[] }) {
  const ctx: Ctx = { run, ...o, version: null, commit: null, flashed: [], renders: [] };
  const headerBefore = await readFile(rel(HEADER), "utf8");
  const configBefore = await readFile(rel(CONFIG), "utf8");
  let ok = false;
  try {
    await step(ctx, "generate", (log) => generate(ctx, log));
    await step(ctx, "parity", (log) => parity(ctx, log));
    if (!o.dryRun) {
      await step(ctx, "version", (log) => bumpVersion(ctx, log));
      await step(ctx, "build", (log) => build(ctx, log));
      await step(ctx, "commit", (log) => commit(ctx, log));
      await step(ctx, "flash", (log) => flash(ctx, log));
      await step(ctx, "render", (log) => renderCheck(ctx, log));
    }
    ok = true;
  } catch (err) {
    if (!(err instanceof StepFailed)) throw err;
  } finally {
    // A dry run keeps no file changes; a failed run gives back the version it did not ship.
    if (o.dryRun && (await readFile(rel(HEADER), "utf8")) !== headerBefore) {
      await writeFile(rel(HEADER), headerBefore);
      run.push({ type: "log", step: "generate", line: `dry run: restored ${HEADER} to how it was before the run` });
    }
    if (!o.dryRun && !ctx.commit && (await readFile(rel(CONFIG), "utf8")) !== configBefore) {
      await writeFile(rel(CONFIG), configBefore);
      ctx.version = null;
      run.push({ type: "log", step: "version", line: `run stopped before the commit: restored ${CONFIG} (the version was not shipped)` });
    }
  }
  const cancelled = run.ac.signal.aborted;
  const summary = cancelled ? "Cancelled."
    : !ok ? "Stopped at the first failure."
    : o.dryRun ? "Dry run passed: the header generates and the firmware model matches the lab. No files changed."
    : `Deployed v${ctx.version}${ctx.flashed.length ? ` to ${ctx.flashed.map((b) => b.name).join(", ")}` : " (built, nothing flashed)"}.`;
  run.push({ type: "done", ok, cancelled, version: ctx.version, commit: ctx.commit, renders: ctx.renders, summary });
}

// 1. Generate
async function generate(ctx: Ctx, log: (l: string) => void): Promise<StepResult> {
  log(`$ python3 tools/gen_eye_params.py eyelab/faces > ${HEADER}`);
  const text = await runProc("python3", ["tools/gen_eye_params.py", "eyelab/faces"], { cwd: REPO, capture: true, onLine: log, signal: ctx.run.ac.signal });
  if (!text.includes("botEyeParams")) throw new Error("the generator printed no header");
  if ((await readFile(rel(HEADER), "utf8")) !== text) await writeFile(rel(HEADER), text);
  log(`$ git diff --stat -- ${HEADER}`);
  const stat = (await git(["diff", "--stat", "--", HEADER], ctx)).trim();
  if (!stat) { log("(no change from HEAD)"); return { summary: "header unchanged from HEAD" }; }
  stat.split("\n").forEach((l) => log(l));
  return { summary: stat.split("\n").pop()!.trim() };
}

// 2. Parity
async function parity(ctx: Ctx, log: (l: string) => void): Promise<StepResult> {
  const r = await runParity({ repo: REPO, log, signal: ctx.run.ac.signal });
  describeParity(r).forEach((l) => log(l));
  const worst = r.worst ? `worst ${r.worst.px} px (${r.worst.label})` : "all identical";
  if (!r.ok) return { status: "fail", summary: `${r.over.length} of ${r.frames} frames over the ${r.budget} px budget; ${worst}` };
  return { summary: `${r.identical} of ${r.frames} frames identical, ${worst}; budget ${r.budget} px` };
}

// 3. Version
// Plain x.y.z belongs to GitHub releases only. A lab deploy is a test build of the next
// release, tagged -eyes.N, so it never takes a release number and never collides with
// another branch's test builds (party mode uses -party.N). See PLAN.md, "Version rule".
const VERSION_RE = /(#define\s+FIRMWARE_VERSION\s+")([^"]+)(")/;
export function nextVersion(current: string): string {
  const plain = /^(\d+)\.(\d+)\.(\d+)$/.exec(current);
  if (plain) return `${plain[1]}.${plain[2]}.${Number(plain[3]) + 1}-eyes.1`;
  const eyes = /^(\d+\.\d+\.\d+)-eyes\.(\d+)$/.exec(current);
  if (eyes) return `${eyes[1]}-eyes.${Number(eyes[2]) + 1}`;
  const tag = /^\d+\.\d+\.\d+-([a-z]+)\.\d+$/.exec(current)?.[1];
  throw new Error(tag
    ? `FIRMWARE_VERSION is ${current}, a ${tag} test build; the eye lab only bumps a release or its own -eyes.N builds. Merge or release that branch first.`
    : `FIRMWARE_VERSION is ${current}; expected x.y.z or x.y.z-eyes.N`);
}
async function bumpVersion(ctx: Ctx, log: (l: string) => void): Promise<StepResult> {
  const text = await readFile(rel(CONFIG), "utf8");
  const m = VERSION_RE.exec(text);
  if (!m) throw new Error(`no #define FIRMWARE_VERSION "…" in ${CONFIG}`);
  const from = m[2], to = nextVersion(from);
  await writeFile(rel(CONFIG), text.replace(VERSION_RE, `$1${to}$3`));
  ctx.version = to;
  log(`${CONFIG}: FIRMWARE_VERSION ${from} -> ${to}`);
  return { summary: `${from} → ${to}` };
}

// 4. Build
const fmtMs = (ms: number) => (ms >= 60_000 ? `${Math.floor(ms / 60_000)}m ${Math.round((ms % 60_000) / 1000)}s` : `${(ms / 1000).toFixed(1)}s`);
export const binName = (board: string, version: string) => `vizbot-${board}-v${version}.bin`;
async function build(ctx: Ctx, log: (l: string) => void): Promise<StepResult> {
  if (!ctx.envs.length) return { status: "skip", summary: "no bots chosen, nothing to build" };
  if (!existsSync(PIO)) throw new Error(`${PIO} not found (install PlatformIO Core; never use Homebrew pio)`);
  const done: string[] = [];
  for (const env of ctx.envs) {
    const board = envBoard(env)!;
    const t0 = Date.now();
    log(`$ ~/.platformio/penv/bin/pio run -d vizbot -e ${env}`);
    await runProc(PIO, ["run", "-d", "vizbot", "-e", env], { cwd: REPO, onLine: log, signal: ctx.run.ac.signal });
    const bin = join("builds", binName(board, ctx.version!));
    const st = await stat(rel(bin)).catch(() => null);
    if (!st) throw new Error(`pio finished but ${bin} is missing (vizbot/name_firmware.py writes it)`);
    log(`${env}: ${bin}, ${(st.size / 1024).toFixed(0)} KB, built in ${fmtMs(Date.now() - t0)}`);
    done.push(`${bin} (${fmtMs(Date.now() - t0)})`);
  }
  return { summary: done.join(", ") };
}

// 5. Commit
async function commit(ctx: Ctx, log: (l: string) => void): Promise<StepResult> {
  // The faces changed since the last deploy commit, or since faces/ first landed.
  const lastDeploy = (await git(["log", "-1", "--format=%H", `--grep=^${DEPLOY_SUBJECT}`], ctx)).trim();
  const base = lastDeploy || (await git(["log", "--diff-filter=A", "--format=%H", "--", "eyelab/faces/globals.json"], ctx)).trim().split("\n").pop() || "HEAD";
  log(`$ git add -- ${COMMIT_PATHS.join(" ")}`);
  await git(["add", "--", ...COMMIT_PATHS], ctx, log);
  const staged = (await git(["diff", "--cached", "--name-only", "--", ...COMMIT_PATHS], ctx)).trim();
  if (!staged) return { status: "skip", summary: "nothing to commit in those paths" };
  staged.split("\n").forEach((f) => log(`staged ${f}`));
  const changed = (await git(["diff", "--cached", "--name-only", base, "--", "eyelab/faces"], ctx)).trim().split("\n").filter(Boolean);
  const faces = changed.map((p) => p.split("/").pop()!).map((f) => {
    const m = /^(\d{2})-(.+)\.json$/.exec(f);
    return m ? `${m[1]} ${m[2].toUpperCase().replace(/-/g, "_")}` : f.replace(/\.json$/, "");
  });
  const since = lastDeploy ? `the last deploy (${lastDeploy.slice(0, 7)})` : `faces/ first landed (${base.slice(0, 7)})`;
  const body = faces.length ? `Faces changed since ${since}:\n${faces.map((f) => `- ${f}`).join("\n")}` : `No face changes since ${since}.`;
  const subject = `${DEPLOY_SUBJECT}${ctx.version}`;
  log(`$ git commit -m "${subject}" -- ${COMMIT_PATHS.join(" ")}`);
  body.split("\n").forEach((l) => log(`  ${l}`));
  await git(["commit", "-m", subject, "-m", body, "--", ...COMMIT_PATHS], ctx, log);
  ctx.commit = (await git(["rev-parse", "--short", "HEAD"], ctx)).trim();
  log(`committed ${ctx.commit} (not pushed)`);
  return { summary: `${ctx.commit} ${subject}, ${faces.length} face${faces.length === 1 ? "" : "s"} changed` };
}

// 6. Flash (OTA)
const isStackchan = (b: BotInfo) => b.board === "stackchan";
async function waitForVersion(ctx: Ctx, b: BotInfo, log: (l: string) => void, limitMs: number): Promise<number> {
  const t0 = Date.now(), signal = ctx.run.ac.signal;
  let last = "";
  while (Date.now() - t0 < limitMs) {
    await sleep(3000, signal);
    try {
      const s = await getState(b.ip, 2500, signal);
      if (s.firmwareVersion === ctx.version) return Date.now() - t0;
      if (s.firmwareVersion !== last) { last = s.firmwareVersion; log(`${b.name} answers, still on ${last}`); }
    } catch (err) {
      if (signal.aborted) throw new Cancelled();
      if (last !== "offline") { last = "offline"; log(`${b.name} is rebooting (no answer yet)`); }
    }
  }
  throw new Error(`${b.name} did not come back on v${ctx.version} within ${Math.round(limitMs / 1000)} s`);
}
async function flashOne(ctx: Ctx, b: BotInfo, log: (l: string) => void) {
  const signal = ctx.run.ac.signal;
  // Check the bot at this IP is still the one chosen, and still Kevin's.
  const s = await getState(b.ip, 3000, signal);
  const allowed = await allowlist();
  if (s.deviceName !== b.name) throw new Error(`${b.ip} is now ${s.deviceName}, not ${b.name}; rescan and try again`);
  if (!allowed.has(s.deviceName)) throw new Error(`${s.deviceName} is not on the allowlist; refusing to flash it`);
  if (s.boardType !== b.board) throw new Error(`${b.name} reports board ${s.boardType}, expected ${b.board}`);
  log(`${b.name} (${b.board}) at ${b.ip} is on v${s.firmwareVersion}`);

  const name = binName(b.board, ctx.version!);
  const bin = await readFile(rel(join("builds", name)));
  for (let attempt = 1; ; attempt++) {
    log(`POST http://${b.ip}/update  firmware=${name} (${(bin.length / 1024).toFixed(0)} KB)${attempt > 1 ? ", retry" : ""}`);
    let reply = "";
    try { reply = await uploadFirmware(b.ip, bin, name, signal); } catch (err) {
      if (signal.aborted) throw new Cancelled();
      log(`upload: ${err instanceof Error ? err.message : String(err)}`);
    }
    let parsed: { success?: boolean; error?: string; message?: string } | null = null;
    try { parsed = reply ? JSON.parse(reply) : null; } catch { /* not JSON */ }
    if (parsed?.success === true) { log(`${b.name}: ${parsed.message ?? "success"}`); break; }
    log(reply ? `${b.name} answered: ${parsed?.error ?? reply.slice(0, 200)}` : `${b.name} answered nothing`);
    // An empty answer can still mean it took the image and rebooted: look before uploading again.
    await sleep(5000, signal);
    const now = await getState(b.ip, 2500, signal).catch(() => null);
    if (now?.firmwareVersion === ctx.version) { log(`${b.name} is already on v${ctx.version}`); break; }
    if (attempt >= 2) throw new Error(`${b.name}: upload failed twice`);
  }
  const ms = await waitForVersion(ctx, b, log, 90_000);
  log(`${b.name} is back on v${ctx.version} after ${fmtMs(ms)}`);

  if (isStackchan(b)) {
    // Without a cold boot after a flash the Stackchan head stays dead (servo rail).
    const message = `Power-cycle ${b.name} now: hold the bottom button about 6 s until it powers off, then turn it back on. Press Done when it is back on.`;
    ctx.run.push({ type: "waiting", step: "flash", bot: b.name, message });
    await ctx.run.waitAck();
    ctx.run.push({ type: "resumed", step: "flash" });
    log(`${b.name}: power-cycled, waiting for it to answer`);
    const back = await waitForVersion(ctx, b, log, 90_000);
    log(`${b.name} answers on v${ctx.version} after ${fmtMs(back)}`);
  }
}
async function flash(ctx: Ctx, log: (l: string) => void): Promise<StepResult> {
  if (!ctx.bots.length) return { status: "skip", summary: "no bots chosen" };
  if (NO_FLASH) throw new Error("flashing is off on this server (EYELAB_NO_FLASH=1)");
  for (const b of ctx.bots) {
    await flashOne(ctx, b, log);
    ctx.flashed.push(b);
  }
  return { summary: `${ctx.flashed.map((b) => b.name).join(", ")} on v${ctx.version}` };
}

// 7. Render check
async function renderOne(ctx: Ctx, b: BotInfo, log: (l: string) => void): Promise<RenderResult> {
  const signal = ctx.run.ac.signal;
  const first = await getState(b.ip, 3000, signal); // reading /state starts a fresh faceRender window
  await sleep(10_000, signal);
  const s = await getState(b.ip, 3000, signal);
  const fr = s.faceRender && typeof s.faceRender.renders === "number" ? s.faceRender : null;
  const base = { bot: b.name, ip: b.ip, firmware: s.firmwareVersion, faceRender: fr };
  if (!fr) return { ...base, status: "fail", note: "no faceRender in /state (is the firmware older than 3.7.39?)" };
  const line = `avg ${(fr.avgUs / 1000).toFixed(1)} ms, max ${(fr.maxUs / 1000).toFixed(1)} ms, ${fr.renders} renders, ${fr.skipped} skipped in 10 s`;
  log(`${b.name}: ${line}${first.firmwareVersion !== ctx.version ? ` (was on ${first.firmwareVersion})` : ""}`);
  if (fr.renders <= 0) return { ...base, status: "fail", note: `no face renders in 10 s: is ${b.name} showing its face? ${line}` };
  if (fr.avgUs > RENDER_WARN_US) return { ...base, status: "warn", note: `${line}: over half the 20 fps budget` };
  return { ...base, status: "ok", note: line };
}
async function renderCheck(ctx: Ctx, log: (l: string) => void): Promise<StepResult> {
  if (!ctx.flashed.length) return { status: "skip", summary: "nothing flashed" };
  log("reading /state to start a fresh faceRender window, then again after 10 s");
  ctx.renders = await Promise.all(ctx.flashed.map((b) => renderOne(ctx, b, log)));
  const worst: StepEnd = ctx.renders.some((r) => r.status === "fail") ? "fail" : ctx.renders.some((r) => r.status === "warn") ? "warn" : "ok";
  return { status: worst, summary: ctx.renders.map((r) => `${r.bot}: ${r.note}`).join("; ") };
}
