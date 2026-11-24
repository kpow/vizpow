# vizBot health log

Day-to-day health of the vizBots, collected on the Mac. No firmware changes:
every 5 minutes a launchd job reads each bot's status pages over WiFi.

## Use it

1. Install (once): `python3 tools/health/healthlog.py --install`
2. Check it runs: `python3 tools/health/healthlog.py --status`
3. Weekly grade: `python3 tools/health/report.py` (add `--save` to keep a copy)
4. Remove: `python3 tools/health/healthlog.py --uninstall` (logs stay)

Re-run `--install` after changing `healthlog.py` or `bots.json`: the job runs a
copy in `~/Library/Application Support/vizbot-health/`, so a deleted worktree
can't break it.

## What it reads

Per bot, plain GETs only: `/state`, `/debug/tasks`, `/debug/crash`, and on a
StackChan `/debug/touchraw`. USB serial is never opened. A bot that doesn't
answer is logged as offline.

## Where things land

- `~/Library/Logs/vizbot-health/health-YYYY-MM-DD.jsonl`: one line per bot per poll
- `~/Library/Logs/vizbot-health/reboots/YYYY-MM-DD/<bot>-HHMMSS/`: crash dump,
  tasks, and on a StackChan the power log, saved the moment a reboot or a new
  crash dump is seen
- `~/Library/Logs/vizbot-health/state.json`: IP cache and last uptime per bot

## Bots

`bots.json` lists hostnames (never IPs). Any `vizbot-*.local` that mDNS finds
is added on its own; the browse runs at most once an hour.

## The bar

`report.py` uses the soak bar from `tools/soak/scorecard.py` (0 reboots, 0 new
crash dumps, 0 touch storms, 0 WiFi dead links, RAM low >= 16 KB, 0 failed
allocations) plus false wakes from `~/Library/Logs/vizbrain/vizbrain.log`:
a wake with no speech sent within 3 minutes, at most 1 a day. Firmware updates
are not counted as reboots. Head taps are shown but not graded, since real pats
fire them too.
