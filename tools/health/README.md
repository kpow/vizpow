# vizBot health log

Day-to-day health of the vizBots, collected on the Mac. No firmware changes:
every 5 minutes a launchd job finds the bots on the network and reads their
status pages over WiFi.

## Use it

1. Install (once): `python3 tools/health/healthlog.py --install`
2. Check it runs, and see every bot it has met: `python3 tools/health/healthlog.py --status`
3. Weekly grade: `python3 tools/health/report.py` (`--all` for every bot's details, `--save` to keep a copy)
4. Remove: `python3 tools/health/healthlog.py --uninstall` (logs stay)

Re-run `--install` after changing `healthlog.py`: the job runs a copy in
`~/Library/Application Support/vizbot-health/`, so a deleted worktree can't
break it.

## How bots are found and told apart

Bots come and go, get renamed and get reflashed, so there is no bot list.

- Each run sweeps the Mac's /24 for port 80 (about 3 s) and asks each host
  for `/state`. Anything that answers like a vizBot is logged.
- A bot's id is its WiFi MAC, read from the Mac's ARP table. A new name,
  new firmware or new board keeps the same history.
- A bot seen in the last 7 days that isn't on the network gets an "away" line.
- Nicknames, bots to skip, and a subnet override go in
  `~/Library/Application Support/vizbot-health/config.json` (see
  `bots.example.json`). That file holds MACs, so it stays out of the repo.

## What it reads

Plain GETs only: `/state`, `/debug/tasks`, `/debug/crash`, and on a StackChan
`/debug/touchraw`. USB serial is never opened.

vizMini (the C3, `vizmini/` firmware) serves only `/state`: no uptime, crash
dump or task stats. Its reboots are seen when its least-ever free heap rises
(that can only fall within one boot), so some are missed. Its RAM low is shown
but not graded: the 16 KB floor was set on the StackChan.

## Where things land

- `~/Library/Logs/vizbot-health/health-YYYY-MM-DD.jsonl`: one line per bot per run
- `~/Library/Logs/vizbot-health/reboots/YYYY-MM-DD/<name>-HHMMSS/`: crash dump
  and tasks, plus on a StackChan the power log and reset reason, saved the
  moment a reboot or a new crash dump is seen
- `~/Library/Logs/vizbot-health/state.json`: last uptime, firmware and IP per MAC

## Reboots

Each reboot gets a kind:

- **crash**: a panic, watchdog or brownout reset, or a new crash dump. Graded.
- **unknown**: rebooted while on WiFi before and after, and the board can't
  say why (only the CoreS3 reports a reset reason). Graded, but a quick
  unplug and replug looks the same.
- **update**: firmware or board changed. Info.
- **power**: a power-on reset, or the bot was away first (unplugged). Info.
- **restart**: a software restart. Info.

A crash while a bot was away still shows up: the dump stays in flash.

## The bar

`report.py` uses the soak bar from `tools/soak/scorecard.py`: 0 crashes, 0
unknown reboots, 0 touch storms, 0 WiFi dead links, RAM low >= 16 KB, 0 failed
allocations. False wakes come from `~/Library/Logs/vizbrain/vizbrain.log`: a
wake with no speech sent within 3 minutes, at most 1 a day. Head taps are shown
but not graded, since real pats fire them too.
