# vizBot eye lab ("eyelab") — plan

## Where we are (2026-10-25)

Branch `feat/eyelab-app`, stacked on `feat/eye-model-r4` (PR #115: the firmware eye model,
v3.7.39). The eye lab began as a claude.ai artifact
(https://claude.ai/artifact/EDGxtjiEETp2j2d3x1Y66q); this app replaces it as part of the vizpow
toolset.

- Phase 1 (standalone lab) is committed.
- Phase 2 (deploy pipeline) is built and tested short of a real flash: dry run passes (389 frames,
  388 identical, worst 2 px), a deliberately wrong firmware value fails parity, and a full lcd-169
  run (bump, build, commit) worked in a throwaway worktree. **Next: Kevin runs the first real
  flash** from the Deploy section (one 1.69 first, then the Stackchan with its power-cycle pause).

## Context
The eyes were redesigned in four review rounds (docs/eye-art-direction.md, docs/eye-art-review-r2.md,
docs/eye-art-review-r4.md) and ported to the firmware (vizbot/bot_eye_model.h). Kevin wants one local
tool to open, edit eyes, make new faces and push them to his bots through a pipeline, with Claude on
hand like kpow-dash. Same methodology as kpow-dash: scripts do the work, Claude runs only when asked.

## Core rules
- **Faces are files.** One JSON file per face in `eyelab/faces/`, in git. Saving a slider writes the
  file; git is the history and the undo. No database for face data.
- **The lab model is the reference.** The firmware (`bot_eye_model.h`) must match the lab pixel for
  pixel; the pipeline checks that on every deploy.
- **Face numbers never change.** vizbrain, `/bot/expression` and the touch UI address faces by index.
  New faces append at 25 and up.
- **Your bots only.** The pipeline flashes Kevin's bots without asking; friends' devices never.

## Decisions made
- Lives in the vizpow repo at `eyelab/`, next to `tools/` (it builds vizpow firmware).
- Started on demand with `npm run dev` (no launchd).
- Same stack as kpow-dash: Hono on `@hono/node-server`, Vite + React, TypeScript, `tsc --noEmit`
  as the gate. Server `127.0.0.1:4060`, Vite dev `:4061` with `/api` proxied (4050 is vizbrain's).
- The artifact lab stays until this app replaces it.

## Architecture (one package)
- `server/` — Hono. Reads and writes `faces/*.json` and runs the deploy pipeline; later the Claude panel.
  Origin allowlist on POSTs like kpow-dash.
- `client/` — Vite + React. The lab UI: before/after device canvases, face list, tuning panel,
  look pad, scorecard.
- `shared/` — the eye model and the old-renderer port, as plain TypeScript with no DOM: frame
  buffer, raster, springs, motion. Used by the client and by the pipeline's parity check.
- `parity/` — the firmware model host-compiled against the lab (`test.cpp`, `lab.ts`, `parity.ts`).
- `config/bots.json` — the allowlist of Kevin's bots.
- `faces/` — `NN-name.json` per face (eye params + brows/mouth), `globals.json`.

## Phases

### Phase 1 — standalone lab (this branch)
- Port the artifact lab's JS into `shared/` (model, old renderer, RGB565 frame) and `client/` (UI).
- Seed `faces/` from `tools/eye_params_r4.json` (round 4 + Kevin's seven tweaks) and the
  `BotExpression` brow/mouth rows in `vizbot/bot_faces.h`.
- Sliders save to the face file (debounced); a dirty/saved status; reset = `git checkout` of that file.
- `tools/gen_eye_params.py` reads `faces/` instead of the frozen snapshot.
- Done when: `npm run dev` shows the same faces and motion as the artifact, edits land in
  `faces/*.json`, and the regenerated `bot_eye_params.h` is byte-identical before any edit.

### Phase 2 — deploy pipeline (built; first real flash pending)
One Deploy button, each step streamed with logs, stop on first failure: generate
`bot_eye_params.h` → host-compile the firmware model and diff it against the lab → bump
`FIRMWARE_VERSION` → `~/.platformio/penv/bin/pio` build → commit → OTA-flash chosen bots (found by
a `/state` scan; Stackchan gets the cold-boot reminder) → read `faceRender` back. A GitHub release
stays a separate step Kevin approves. Details in README.md, "Deploy".
- `server/deploy.ts` runs it (one run at a time, events kept for the stream, a log file per run);
  `server/bots.ts` scans and uploads; `parity/` is the firmware-vs-lab check (8 px per frame budget).
- Allowlist: `config/bots.json` by `deviceName`. Unlisted bots show but cannot be chosen, and the
  server refuses them. The `faces` board refuses OTA (shared flash with kFun), so it is USB only.
- Dry run = generate + parity, header restored. A run that stops before the commit restores
  `config.h`. The commit holds only the header, `config.h`, `eyelab/faces/` and the generator.
- Done when: a real deploy flashes a 1.69 and the Stackchan and reads `faceRender` back.

### Phase 3 — Claude panel
Same two modes as kpow-dash. **Ask**: read-only tools (read faces, render a face to PNG, run the
scorecard). **Change**: Agent SDK in a git worktree on an `eyelab/<slug>` branch, tools limited to
the worktree, `tsc --noEmit` as the only Bash, preview / keep / undo. Key from Keychain like the dash.

### Phase 4 — new faces
Adding face 25+: brow and mouth editors in the lab, and the pipeline generates the firmware's
`BotExpression` row, `EXPR_*` define, `BOT_NUM_EXPRESSIONS`, the mood chip pages, and vizbrain's face
list. First check how vizbrain names faces.
