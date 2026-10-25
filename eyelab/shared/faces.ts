// The face file format (eyelab/faces/NN-name.json and globals.json), shared by the server,
// the client and the seed script: types, key order, validation and the stable JSON writer.
// Faces are files in git, so the writer keeps key order fixed and arrays on one line for
// readable diffs, and the validator is the only gate between a request body and the disk.

export const SHAPE_KEYS = ["aspect", "scale", "lidTop", "slant", "sag", "lidBot", "bow", "shut", "cup", "inset", "pdy"] as const;
export const PUP_KEYS = ["pw", "ph", "heart", "star"] as const;
export const EYE_KEYS = [...SHAPE_KEYS, ...PUP_KEYS] as const;
export type ShapeKey = (typeof SHAPE_KEYS)[number];
export type PupKey = (typeof PUP_KEYS)[number];
export type PairKey = (typeof EYE_KEYS)[number];
export type Pair = [number, number];

export const OVERLAYS = ["none", "x", "spiral", "glitch"] as const;
export const GAZES = ["idle", "thinking", "nervous", "worried", "shy", "focused", "annoyed", "shocked"] as const;
export const ENTRIES = ["", "small", "eyeroll"] as const;
export type Overlay = (typeof OVERLAYS)[number];
export type GazePattern = (typeof GAZES)[number];
export type Entry = (typeof ENTRIES)[number];

// Pupil presets (brief §3). An eye's `preset` names one of these, or "" when hand-tuned.
export const PRESETS: Record<string, Partial<Record<PairKey, number | Pair>>> = {
  circle: { pw: .27, ph: .27 }, small: { pw: .18, ph: .18 }, dot: { pw: .08, ph: .08 }, big: { pw: .36, ph: .36 },
  plead: { pw: .32, ph: .34, pdy: .10 }, joy: { pw: .30, ph: .30, pdy: -.12 }, peek: { pw: .20, ph: .20, pdy: -.18 },
  odd: { pw: [.16, .26], ph: [.16, .26] }, heart: { pw: .42, ph: .42, heart: 1 }, star: { pw: .42, ph: .42, star: 1 },
};

/** The after model's parameters for one face. Per-eye keys are [screen-left, screen-right]. */
export type EyeParams = Record<PairKey, Pair> & {
  preset: string;
  gazeX: number;
  gazeY: number;
  pop: number;
  transitionMs: number;
  overlay: Overlay;
  gaze: GazePattern;
  pulse: boolean;
  exitBlink: boolean;
  entry: Entry;
  noBlinkMs: number;
  blinkRate: number;
};

export const MOUTH_TYPES = ["NONE", "LINE", "SMILE", "FROWN", "OPEN_O", "GRIN", "WAVY", "SMIRK", "TEETH", "TONGUE", "ZIGZAG", "POUT", "WHISTLE", "FLAT_FROWN"] as const;
export type MouthType = (typeof MOUTH_TYPES)[number];

/** Brow columns of the firmware's BotExpression row (bot_faces.h). Read-only in phase 1. */
export interface Brow { offsetY: number; length: number; thickness: number; angleL: number; angleR: number; visible: boolean }
/** Mouth columns of the firmware's BotExpression row. Read-only in phase 1. */
export interface Mouth { type: MouthType; width: number; offsetY: number; curve: number }

export type Ticks = [number, number, number, number, number, number];

export interface Face {
  index: number;
  name: string;
  kevinTuned: boolean;
  eye: EyeParams;
  brow: Brow;
  mouth: Mouth;
  scorecard: Ticks;
}

export interface Globals { R: number; gap: number; arcW: number; spin: number }

// Key order on disk. Anything not listed is rejected by the validator.
const FACE_KEYS = ["index", "name", "kevinTuned", "eye", "brow", "mouth", "scorecard"] as const;
const EYE_SCALAR_KEYS = ["preset", "gazeX", "gazeY", "pop", "transitionMs", "overlay", "gaze", "pulse", "exitBlink", "entry", "noBlinkMs", "blinkRate"] as const;
const EYE_ORDER = [...EYE_KEYS, ...EYE_SCALAR_KEYS] as const;
const BROW_KEYS = ["offsetY", "length", "thickness", "angleL", "angleR", "visible"] as const;
const MOUTH_KEYS = ["type", "width", "offsetY", "curve"] as const;
const GLOBAL_KEYS = ["R", "gap", "arcW", "spin"] as const;

