// The lab's simulation state in one object, so the model code can live in plain modules
// with no DOM and no globals: the artifact kept these as closure variables, here every
// function takes the Lab as its first argument. Also the device table the canvases use.

import type { EyeParams, Globals, PairKey } from "./faces";
import { EYE_KEYS } from "./faces";
import type { Expr } from "./before";
import { BlinkState, FaceState, IMUTracker, LookAround, Tweens } from "./before";
import { Spring, F_POP } from "./model";

export interface Device { label: string; w: number; h: number; cx: number; cy: number; ppi: number; k: number; chip?: boolean; mono?: boolean }
export const DEVICES: Record<string, Device> = {
  lcd169: { label: '1.69" 240×280', w: 240, h: 280, cx: 120, cy: 118, ppi: 218, k: 1 },
  lcd13: { label: '1.3" 240×240', w: 240, h: 240, cx: 120, cy: 118, ppi: 261, k: 1 },
  cores3: { label: "CoreS3 320×240", w: 320, h: 240, cx: 160, cy: 105, ppi: 200, k: 1 },
  chip: { label: "×0.25 chip scale 60×70", w: 60, h: 70, cx: 30, cy: 30, ppi: 218, k: .25, chip: true },
  mono: { label: "vizMini OLED 128×64 1-bit", w: 128, h: 64, cx: 64, cy: 30, ppi: 149, k: .45, mono: true },
};

export interface Ui {
  device: string; scale: string; fps: number; speed: number; fc: number; idle: boolean; brows: boolean; mouth: boolean; pupils: boolean;
  mono: boolean; ambient: boolean; sleep: boolean; legacy: boolean; catchlight: boolean; pendant: boolean;
}
export interface Pad { x: number; y: number; drag: boolean }

export type Why = "auto" | "double" | "swap" | "exit" | "saccade" | "fidget";
export interface Fidget { name: string; t0: number; dur: number; p: number; forced: boolean }
export interface IdleLook { kind: "mood" | "pupil"; mood?: "curious" | "bored"; preset?: string; until: number }
export interface GlitchBand { y0: number; h: number; dx: number }
export interface AfterState {
  eyes: [Record<PairKey, Spring>, Record<PairKey, Spring>];
  brow: [{ ang: Spring; len: Spring; thick: Spring }, { ang: Spring; len: Spring; thick: Spring }];
  gx: Spring; gy: Spring; popS: Spring; popA: Spring; bStr: Spring;
  expr: number; enteredAt: number; entryT: number; overlay: string; brows: boolean; pending: { overlay: string; brows: boolean } | null;
  blink: { ph: number; t: number; b: number; next: number; last: number; queue: { at: number; why: Why }[] };
  gaze: { tx: number; ty: number; mode: "sacc" | "pursuit"; next: number; side: number; up: boolean; micro: boolean };
  idle: { nextLook: number; look: IdleLook | null; nextFid: number; fid: Fidget | null; lastFid: string };
  glitch: [{ next: number; bands: GlitchBand[] }, { next: number; bands: GlitchBand[] }];
  spinA: number; spiralA: number; smallUntil: number; gxOut: number; gyOut: number;
}
export interface BeforeState { face: FaceState; blink: BlinkState; look: LookAround; imu: IMUTracker; wasSleep: boolean }
export interface Stats { gaze: number[]; blinks: number[]; fidgets: number[]; since: number }
export interface Mon {
  maxStep: number; worst: string; minGap: number; pupilOut: number; swapsReq: number; swapsBlink: number; exitBlinks: number; label: string;
  skipNext: boolean; prevArea: number[]; prevBlink: boolean; prevShut: number[]; prevShutMove: boolean[];
}
export interface Buffers { w: number; h: number; M: [Uint8Array, Uint8Array]; PM: Uint8Array; CL: Uint8Array; MU: [Uint8Array | null, Uint8Array | null]; GB: [number[][], number[][]] }
export interface Profile { t: Int16Array; b: Int16Array }

export interface Lab {
  EXPR: Expr[];
  /** After-model params per face; the array is mutated in place, never reassigned. */
  AP: EyeParams[];
  GP: Globals;
  ui: Ui; pad: Pad; AS: AfterState; BF: BeforeState; tweens: Tweens;
  stats: Stats; mon: Mon; lastInteract: number;
  buf: Buffers; silCache: Uint8Array[] | null; profCache: Profile[] | null;
}

export const dev = (L: Lab) => (L.ui.mono ? DEVICES.mono : DEVICES[L.ui.device]);
export const padEngaged = (L: Lab) => L.pad.drag || Math.abs(L.pad.x) > 0.02 || Math.abs(L.pad.y) > 0.02;

function eyeSprings() {
  const o = {} as Record<PairKey, Spring>;
  for (const k of EYE_KEYS) o[k] = new Spring(0, 2, .85);
  return o;
}
const browSprings = () => ({ ang: new Spring(0, 2, .85), len: new Spring(0, 2, .85), thick: new Spring(0, 2, .85) });

export function createState(EXPR: Expr[], AP: EyeParams[], GP: Globals): Lab {
  const tweens = new Tweens();
  const empty = new Uint8Array(0);
  return {
    EXPR, AP, GP, tweens,
    ui: {
      device: "lcd169", scale: "auto", fps: 20, speed: 1, fc: 0xFFFF, idle: true, brows: true, mouth: true, pupils: true,
      mono: false, ambient: false, sleep: false, legacy: false, catchlight: false, pendant: false,
    },
    pad: { x: 0, y: 0, drag: false },
    AS: {
      eyes: [eyeSprings(), eyeSprings()], brow: [browSprings(), browSprings()],
      gx: new Spring(0, 6, 1), gy: new Spring(0, 6, 1),
      popS: new Spring(0, F_POP, 1), popA: new Spring(0, 2.2, .45), bStr: new Spring(0, F_POP, 1),
      expr: 0, enteredAt: 0, entryT: 1e9, overlay: "none", brows: true, pending: null,
      blink: { ph: 0, t: 0, b: 0, next: 0, last: -1e9, queue: [] },
      gaze: { tx: 0, ty: 0, mode: "sacc", next: 0, side: 1, up: false, micro: false },
      idle: { nextLook: 0, look: null, nextFid: 0, fid: null, lastFid: "" },
      glitch: [{ next: 0, bands: [] }, { next: 0, bands: [] }],
      spinA: 0, spiralA: 0, smallUntil: 0, gxOut: 0, gyOut: 0,
    },
    BF: { face: new FaceState(EXPR, tweens), blink: new BlinkState(), look: new LookAround(tweens), imu: new IMUTracker(), wasSleep: false },
    stats: { gaze: [], blinks: [], fidgets: [], since: 0 },
    mon: { maxStep: 0, worst: "", minGap: 1e9, pupilOut: 0, swapsReq: 0, swapsBlink: 0, exitBlinks: 0, label: "", skipNext: true, prevArea: [0, 0], prevBlink: false, prevShut: [0, 0], prevShutMove: [false, false] },
    lastInteract: 0,
    buf: { w: 0, h: 0, M: [empty, empty], PM: empty, CL: empty, MU: [null, null], GB: [[], []] },
    silCache: null, profCache: null,
  };
}
