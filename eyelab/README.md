# eyelab

The vizBot eye lab as a local app: before/after device canvases, the 25 faces, the
tuning panel, the scorecard, and a Deploy button that takes the faces to the bots. It
replaces the claude.ai artifact lab. See `PLAN.md`.

## Run it

```sh
cd eyelab
npm install        # once
npm run dev        # server on 127.0.0.1:4060, lab UI on http://localhost:4061
```

Ports are 4060/4061 because vizbrain owns 4050. `npm run typecheck` is the gate.
`npm run build` then `npm start` serves the built UI from the server on :4060.

## Where faces live

- `faces/NN-name.json`: one file per face, by firmware index. Each holds `eye` (the after
  model's parameters; per-eye values are `[left, right]`), `brow` and `mouth` (the
  `BotExpression` row in `vizbot/bot_faces.h`, read-only for now), and `scorecard`
  (the six criterion ticks).
- `faces/globals.json`: R, gap, arcW, spin.
- Face numbers never change. New faces append at 25 and up.
- `scripts/seed-faces.ts` wrote the first set from `tools/eye_params_r4.json`. It is
  kept for the record. Running it again overwrites `faces/`.

## How saving works

- A slider writes its face file about 800 ms after you stop moving it. Clicks (selects,
  ticks) write at once.
- There is one write in flight per file. The server checks every body, writes to a temp
  file and renames it, and keeps key order fixed so `git diff` shows only what changed.
- "edited", the row tint and the "tuned" badge mean "differs from the last commit"
  (`git show HEAD:eyelab/faces/…`). Until `faces/` is committed there is nothing to
  compare with, so nothing is tinted.
- Reset restores a file from HEAD. Reset all (two clicks) does all faces and globals.
- Git is the undo: `git checkout eyelab/faces` throws away everything since the last commit.

## Regenerate the firmware header

```sh
python3 tools/gen_eye_params.py eyelab/faces > vizbot/bot_eye_params.h
git diff vizbot/bot_eye_params.h
```

Built from `eyelab/faces`, the header's `Source:` and `Regenerate:` lines name
`eyelab/faces`. The first deploy commits that two-line comment change; the values
match the round 4 header until a face is edited.

## Deploy

The Deploy section at the bottom of the lab takes the faces to the bots. Tick bots,
press **Deploy**. Each step streams its log and the run stops at the first failure:

1. **Generate**: `tools/gen_eye_params.py eyelab/faces` writes `vizbot/bot_eye_params.h`, then `git diff --stat`.
2. **Parity**: host-compiles `vizbot/bot_eye_model.h` with the new header (`c++ -std=c++17`) and renders
   389 frames (every face at rest, plain and ambient, nine transitions, sleep, pendant, look pad, chip scale,
   glitch bands); `parity/lab.ts` renders the same frames from `shared/`. Any frame more than 8 px apart fails.
3. **Version**: bumps the patch of `FIRMWARE_VERSION` in `vizbot/config.h`.
4. **Build**: `~/.platformio/penv/bin/pio run -d vizbot -e <env>` per board, then checks `builds/vizbot-<board>-v<version>.bin`.
5. **Commit**: `eyelab: deploy v<version>` with only the header, `config.h`, `eyelab/faces/` and the generator.
   The body lists the faces changed since the last deploy commit. Never pushed.
6. **Flash**: checks `/state`, POSTs the bin to `/update` (field `firmware`), retries once on an empty answer,
   then waits up to 90 s for the new version. A Stackchan pauses on "Power-cycle <name> now" until you press Done.
7. **Render check**: reads `faceRender` over 10 s. No renders fails; an average over 25 ms warns.

**Dry run** does steps 1 and 2 and puts the header back. A run that stops before the commit puts
`config.h` back, so a failed build does not use up a version. Cancel kills pio and anything else running.
A GitHub release stays a separate step.

- **Bots** come from a LAN scan (`arp -a`, then `GET /state` on each address). Only names in
  `config/bots.json` can be ticked, and the server refuses to flash any other. The `faces` board shows
  but cannot be ticked: it refuses OTA. IPs stay in memory; nothing is written to disk.
- **Logs**: one per run in `~/Library/Logs/eyelab/deploy-<timestamp>.log`. The page keeps the last run;
  reloading mid-run picks the stream back up.
- **Parity on its own**: `npm run parity`. `npm run parity -- --params <file>` tests another header.
- **API**: `GET /api/bots`; `POST /api/deploy {bots, dryRun?, envs?}` (409 while one runs; `envs` builds
  without flashing, e.g. `{"bots":[],"envs":["lcd-169"]}`); `GET /api/deploy/stream` (SSE);
  `POST /api/deploy/cancel`; `POST /api/deploy/ack`. `EYELAB_NO_FLASH=1` on the server refuses any flash.

## Layout

- `shared/`: the eye model with no DOM. `raster.ts` (RGB565 frame and primitives),
  `before.ts` (the old firmware renderer), `model.ts` and `motion.ts` (the after model),
  `measure.ts` (scorecard checks), `faces.ts` (file format and validation), `state.ts`
  and `lab.ts` (one simulated frame). The firmware is diffed against this code.
- `server/`: Hono API over `faces/` (`faces.ts`), the deploy pipeline (`deploy.ts`), the bot
  scan and OTA upload (`bots.ts`), and the child-process runner (`proc.ts`).
- `parity/`: the firmware-vs-lab check. `test.cpp` and `stub/` (firmware side), `lab.ts` (lab
  side), `parity.ts` (compare), `cli.ts`. Build outputs go to `parity/build/` (gitignored).
- `config/bots.json`: the allowlist of Kevin's bots, by `deviceName`.
- `client/src/`: the React UI; `Deploy.tsx` is the Deploy section. `window.__eyelab` exposes the model for test scripts.
