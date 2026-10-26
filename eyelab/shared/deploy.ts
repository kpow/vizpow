// The deploy pipeline's wire types, shared by the server (server/deploy.ts) and the lab's
// Deploy section: the bots the LAN scan found, and the events GET /api/deploy/stream sends.
// Types only, so the client bundle pulls in nothing from Node.

export type StepId = "generate" | "parity" | "version" | "build" | "commit" | "flash" | "render";
export type StepStatus = "pending" | "running" | "ok" | "warn" | "fail" | "skip" | "waiting";
export type StepEnd = "ok" | "warn" | "fail" | "skip";

/** One bot from GET /api/bots. `selectable` = on the allowlist and its board can take an OTA build. */
export interface BotInfo {
  name: string;
  ip: string;
  board: string;
  firmware: string;
  allowed: boolean;
  env: string | null;
  selectable: boolean;
  /** Why it cannot be chosen, when it cannot. */
  reason?: string;
}

export interface FaceRender { avgUs: number; maxUs: number; renders: number; skipped: number }
export interface RenderResult { bot: string; ip: string; firmware: string; faceRender: FaceRender | null; status: StepEnd; note: string }

export interface DeployRequest {
  bots: string[];
  dryRun?: boolean;
  /** Extra envs to build without flashing (API only), e.g. ["lcd-169"]. */
  envs?: string[];
}

export type DeployEvent =
  | { type: "run"; id: string; startedAt: string; dryRun: boolean; bots: string[]; envs: string[]; steps: { id: StepId; title: string }[]; logFile: string }
  | { type: "step-start"; step: StepId }
  | { type: "log"; step: StepId; line: string }
  | { type: "step-end"; step: StepId; status: StepEnd; summary: string; ms: number }
  | { type: "waiting"; step: StepId; bot: string; message: string }
  | { type: "resumed"; step: StepId }
  | { type: "done"; ok: boolean; cancelled: boolean; version: string | null; commit: string | null; renders: RenderResult[]; summary: string }
  | { type: "error"; message: string }
  /** Stream only: no run since the server started. */
  | { type: "idle" }
  /** Stream only: keeps the connection open while a step waits. */
  | { type: "ping" };