// Allowed ranges. Slider keys use the lab's slider ranges (the UI cannot leave them).
const RANGE: Record<string, [number, number, boolean?]> = {
  aspect: [.55, 1.15], scale: [.85, 1.12], lidTop: [0, 1], slant: [-.6, .5], sag: [0, .3], lidBot: [0, .6], bow: [0, .5],
  shut: [0, 1], cup: [0, 1], pw: [.06, .42], ph: [.06, .42], inset: [0, .2], pdy: [-.2, .2], heart: [0, 1], star: [0, 1],
  gazeX: [-.35, .35], gazeY: [-.25, .25], pop: [0, 1], transitionMs: [100, 600], noBlinkMs: [0, 10000, true], blinkRate: [.1, 5],
  R: [40, 56, true], gap: [0, .3], arcW: [.12, .2], spin: [0, 1.2],
  "brow.offsetY": [-100, 100, true], "brow.length": [0, 100, true], "brow.thickness": [0, 30, true],
  "brow.angleL": [-90, 90, true], "brow.angleR": [-90, 90, true],
  "mouth.width": [0, 100, true], "mouth.offsetY": [-100, 160, true], "mouth.curve": [-50, 50, true],
};
const EPS = 1e-9;

export const FACE_NAME = /^[A-Z][A-Z0-9_]{0,23}$/;
export const faceFileName = (index: number, name: string) => `${String(index).padStart(2, "0")}-${name.toLowerCase().replace(/_/g, "-")}.json`;

type Obj = Record<string, unknown>;
const isObj = (v: unknown): v is Obj => typeof v === "object" && v !== null && !Array.isArray(v);

function exactKeys(o: Obj, keys: readonly string[], where: string) {
  const extra = Object.keys(o).filter((k) => !keys.includes(k));
  if (extra.length) throw new Error(`${where}: unknown key ${extra[0]}`);
  const missing = keys.filter((k) => !(k in o));
  if (missing.length) throw new Error(`${where}: missing ${missing[0]}`);
}
function num(v: unknown, key: string, where: string): number {
  if (typeof v !== "number" || !Number.isFinite(v)) throw new Error(`${where}.${key}: expected a number`);
  const r = RANGE[key];
  if (r) {
    if (v < r[0] - EPS || v > r[1] + EPS) throw new Error(`${where}.${key}: ${v} is outside ${r[0]}..${r[1]}`);
    if (r[2] && !Number.isInteger(v)) throw new Error(`${where}.${key}: expected a whole number`);
  }
  return v;
}
function oneOf<T extends string>(v: unknown, list: readonly T[], key: string, where: string): T {
  if (typeof v !== "string" || !(list as readonly string[]).includes(v)) throw new Error(`${where}.${key}: expected one of ${list.map((s) => JSON.stringify(s)).join(", ")}`);
  return v as T;
}
function bool(v: unknown, key: string, where: string): boolean {
  if (typeof v !== "boolean") throw new Error(`${where}.${key}: expected true or false`);
  return v;
}

