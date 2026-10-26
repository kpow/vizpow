// Runs the pipeline's tools (python3, c++, tsx, pio, git) as child processes: argument
// arrays only, never a shell string, output split into lines for the live log, and an
// AbortSignal that kills the whole process group, so Cancel also stops pio's compilers.

import { spawn } from "node:child_process";
import { basename } from "node:path";

/** Thrown when the run was cancelled while a process ran. */
export class Cancelled extends Error {
  constructor() { super("cancelled"); }
}

export interface ProcOpts {
  cwd: string;
  env?: NodeJS.ProcessEnv;
  /** Each non-empty line of output (stdout unless captured, and stderr). */
  onLine?: (line: string) => void;
  /** Collect stdout and resolve with it instead of logging it. */
  capture?: boolean;
  signal?: AbortSignal;
}

/** Resolves with captured stdout (or ""); rejects with the exit code (and the last lines, when nothing was logged). */
export function runProc(cmd: string, args: string[], o: ProcOpts): Promise<string> {
  return new Promise((resolve, reject) => {
    if (o.signal?.aborted) return reject(new Cancelled());
    const child = spawn(cmd, args, { cwd: o.cwd, env: o.env ?? process.env, detached: true, stdio: ["ignore", "pipe", "pipe"] });
    let captured = "";
    const tail: string[] = [];
    const emit = (line: string) => {
      if (!line.trim()) return;
      tail.push(line); if (tail.length > 8) tail.shift();
      o.onLine?.(line);
    };
    const lines = (stream: NodeJS.ReadableStream, capture: boolean) => {
      let buf = "";
      stream.setEncoding("utf8");
      stream.on("data", (d: string) => {
        if (capture) { captured += d; return; }
        buf += d;
        const parts = buf.split(/\r\n|\r|\n/);
        buf = parts.pop() ?? "";
        parts.forEach(emit);
      });
      stream.on("end", () => { if (buf) emit(buf); });
    };
    lines(child.stdout!, !!o.capture);
    lines(child.stderr!, false);

    // detached gives the child its own process group; -pid signals all of it.
    const kill = () => {
      const pid = child.pid;
      if (!pid) return;
      try { process.kill(-pid, "SIGTERM"); } catch { /* already gone */ }
      setTimeout(() => { try { process.kill(-pid, "SIGKILL"); } catch { /* already gone */ } }, 3000).unref();
    };
    o.signal?.addEventListener("abort", kill, { once: true });
    child.on("error", (err) => { o.signal?.removeEventListener("abort", kill); reject(new Error(`${basename(cmd)}: ${err.message}`)); });
    child.on("close", (code, sig) => {
      o.signal?.removeEventListener("abort", kill);
      if (o.signal?.aborted) return reject(new Cancelled());
      if (code === 0) return resolve(captured);
      const last = !o.onLine && tail.length ? `\n${tail.join("\n")}` : ""; // with onLine the log already has it
      reject(new Error(`${basename(cmd)} ${args[0] ?? ""} exited with ${code ?? sig}${last}`));
    });
  });
}
