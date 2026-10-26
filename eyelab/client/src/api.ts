// Fetch helpers for the eyelab server's API (server/index.ts): face files and the deploy
// pipeline. Errors come back as ApiError with the server's own message, or status 0 when the
// server did not answer.

import type { BotInfo, DeployRequest } from "../../shared/deploy";
import type { Face, Globals } from "../../shared/faces";

export class ApiError extends Error {
  constructor(public status: number, message: string) { super(message); }
}

async function call<T>(method: string, url: string, body?: unknown): Promise<T> {
  let res: Response;
  try {
    res = await fetch(url, { method, headers: body === undefined ? undefined : { "content-type": "application/json" }, body: body === undefined ? undefined : JSON.stringify(body) });
  } catch {
    throw new ApiError(0, "the eyelab server did not answer");
  }
  const data = await res.json().catch(() => null);
  if (!res.ok) throw new ApiError(res.status, (data && typeof data.message === "string" && data.message) || `HTTP ${res.status}`);
  return data as T;
}

export const getFaces = () => call<{ faces: Face[]; globals: Globals }>("GET", "/api/faces");
export const putFace = (face: Face) => call<{ ok: true; written: boolean }>("PUT", `/api/faces/${face.index}`, face);
export const putGlobals = (g: Globals) => call<{ ok: true; written: boolean }>("PUT", "/api/globals", g);
export const getFaceBaseline = (i: number) => call<{ face: Face | null }>("GET", `/api/faces/${i}/baseline`);
export const getGlobalsBaseline = () => call<{ globals: Globals | null }>("GET", "/api/globals/baseline");
export const resetFace = (i: number) => call<{ ok: true; face: Face }>("POST", `/api/faces/${i}/reset`);
export const resetGlobals = () => call<{ ok: true; globals: Globals }>("POST", "/api/globals/reset");

export const getBots = () => call<{ bots: BotInfo[] }>("GET", "/api/bots");
export const startDeploy = (req: DeployRequest) => call<{ ok: true; id: string; logFile: string }>("POST", "/api/deploy", req);
export const cancelDeploy = () => call<{ ok: boolean }>("POST", "/api/deploy/cancel");
export const ackDeploy = () => call<{ ok: boolean }>("POST", "/api/deploy/ack");
