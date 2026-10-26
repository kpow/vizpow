// Reads and writes the face files in eyelab/faces/. Git is the history: the "baseline"
// of a face is its version at HEAD, and reset restores that version. Writes go through a
// temp file and a rename so a crash never leaves a half-written face, and are queued per
// file so two saves of one face never interleave.

import { execFile } from "node:child_process";
import { readFile, readdir, rename, writeFile } from "node:fs/promises";
import { join, relative } from "node:path";
import { promisify } from "node:util";
import { faceToFile, globalsToFile, validateFace, validateGlobals } from "../shared/faces";
import type { Face, Globals } from "../shared/faces";

const run = promisify(execFile);

export const ROOT = new URL("..", import.meta.url).pathname; // eyelab/
export const FACES_DIR = join(ROOT, "faces");
const GLOBALS = "globals.json";
const FACE_FILE = /^(\d{2})-[a-z0-9-]+\.json$/;

/** A request problem the client should see as-is (400/403/404/409). */
export class HttpError extends Error {
  constructor(public status: 400 | 403 | 404 | 409, message: string) { super(message); }
}

async function faceFiles(dir = FACES_DIR): Promise<Map<number, string>> {
  const out = new Map<number, string>();
  for (const name of await readdir(dir)) {
    const m = FACE_FILE.exec(name);
    if (m) out.set(Number(m[1]), name);
  }
  return out;
}
async function fileFor(index: number) {
  const name = (await faceFiles()).get(index);
  if (!name) throw new HttpError(404, `no face ${index}`);
  return name;
}

/** Every face file in index order plus globals, validated. `dir` defaults to eyelab/faces (the parity check passes its own). */
export async function readFaces(dir = FACES_DIR): Promise<{ faces: Face[]; globals: Globals }> {
  const files = [...(await faceFiles(dir)).entries()].sort((a, b) => a[0] - b[0]);
  const faces: Face[] = [];
  for (const [index, name] of files) {
    const face = validateFace(JSON.parse(await readFile(join(dir, name), "utf8")));
    if (face.index !== index) throw new Error(`${name}: index ${face.index} does not match the file name`);
    faces.push(face);
  }
  faces.forEach((f, n) => { if (f.index !== n) throw new Error(`faces/: face ${n} is missing`); });
  const globals = validateGlobals(JSON.parse(await readFile(join(dir, GLOBALS), "utf8")));
  return { faces, globals };
}

// ---------- writes: one at a time per file, atomically

const queues = new Map<string, Promise<unknown>>();
function queued<T>(name: string, job: () => Promise<T>): Promise<T> {
  const next = (queues.get(name) ?? Promise.resolve()).then(job, job);
  queues.set(name, next.catch(() => {}));
  return next;
}
async function writeAtomic(name: string, text: string) {
  const path = join(FACES_DIR, name);
  const current = await readFile(path, "utf8").catch(() => null);
  if (current === text) return false; // nothing changed: leave the file and its mtime alone
  const tmp = join(FACES_DIR, `.${name}.${process.pid}.${Date.now()}.tmp`);
  await writeFile(tmp, text);
  await rename(tmp, path);
  return true;
}

export async function writeFace(index: number, body: unknown) {
  const name = await fileFor(index);
  let text: string;
  try { text = faceToFile(body); } catch (err) { throw new HttpError(400, (err as Error).message); }
  const face = validateFace(body);
  if (face.index !== index) throw new HttpError(400, `body is face ${face.index}, the URL says ${index}`);
  return queued(name, async () => {
    const onDisk = validateFace(JSON.parse(await readFile(join(FACES_DIR, name), "utf8")));
    if (onDisk.name !== face.name) throw new HttpError(400, `face ${index} is ${onDisk.name}; names never change`);
    return { written: await writeAtomic(name, text) };
  });
}

export async function writeGlobals(body: unknown) {
  let text: string;
  try { text = globalsToFile(body); } catch (err) { throw new HttpError(400, (err as Error).message); }
  return queued(GLOBALS, async () => ({ written: await writeAtomic(GLOBALS, text) }));
}

// ---------- git: HEAD is the baseline

async function gitPath(name: string) {
  const { stdout } = await run("git", ["rev-parse", "--show-toplevel"], { cwd: FACES_DIR });
  return relative(stdout.trim(), join(FACES_DIR, name)).split("\\").join("/");
}
/** The file's text at HEAD, or null when the last commit does not have it. */
async function headText(name: string): Promise<string | null> {
  const path = await gitPath(name);
  try {
    const { stdout } = await run("git", ["show", `HEAD:${path}`], { cwd: FACES_DIR, maxBuffer: 1 << 20 });
    return stdout;
  } catch {
    return null;
  }
}

export async function faceBaseline(index: number): Promise<Face | null> {
  const text = await headText(await fileFor(index));
  return text === null ? null : validateFace(JSON.parse(text));
}
export async function globalsBaseline(): Promise<Globals | null> {
  const text = await headText(GLOBALS);
  return text === null ? null : validateGlobals(JSON.parse(text));
}

/** Restores the file to its HEAD bytes and returns the restored face. */
export async function resetFace(index: number): Promise<Face> {
  const name = await fileFor(index);
  return queued(name, async () => {
    const text = await headText(name);
    if (text === null) throw new HttpError(409, `${name} is not in the last commit yet, so there is nothing to reset to`);
    const face = validateFace(JSON.parse(text));
    await writeAtomic(name, text);
    return face;
  });
}
export async function resetGlobals(): Promise<Globals> {
  return queued(GLOBALS, async () => {
    const text = await headText(GLOBALS);
    if (text === null) throw new HttpError(409, `${GLOBALS} is not in the last commit yet, so there is nothing to reset to`);
    const globals = validateGlobals(JSON.parse(text));
    await writeAtomic(GLOBALS, text);
    return globals;
  });
}
