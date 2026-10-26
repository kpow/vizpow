// The parity check: proves the firmware eye model draws what the lab draws. Host-compiles
// vizbot/bot_eye_model.h with a bot_eye_params.h (test.cpp), renders the same frames from
// eyelab/shared and the face files (lab.ts), and compares them frame by frame. A frame may
// differ by a few pixels of float rounding (the firmware is single precision, the lab is
// double); more than PIXEL_BUDGET means the two models disagree. The deploy pipeline and
// `npm run parity` both call runParity. Build outputs go to parity/build/ (gitignored).

import { copyFile, mkdir, readFile } from "node:fs/promises";
import { join } from "node:path";
import { runProc } from "../server/proc";

/** Most pixels a frame may differ by. Last measured: 388 of 389 identical, one off by 2 px. */
export const PIXEL_BUDGET = 8;

export interface ParityOpts {
  /** The vizpow checkout. */
  repo: string;
  /** The params header to test. Defaults to vizbot/bot_eye_params.h. */
  params?: string;
  /** The face files the lab renders. Defaults to eyelab/faces. */
  faces?: string;
  log: (line: string) => void;
  signal?: AbortSignal;
}
export interface FrameDiff { frame: number; label: string; px: number }
export interface ParityResult { ok: boolean; budget: number; frames: number; identical: number; worst: FrameDiff | null; over: FrameDiff[] }

export async function runParity(o: ParityOpts): Promise<ParityResult> {
  const eyelab = join(o.repo, "eyelab"), vizbot = join(o.repo, "vizbot");
  const here = join(eyelab, "parity"), build = join(here, "build");
  await mkdir(build, { recursive: true });
  // Copies, so the model's #include "config.h" resolves to the stub and not the firmware's config.
  await copyFile(join(vizbot, "bot_eye_model.h"), join(build, "bot_eye_model.h"));
  await copyFile(join(vizbot, "bot_faces.h"), join(build, "bot_faces.h"));
  await copyFile(o.params ?? join(vizbot, "bot_eye_params.h"), join(build, "bot_eye_params.h"));

  const sub = { cwd: o.repo, onLine: (l: string) => o.log(`  ${l}`), signal: o.signal };
  const firmware = async () => {
    o.log("firmware: c++ -std=c++17 vizbot/bot_eye_model.h + bot_eye_params.h");
    await runProc("c++", ["-std=c++17", "-O1", "-I", build, "-I", join(here, "stub"), "-I", vizbot, join(here, "test.cpp"), "-o", join(build, "fw")], sub);
    await runProc(join(build, "fw"), [join(build, "fw.bin")], sub);
  };
  const lab = async () => {
    o.log("lab: tsx parity/lab.ts (eyelab/shared + face files)");
    await runProc(join(eyelab, "node_modules", ".bin", "tsx"), [join(here, "lab.ts"), o.faces ?? join(eyelab, "faces"), join(build, "lab.bin"), join(build, "labels.json")], { ...sub, cwd: eyelab });
  };
  await Promise.all([firmware(), lab()]);

  const { w, h, labels } = JSON.parse(await readFile(join(build, "labels.json"), "utf8")) as { w: number; h: number; labels: string[] };
  const fw = await readFile(join(build, "fw.bin")), lb = await readFile(join(build, "lab.bin"));
  const a = new Uint16Array(fw.buffer, fw.byteOffset, fw.byteLength / 2), b = new Uint16Array(lb.buffer, lb.byteOffset, lb.byteLength / 2);
  const N = w * h, nA = a.length / N, nB = b.length / N;
  if (nA !== nB) throw new Error(`frame counts differ: firmware ${nA}, lab ${nB} (do test.cpp and lab.ts render the same list? same number of faces?)`);

  let identical = 0, worst: FrameDiff | null = null;
  const over: FrameDiff[] = [];
  for (let f = 0; f < nA; f++) {
    let px = 0;
    for (let i = f * N, end = i + N; i < end; i++) if (a[i] !== b[i]) px++;
    const d = { frame: f, label: labels[f] ?? `frame ${f}`, px };
    if (px === 0) identical++;
    if (!worst || px > worst.px) worst = d;
    if (px > PIXEL_BUDGET) over.push(d);
  }
  return { ok: over.length === 0, budget: PIXEL_BUDGET, frames: nA, identical, worst: worst && worst.px > 0 ? worst : null, over };
}

/** The step's summary lines. */
export function describeParity(r: ParityResult): string[] {
  const out = [
    `budget: ${r.budget} px per frame`,
    `${r.frames} frames compared, ${r.identical} identical`,
    r.worst ? `worst frame: ${r.worst.label} (#${r.worst.frame}), ${r.worst.px} px` : "worst frame: none, every frame is identical",
  ];
  if (!r.ok) {
    out.push(`${r.over.length} frame${r.over.length === 1 ? "" : "s"} over budget:`);
    for (const d of [...r.over].sort((x, y) => y.px - x.px).slice(0, 5)) out.push(`  ${d.label} (#${d.frame}): ${d.px} px`);
  }
  return out;
}
