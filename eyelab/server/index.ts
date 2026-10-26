// The eye lab's local server: a small JSON API over the face files in eyelab/faces/, so
// a slider in the browser lands in a file git can diff, plus the deploy pipeline that takes
// those faces to Kevin's bots. Loopback only. Phase 3 adds the Claude panel here.

import { serve } from "@hono/node-server";
import { serveStatic } from "@hono/node-server/serve-static";
import { Hono } from "hono";
import type { Context } from "hono";
import { streamSSE } from "hono/streaming";
import { relative } from "node:path";
import { scanBots } from "./bots";
import { ackDeploy, cancelDeploy, currentRun, startDeploy } from "./deploy";
import { HttpError, ROOT, faceBaseline, globalsBaseline, readFaces, resetFace, resetGlobals, writeFace, writeGlobals } from "./faces";

// 4060, not 4050: vizbrain owns *:4050 on this Mac, and a 127.0.0.1 bind would quietly shadow it.
const PORT = 4060;
const app = new Hono();

// Only the lab's own pages may write. A web page open elsewhere in the browser could
// otherwise PUT to 127.0.0.1:4060 (browsers send Origin on those).
const LAB_ORIGINS = new Set(["http://localhost:4060", "http://localhost:4061", "http://127.0.0.1:4060", "http://127.0.0.1:4061"]);
app.use("/api/*", async (c, next) => {
  const origin = c.req.header("origin");
  if (c.req.method !== "GET" && origin && !LAB_ORIGINS.has(origin)) return c.json({ ok: false, message: "forbidden origin" }, 403);
  await next();
});

/** Runs a handler and turns HttpError into a JSON error the client shows as-is. */
async function answer(c: Context, fn: () => Promise<unknown>) {
  try {
    return c.json(await fn());
  } catch (err) {
    if (err instanceof HttpError) return c.json({ ok: false, message: err.message }, err.status);
    console.error("[eyelab]", err);
    return c.json({ ok: false, message: err instanceof Error ? err.message : String(err) }, 500);
  }
}
function indexParam(c: Context) {
  const raw = c.req.param("index") ?? "";
  if (!/^\d{1,2}$/.test(raw)) throw new HttpError(400, "face index must be 0..99");
  return Number(raw);
}
async function body(c: Context) {
  try { return await c.req.json(); } catch { throw new HttpError(400, "body must be JSON"); }
}

app.get("/api/faces", (c) => answer(c, readFaces));
app.put("/api/faces/:index", (c) => answer(c, async () => ({ ok: true, ...(await writeFace(indexParam(c), await body(c))) })));
// The face as of the last commit; "edited" in the lab means "differs from this". null when not committed yet.
app.get("/api/faces/:index/baseline", (c) => answer(c, async () => ({ face: await faceBaseline(indexParam(c)) })));
app.post("/api/faces/:index/reset", (c) => answer(c, async () => ({ ok: true, face: await resetFace(indexParam(c)) })));
app.put("/api/globals", (c) => answer(c, async () => ({ ok: true, ...(await writeGlobals(await body(c))) })));
app.get("/api/globals/baseline", (c) => answer(c, async () => ({ globals: await globalsBaseline() })));
app.post("/api/globals/reset", (c) => answer(c, async () => ({ ok: true, globals: await resetGlobals() })));

// Deploy pipeline (server/deploy.ts). POSTs go through the Origin allowlist above.
app.get("/api/bots", (c) => answer(c, async () => ({ bots: await scanBots() })));
app.post("/api/deploy", (c) => answer(c, async () => ({ ok: true, ...(await startDeploy(await body(c))) })));
app.post("/api/deploy/cancel", (c) => c.json({ ok: cancelDeploy() }));
app.post("/api/deploy/ack", (c) => c.json({ ok: ackDeploy() }));
// The current or last run: every event so far, then live ones until it finishes. A page that
// reconnects mid-run gets the whole run replayed and carries on from there.
app.get("/api/deploy/stream", (c) => streamSSE(c, async (sse) => {
  const run = currentRun();
  if (!run) { await sse.writeSSE({ data: JSON.stringify({ type: "idle" }) }); return; }
  let i = 0, gone = false;
  sse.onAbort(() => { gone = true; run.wake(false); });
  while (!gone) {
    while (i < run.events.length && !gone) await sse.writeSSE({ data: JSON.stringify(run.events[i++]) });
    if (run.finished && i >= run.events.length) break;
    if (!(await run.next(15_000)) && !gone) await sse.writeSSE({ data: JSON.stringify({ type: "ping" }) });
  }
}));

if (process.env.NODE_ENV === "production") {
  const dist = relative(process.cwd(), `${ROOT}dist`);
  app.use("/*", serveStatic({ root: dist }));
  app.get("*", serveStatic({ path: `${dist}/index.html` }));
}

// Loopback only: nothing on the network can reach the face files.
serve({ fetch: app.fetch, port: PORT, hostname: "127.0.0.1" }, () =>
  console.log(`eyelab server on http://localhost:${PORT}`),
);
