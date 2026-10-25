// Saves edits to the face files. Each face (and globals.json) is its own document: an edit
// marks it dirty and schedules a write about 800 ms after the last input (at once for
// clicks), with one write in flight per document and nothing sent when the file already
// holds the same values. Also holds the HEAD baselines that "edited" and "tuned" compare to.

import type { Face, Globals } from "../../shared/faces";
import { sameVal } from "../../shared/faces";
import * as api from "./api";
import { ApiError } from "./api";

export type SavePath = "globals" | `face/${number}`;
export const facePath = (i: number): SavePath => `face/${i}`;

export class Saver {
  /** The face at HEAD, null when it is not committed yet, undefined until loaded. */
  baselines: (Face | null | undefined)[] = [];
  globalsBase: Globals | null | undefined = undefined;
  loaded = false;
  loadError = "";
  err = "";
  dirty = new Set<SavePath>();
  inflight = 0;
  private ver: Partial<Record<SavePath, number>> = {};
  private timers: Partial<Record<SavePath, ReturnType<typeof setTimeout>>> = {};
  private chain: Partial<Record<SavePath, Promise<unknown>>> = {};
  private known: Partial<Record<SavePath, string>> = {}; // what the file holds, as JSON

  constructor(private build: (path: SavePath) => Face | Globals, private onChange: () => void) {}

  /** After the first GET: record what the files hold, then fetch the HEAD baselines. */
  async init(faces: Face[], globals: Globals) {
    faces.forEach((f) => { this.known[facePath(f.index)] = JSON.stringify(f); });
    this.known.globals = JSON.stringify(globals);
    this.loaded = true; this.onChange();
    const [bases, g] = await Promise.all([
      Promise.all(faces.map((f) => api.getFaceBaseline(f.index).then((r) => r.face, () => null))),
      api.getGlobalsBaseline().then((r) => r.globals, () => null),
    ]);
    this.baselines = bases; this.globalsBase = g; this.onChange();
  }

  /** A local edit. Saves after `delay` ms of quiet (0 for clicks). */
  touch(path: SavePath, delay = 800) {
    if (!this.loaded) return;
    this.dirty.add(path); this.ver[path] = (this.ver[path] || 0) + 1;
    clearTimeout(this.timers[path]);
    this.timers[path] = setTimeout(() => { delete this.timers[path]; this.flush(path); }, delay);
    this.onChange();
  }

  private flush(path: SavePath) {
    if (this.timers[path]) return;
    const v = this.ver[path] || 0;
    this.inflight++; this.onChange();
    const run = async () => {
      const body = this.build(path), text = JSON.stringify(body);
      if (this.known[path] === text) return; // the file already holds this
      if (path === "globals") await api.putGlobals(body as Globals); else await api.putFace(body as Face);
      this.known[path] = text;
    };
    const p = (this.chain[path] || Promise.resolve()).then(run); // one write at a time per document
    this.chain[path] = p.catch(() => {});
    p.then(() => { this.err = ""; if (this.ver[path] === v && !this.timers[path]) this.dirty.delete(path); },
      (e) => { this.err = writeError(e); })
      .finally(() => { this.inflight--; this.onChange(); });
  }

  /** Restores a document from HEAD, after any write already in flight for it. */
  reset<T>(path: SavePath, fn: () => Promise<T>): Promise<T> {
    clearTimeout(this.timers[path]); delete this.timers[path];
    this.ver[path] = (this.ver[path] || 0) + 1;
    const p = (this.chain[path] || Promise.resolve()).then(fn);
    this.chain[path] = p.catch(() => {});
    return p.then((r) => {
      const v = r as unknown as { face?: Face; globals?: Globals };
      this.known[path] = JSON.stringify(v.face ?? v.globals);
      this.dirty.delete(path); this.err = ""; this.onChange();
      return r;
    });
  }

  /** "edited since the last commit" for a whole face (eye, ticks, brows, mouth). */
  faceEdited(i: number, face: Face) {
    const b = this.baselines[i];
    return !!b && !sameVal(face, b);
  }

  status(): { s: string; text: string } {
    if (this.loadError) return { s: "error", text: this.loadError };
    if (!this.loaded) return { s: "pending", text: "Loading faces…" };
    if (this.inflight) return { s: "saving", text: "Saving…" };
    if (this.err) return { s: "error", text: this.err };
    if (this.dirty.size) return { s: "dirty", text: "Unsaved changes" };
    return { s: "saved", text: "Saved" };
  }
}

function writeError(e: unknown) {
  if (e instanceof ApiError && e.status === 0) return "Not saved: the eyelab server did not answer. Your tweak is still on screen; start the server (npm run dev in eyelab/), then move a slider to retry.";
  if (e instanceof ApiError && e.status === 400) return `Not saved: the server rejected this tweak (${e.message}). Use Copy JSON to keep a copy, then reload the page.`;
  return `Not saved: ${e instanceof Error ? e.message : String(e)}. Your tweak is still on screen; move a slider to retry.`;
}
