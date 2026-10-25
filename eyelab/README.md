# eyelab

The vizBot eye lab as a local app: before/after device canvases, the 25 faces, the
tuning panel and the scorecard. It replaces the claude.ai artifact lab. See `PLAN.md`.

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

Before any face is edited the output is byte-identical to the committed header.

## Layout

- `shared/`: the eye model with no DOM. `raster.ts` (RGB565 frame and primitives),
  `before.ts` (the old firmware renderer), `model.ts` and `motion.ts` (the after model),
  `measure.ts` (scorecard checks), `faces.ts` (file format and validation), `state.ts`
  and `lab.ts` (one simulated frame). The firmware is diffed against this code.
- `server/`: Hono API over `faces/`.
- `client/src/`: the React UI. `window.__eyelab` exposes the model for test scripts.
