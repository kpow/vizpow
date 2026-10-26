// `npm run parity` (from eyelab/): the deploy pipeline's parity step on its own, against the
// current vizbot/bot_eye_params.h. `--params <file>` tests another header instead, which is
// how to check that a deliberately wrong firmware value fails. Exits 1 on a failure.

import { resolve } from "node:path";
import { describeParity, runParity } from "./parity";

const args = process.argv.slice(2);
const at = args.indexOf("--params");
const params = at >= 0 ? resolve(args[at + 1] ?? "") : undefined;
const repo = new URL("../..", import.meta.url).pathname;

runParity({ repo, params, log: (l) => console.log(l) }).then((r) => {
  for (const l of describeParity(r)) console.log(l);
  console.log(r.ok ? "parity: PASS" : "parity: FAIL");
  process.exit(r.ok ? 0 : 1);
}, (err) => { console.error(`parity: ${err instanceof Error ? err.message : err}`); process.exit(1); });
