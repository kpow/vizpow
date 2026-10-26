// Lab side of the parity check: renders the same frames as parity/test.cpp, in the same
// order, with the after model in eyelab/shared and the face files, no browser. Writes the
// raw RGB565 frames and a label per frame, so parity.ts can name the worst one.
//
//   tsx parity/lab.ts <facesDir> <out.bin> <labels.json>
//
// Keep the frame list in step with test.cpp.

import { writeFile } from "node:fs/promises";
import { readFaces } from "../server/faces";
import { Frame } from "../shared/raster";
import { createLab } from "../shared/lab";
import { renderAfter } from "../shared/model";
import { afterSetExpr, stepAfter } from "../shared/motion";
import { DEVICES } from "../shared/state";
import type { Device, Lab } from "../shared/state";

/** Transitions both sides render: [from, to] by face index. */
const PAIRS: [number, number][] = [[0, 6], [16, 20], [0, 7], [2, 1], [4, 3], [0, 24], [12, 9], [13, 18], [0, 21]];
const W = 240, H = 280;
const D: Device = { ...DEVICES.lcd169, w: W, h: H, cx: 120, cy: 118, k: 1 };

async function main() {
  const [facesDir, outPath, labelsPath] = process.argv.slice(2);
  if (!facesDir || !outPath || !labelsPath) throw new Error("usage: tsx parity/lab.ts <facesDir> <out.bin> <labels.json>");
  const { faces, globals } = await readFaces(facesDir);
  const names = faces.map((f) => f.name);
  const out: Buffer[] = [];
  const labels: string[] = [];

  const make = (): Lab => { const L = createLab(faces, globals); L.ui.idle = false; return L; };
  const frame = (L: Lab, now: number, ambient: boolean, label: string, k = 1) => {
    const fr = new Frame(W, H);
    fr.clear(ambient ? 0x1234 : 0); // the firmware's caller clears; the ambient stroke is what differs
    L.ui.ambient = ambient;
    renderAfter(L, fr, { ...D, k }, now, 0xFFFF, () => {});
    out.push(Buffer.from(fr.px.buffer.slice(0)));
    labels.push(label);
  };

  { // every face at rest, plain and ambient
    const L = make();
    for (let i = 0; i < faces.length; i++) { afterSetExpr(L, i, 0, true); frame(L, 0, false, `${names[i]} rest`); frame(L, 0, true, `${names[i]} rest ambient`); }
  }
  for (const [a, b] of PAIRS) { // transitions, idle off, 50 ms frames
    const L = make(); let now = 100000;
    afterSetExpr(L, a, now, true);
    for (let f = 0; f < 4; f++) { now += 50; stepAfter(L, now, .05); }
    afterSetExpr(L, b, now, false);
    for (let f = 0; f < 24; f++) { now += 50; stepAfter(L, now, .05); frame(L, now, false, `${names[a]} -> ${names[b]} +${(f + 1) * 50}ms`); }
  }
  { // sleep
    const L = make(); let now = 100000; afterSetExpr(L, 1, now, true); L.ui.sleep = true;
    for (let f = 0; f < 30; f++) { now += 50; stepAfter(L, now, .05); frame(L, now, false, `sleep +${(f + 1) * 50}ms`); }
  }
  { // pendant: neutral to happy
    const L = make(); let now = 100000; L.ui.pendant = true; afterSetExpr(L, 0, now, true);
    for (let f = 0; f < 2; f++) { now += 50; stepAfter(L, now, .05); }
    afterSetExpr(L, 1, now, false);
    for (let f = 0; f < 20; f++) { now += 50; stepAfter(L, now, .05); frame(L, now, false, `pendant ${names[0]} -> ${names[1]} +${(f + 1) * 50}ms`); }
  }
  { // look pad
    const L = make(); let now = 100000; afterSetExpr(L, 0, now, true); L.pad.x = .8; L.pad.y = -.5;
    for (let f = 0; f < 20; f++) { now += 50; stepAfter(L, now, .05); frame(L, now, false, `look pad +${(f + 1) * 50}ms`); }
  }
  { // chip scale
    const L = make();
    for (let i = 0; i < faces.length; i++) { afterSetExpr(L, i, 0, true); frame(L, 0, false, `${names[i]} x0.25`, .25); frame(L, 0, true, `${names[i]} x0.36 ambient`, .36); }
  }
  { // glitch bands, fixed
    const L = make(); let now = 100000; afterSetExpr(L, 23, now, true);
    const B = [[[.1, .2, .15], [.4, .12, -.18], [.45, .25, .11]], [[.6, .15, -.2], [.05, .1, .13], [.3, .28, -.12]]];
    for (let e = 0; e < 2; e++) { L.AS.glitch[e].bands = B[e].map((b) => ({ y0: b[0], h: b[1], dx: Math.fround(b[2]) })); L.AS.glitch[e].next = 1e12; }
    frame(L, now, false, "glitch bands"); frame(L, now, true, "glitch bands ambient");
    L.pad.x = 1; L.pad.y = .6;
    for (let f = 0; f < 10; f++) { now += 50; stepAfter(L, now, .05); }
    frame(L, now, false, "glitch bands + look pad");
  }

  await writeFile(outPath, Buffer.concat(out));
  await writeFile(labelsPath, JSON.stringify({ w: W, h: H, labels }) + "\n");
}

main().catch((err) => { console.error(err instanceof Error ? err.message : err); process.exit(1); });
