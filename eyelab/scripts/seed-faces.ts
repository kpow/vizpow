// One-off: writes eyelab/faces/ from the frozen round 4 export (tools/eye_params_r4.json,
// Kevin's seven tweaks included) and the brow/mouth columns of the lab's ROWS table, after
// checking those columns against the BotExpression table in vizbot/bot_faces.h.
// Kept so the seed is reproducible:  npx tsx scripts/seed-faces.ts   (overwrites faces/)

import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ROWS, rowToExpr } from "../shared/before";
import { MOUTH_TYPES, faceFileName, globalsToFile, faceToFile, EYE_KEYS } from "../shared/faces";
import type { Face, Ticks } from "../shared/faces";

const HERE = new URL("..", import.meta.url).pathname; // eyelab/
const REPO = join(HERE, "..");
const FACES = join(HERE, "faces");

const exported = JSON.parse(readFileSync(join(REPO, "tools/eye_params_r4.json"), "utf8"));

// The firmware table, as the source the ROWS columns must agree with.
const src = readFileSync(join(REPO, "vizbot/bot_faces.h"), "utf8");
const table = src.slice(src.indexOf("botExpressions[BOT_NUM_EXPRESSIONS]"));
const blocks = table.slice(0, table.indexOf("};")).split(/\n\s*\{\s*\n/).slice(1);
const fw = blocks.map((b) => {
  const o: Record<string, string> = {};
  for (const m of b.matchAll(/\.(\w+)\s*=\s*([-\w]+)/g)) o[m[1]] = m[2];
  return o;
});
if (fw.length !== ROWS.length) throw new Error(`bot_faces.h has ${fw.length} rows, the lab has ${ROWS.length}`);

// Kevin accepted rule A4, so KISSING's criterion 1 (mouth-led) is ticked.
const TICKS: Record<number, Ticks> = { 21: [1, 0, 0, 0, 0, 0] };

mkdirSync(FACES, { recursive: true });
for (const e of exported.expressions) {
  const i: number = e.index, r = rowToExpr(ROWS[i]), f = fw[i];
  if (e.name !== r.name) throw new Error(`face ${i}: export says ${e.name}, ROWS says ${r.name}`);
  const want: Record<string, string> = {
    browOffsetY: String(r.browOffsetY), browLength: String(r.browLength), browThickness: String(r.browThickness),
    browAngleL: String(r.browAngleL), browAngleR: String(r.browAngleR), browVisible: String(r.browVisible),
    mouthType: "MOUTH_" + MOUTH_TYPES[r.mouthType], mouthWidth: String(r.mouthWidth), mouthOffsetY: String(r.mouthOffsetY), mouthCurve: String(r.mouthCurve),
  };
  for (const [k, v] of Object.entries(want)) if (f[k] !== v) throw new Error(`face ${i} ${r.name}: ${k} is ${f[k]} in bot_faces.h, ${v} in ROWS`);

  const eye: Record<string, unknown> = {};
  for (const k of EYE_KEYS) eye[k] = e[k];
  for (const k of ["preset", "gazeX", "gazeY", "pop", "transitionMs", "overlay", "gaze", "pulse", "exitBlink", "entry", "noBlinkMs", "blinkRate"]) eye[k] = e[k];
  const face = {
    index: i, name: r.name, kevinTuned: !!e.kevinTuned, eye,
    brow: { offsetY: r.browOffsetY, length: r.browLength, thickness: r.browThickness, angleL: r.browAngleL, angleR: r.browAngleR, visible: r.browVisible },
    mouth: { type: MOUTH_TYPES[r.mouthType], width: r.mouthWidth, offsetY: r.mouthOffsetY, curve: r.mouthCurve },
    scorecard: TICKS[i] || [0, 0, 0, 0, 0, 0],
  } as unknown as Face;
  writeFileSync(join(FACES, faceFileName(i, r.name)), faceToFile(face));
}
writeFileSync(join(FACES, "globals.json"), globalsToFile(exported.globals));
console.log(`wrote ${exported.expressions.length} faces and globals.json to ${FACES}`);
