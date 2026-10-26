// Kevin's bots on the LAN: finds them by reading the ARP table and probing each address's
// GET /state, checks them against config/bots.json (his bots only, by deviceName), and does
// the HTTP side of an OTA flash. IPs live only in memory: they come from the scan, never
// from config, and nothing here writes them to disk.

import { execFile } from "node:child_process";
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";
import type { BotInfo, FaceRender } from "../shared/deploy";
import { ROOT } from "./faces";

const run = promisify(execFile);
const ALLOWLIST = join(ROOT, "config", "bots.json");

/** BOARD_TYPE (config.h, /state boardType) to PlatformIO env (vizbot/platformio.ini). */
export const BOARD_ENV: Record<string, string> = {
  "esp32s3-lcd169": "lcd-169",
  "esp32s3-lcd13": "lcd-13",
  m5cores3: "m5cores3",
  stackchan: "stackchan",
};
/** Boards that exist but cannot take a wifi update, and why. */
export const NO_OTA: Record<string, string> = {
  faces: "the faces board refuses OTA: vizBot shares its flash with the kFun game console, so a wifi update would overwrite the games (vizbot/ota_update.h). Update it over USB.",
};
export const envBoard = (env: string) => Object.keys(BOARD_ENV).find((b) => BOARD_ENV[b] === env) ?? null;

export interface BotState { deviceName: string; boardType: string; firmwareVersion: string; faceRender?: FaceRender }

/** The allowlist, read fresh each time so an edit applies without a restart. */
export async function allowlist(): Promise<Set<string>> {
  const raw = JSON.parse(await readFile(ALLOWLIST, "utf8")) as { bots?: unknown };
  if (!Array.isArray(raw.bots) || !raw.bots.every((b) => typeof b === "string")) throw new Error("config/bots.json: bots must be a list of deviceName strings");
  return new Set(raw.bots as string[]);
}

/** GET http://<ip>/state. Throws on a timeout, a non-200, or JSON without deviceName and boardType. */
export async function getState(ip: string, timeoutMs: number, signal?: AbortSignal): Promise<BotState> {
  const sig = signal ? AbortSignal.any([signal, AbortSignal.timeout(timeoutMs)]) : AbortSignal.timeout(timeoutMs);
  const res = await fetch(`http://${ip}/state`, { signal: sig });
  if (!res.ok) throw new Error(`/state answered HTTP ${res.status}`);
  const s = (await res.json()) as Partial<BotState>;
  if (typeof s.deviceName !== "string" || typeof s.boardType !== "string") throw new Error("/state is not a vizBot's");
  return { deviceName: s.deviceName, boardType: s.boardType, firmwareVersion: String(s.firmwareVersion ?? "?"), faceRender: s.faceRender };
}

/** IPv4 neighbours from `arp -a -n`, minus incomplete, broadcast and multicast entries. */
async function arpAddresses(): Promise<string[]> {
  const { stdout } = await run("arp", ["-a", "-n"], { timeout: 5000 });
  const out = new Set<string>();
  for (const line of stdout.split("\n")) {
    const m = /\((\d+\.\d+\.\d+\.\d+)\) at (\S+)/.exec(line);
    if (!m || m[2] === "(incomplete)" || /^ff:ff:ff:ff:ff:ff$/i.test(m[2])) continue;
    const first = Number(m[1].split(".")[0]);
    if (m[1].endsWith(".255") || first >= 224) continue;
    out.add(m[1]);
  }
  return [...out];
}

function describe(ip: string, s: BotState, allowed: Set<string>): BotInfo {
  const ok = allowed.has(s.deviceName);
  const env = BOARD_ENV[s.boardType] ?? null;
  const reason = !ok ? "not on the allowlist (config/bots.json): not one of Kevin's bots"
    : NO_OTA[s.boardType] ?? (env ? undefined : `no OTA build for board ${s.boardType}`);
  return { name: s.deviceName, ip, board: s.boardType, firmware: s.firmwareVersion, allowed: ok, env, selectable: ok && !!env && !NO_OTA[s.boardType], reason };
}

/** Every vizBot that answers /state within 1.5 s, by name. */
export async function scanBots(): Promise<BotInfo[]> {
  const [ips, allowed] = await Promise.all([arpAddresses(), allowlist()]);
  const found = await Promise.all(ips.map(async (ip) => {
    try { return describe(ip, await getState(ip, 1500), allowed); } catch { return null; }
  }));
  return found.filter((b): b is BotInfo => !!b).sort((a, b) => Number(b.selectable) - Number(a.selectable) || a.name.localeCompare(b.name));
}

/**
 * POST the app image to http://<ip>/update as multipart field `firmware`. The firmware checks
 * the filename contains its BOARD_TYPE, so pass the builds/ name. Returns the response body,
 * "" when the bot answered nothing (it reboots right after a good upload).
 */
export async function uploadFirmware(ip: string, bin: Buffer, fileName: string, signal: AbortSignal): Promise<string> {
  const form = new FormData();
  form.append("firmware", new Blob([new Uint8Array(bin)], { type: "application/octet-stream" }), fileName);
  const res = await fetch(`http://${ip}/update`, { method: "POST", body: form, signal: AbortSignal.any([signal, AbortSignal.timeout(180_000)]) });
  return (await res.text()).trim();
}