/** Checks a face body and returns a copy in disk key order. Throws an Error naming the first problem. */
export function validateFace(body: unknown): Face {
  if (!isObj(body)) throw new Error("face: expected an object");
  exactKeys(body, FACE_KEYS, "face");
  const index = body.index;
  if (typeof index !== "number" || !Number.isInteger(index) || index < 0 || index > 99) throw new Error("face.index: expected 0..99");
  if (typeof body.name !== "string" || !FACE_NAME.test(body.name)) throw new Error("face.name: expected an upper-case name");
  const kevinTuned = bool(body.kevinTuned, "kevinTuned", "face");

  const e = body.eye;
  if (!isObj(e)) throw new Error("face.eye: expected an object");
  exactKeys(e, EYE_ORDER, "eye");
  const eye = {} as EyeParams;
  for (const k of EYE_KEYS) {
    const v = e[k];
    if (!Array.isArray(v) || v.length !== 2) throw new Error(`eye.${k}: expected [left, right]`);
    eye[k] = [num(v[0], k, "eye"), num(v[1], k, "eye")];
  }
  for (let j = 0; j < 2; j++) if (eye.heart[j] + eye.star[j] > 1 + 1e-6) throw new Error("eye: heart + star must be 1 or less");
  const preset = e.preset;
  if (typeof preset !== "string" || (preset !== "" && !(preset in PRESETS))) throw new Error(`eye.preset: expected "" or one of ${Object.keys(PRESETS).join(", ")}`);
  eye.preset = preset;
  eye.gazeX = num(e.gazeX, "gazeX", "eye");
  eye.gazeY = num(e.gazeY, "gazeY", "eye");
  eye.pop = num(e.pop, "pop", "eye");
  eye.transitionMs = num(e.transitionMs, "transitionMs", "eye");
  eye.overlay = oneOf(e.overlay, OVERLAYS, "overlay", "eye");
  eye.gaze = oneOf(e.gaze, GAZES, "gaze", "eye");
  eye.pulse = bool(e.pulse, "pulse", "eye");
  eye.exitBlink = bool(e.exitBlink, "exitBlink", "eye");
  eye.entry = oneOf(e.entry, ENTRIES, "entry", "eye");
  eye.noBlinkMs = num(e.noBlinkMs, "noBlinkMs", "eye");
  eye.blinkRate = num(e.blinkRate, "blinkRate", "eye");

  const b = body.brow;
  if (!isObj(b)) throw new Error("face.brow: expected an object");
  exactKeys(b, BROW_KEYS, "brow");
  const brow: Brow = {
    offsetY: num(b.offsetY, "brow.offsetY", "brow"), length: num(b.length, "brow.length", "brow"), thickness: num(b.thickness, "brow.thickness", "brow"),
    angleL: num(b.angleL, "brow.angleL", "brow"), angleR: num(b.angleR, "brow.angleR", "brow"), visible: bool(b.visible, "visible", "brow"),
  };
  const m = body.mouth;
  if (!isObj(m)) throw new Error("face.mouth: expected an object");
  exactKeys(m, MOUTH_KEYS, "mouth");
  const mouth: Mouth = {
    type: oneOf(m.type, MOUTH_TYPES, "type", "mouth"), width: num(m.width, "mouth.width", "mouth"),
    offsetY: num(m.offsetY, "mouth.offsetY", "mouth"), curve: num(m.curve, "mouth.curve", "mouth"),
  };
  const s = body.scorecard;
  if (!Array.isArray(s) || s.length !== 6 || !s.every((t) => t === 0 || t === 1)) throw new Error("face.scorecard: expected 6 ticks of 0 or 1");
  return { index, name: body.name, kevinTuned, eye, brow, mouth, scorecard: s.slice() as Ticks };
}

export function validateGlobals(body: unknown): Globals {
  if (!isObj(body)) throw new Error("globals: expected an object");
  exactKeys(body, GLOBAL_KEYS, "globals");
  return { R: num(body.R, "R", "globals"), gap: num(body.gap, "gap", "globals"), arcW: num(body.arcW, "arcW", "globals"), spin: num(body.spin, "spin", "globals") };
}

// Slider values are typed decimals; arithmetic on them (1 - 0.77) can leave float noise in the
// last digits. Six decimals keeps every value the lab can produce and keeps diffs readable.
const tidy = (v: number) => (Number.isInteger(v) ? v : +v.toFixed(6));

function writeValue(v: unknown, indent: string): string {
  if (Array.isArray(v)) return "[" + v.map((x) => writeValue(x, indent)).join(", ") + "]";
  if (isObj(v)) {
    const inner = indent + "  ";
    const rows = Object.entries(v).map(([k, x]) => `${inner}${JSON.stringify(k)}: ${writeValue(x, inner)}`);
    return rows.length ? "{\n" + rows.join(",\n") + "\n" + indent + "}" : "{}";
  }
  if (typeof v === "number") return JSON.stringify(tidy(v));
  return JSON.stringify(v);
}

/** 2-space JSON with flat arrays on one line and a trailing newline. Input must already be in key order. */
export const stableJson = (v: unknown) => writeValue(v, "") + "\n";

/** A face in disk key order, validated. */
export const faceToFile = (body: unknown) => stableJson(validateFace(body));
export const globalsToFile = (body: unknown) => stableJson(validateGlobals(body));

/** Deep equality with a float tolerance, for "edited since the last commit". */
export function sameVal(a: unknown, b: unknown): boolean {
  if (Array.isArray(a)) return Array.isArray(b) && a.length === b.length && a.every((v, n) => sameVal(v, b[n]));
  if (typeof a === "number") return typeof b === "number" && Math.abs(a - b) < 1e-9;
  if (isObj(a)) {
    if (!isObj(b)) return false;
    const ka = Object.keys(a), kb = Object.keys(b);
    return ka.length === kb.length && ka.every((k) => k in b && sameVal(a[k], b[k]));
  }
  return a === b;
}

export const clone = <T>(o: T): T => JSON.parse(JSON.stringify(o));
