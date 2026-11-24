#!/usr/bin/env python3
"""Weekly vizBot health report.

Grades each bot over the last 7 days (or --days N) from the lines healthlog.py
collects, PASS or FAIL per row, against the soak scorecard's bar
(tools/soak/scorecard.py BAR), plus false wakes from vizbrain.log.

  python3 tools/health/report.py              last 7 days, printed
  python3 tools/health/report.py --days 1     just today-ish
  python3 tools/health/report.py --all        details for every bot, not just failing ones
  python3 tools/health/report.py --save       also write it to
                                              ~/Library/Logs/vizbot-health/reports/

Bots are told apart by MAC address, so a renamed or reflashed bot keeps its
history. A bot passes when every graded row is ok; "info" rows are never graded.
Exit code 0 when every bot passes, 1 otherwise.
"""

import argparse
import datetime as dt
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "soak"))
from scorecard import BAR, counter_delta, dig  # noqa: E402

LOG_DIR = os.environ.get("VIZBOT_HEALTH_DIR") or os.path.expanduser("~/Library/Logs/vizbot-health")
VIZBRAIN_LOG = os.path.expanduser("~/Library/Logs/vizbrain/vizbrain.log")

# Day-to-day bar on top of the soak's. Changing a number here changes what
# "healthy" means: do it on purpose, in its own commit, never to make a week pass.
WEEK_BAR = {
    "false_wakes_per_day_max": 1.0,   # wakes with no voice turn after them
    "wake_turn_window_s": 180,        # a wake is real if the bot sends speech this soon
}


def load_lines(since):
    lines = []
    for p in sorted(glob.glob(os.path.join(LOG_DIR, "health-*.jsonl"))):
        day = os.path.basename(p)[7:17]
        if day < dt.datetime.fromtimestamp(since).strftime("%Y-%m-%d"):
            continue
        with open(p) as f:
            for l in f:
                if l.strip():
                    try:
                        d = json.loads(l)
                    except ValueError:
                        continue
                    if d.get("t", 0) >= since:
                        lines.append(d)
    lines.sort(key=lambda d: d["t"])
    return lines


# ---- vizbrain wakes -----------------------------------------------------------

STAMP = re.compile(r"^(?:(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d) )+\[(\w[\w-]*)\] (.*)$")
BARE = re.compile(r"^\[(\w[\w-]*)\] (.*)$")   # a line printed without a stamp
WAKE = re.compile(r"Hey vizBot from (\S+) \(score ([\d.]+)\)")
VOICE_POST = re.compile(r'^(\S+) "POST /v1/voice')
TURN = re.compile(r"^voice: heard (\d+) words")


def read_wakes(since, until):
    """{ip: [(t, real, score)]} for '[wake] Hey vizBot' lines in the window.

    A wake is real when the bot then sends speech (POST /v1/voice from its IP)
    before its next wake and within the window, and the turn for it heard at
    least one word. Anything else is a false wake. Lines vizbrain printed
    without a stamp take the time of the line before them."""
    if not os.path.exists(VIZBRAIN_LOG):
        return None
    now = dt.datetime.now()
    win = WEEK_BAR["wake_turn_window_s"]
    events = []   # [t, kind, ip, value]; a post's value becomes its turn's word count
    t = None
    last_post = None
    with open(VIZBRAIN_LOG, errors="replace") as f:
        for l in f:
            l = l.rstrip("\n")
            m = STAMP.match(l)
            if m:
                mo, d, H, M, S, tag, msg = m.groups()
                try:
                    when = dt.datetime(now.year, int(mo), int(d), int(H), int(M), int(S))
                except ValueError:
                    continue
                if when > now + dt.timedelta(days=1):   # December line read in January
                    when = when.replace(year=now.year - 1)
                t = when.timestamp()
            else:
                b = BARE.match(l)
                if not b or t is None:
                    continue
                tag, msg = b.groups()
            if t < since - win or t > until:
                continue
            if tag == "wake":
                w = WAKE.search(msg)
                if w:
                    events.append([t, "wake", w.group(1), float(w.group(2))])
            elif tag == "http":
                v = VOICE_POST.match(msg)
                if v:
                    last_post = [t, "post", v.group(1), None]
                    events.append(last_post)
            elif tag == "turn":
                v = TURN.match(msg)
                if v and last_post is not None and last_post[3] is None:
                    last_post[3] = int(v.group(1))
    out = {}
    for i, (t, kind, ip, score) in enumerate(events):
        if kind != "wake" or t < since:
            continue
        real = False
        for et, k, eip, n in events[i + 1:]:
            if et > t + win:
                break
            if eip != ip:
                continue
            if k == "wake":
                break
            if k == "post":
                real = n is None or n > 0
                break
        out.setdefault(ip, []).append((t, real, score))
    return out


# ---- grading ------------------------------------------------------------------

def hm(t):
    return dt.datetime.fromtimestamp(t).strftime("%a %H:%M")


def label(lines):
    last = lines[-1]
    return last.get("nick") or next((d.get("name") for d in reversed(lines) if d.get("name")), last["id"])


def grade(lines, wakes, window):
    """Rows (check, value, bar, ok, note) for one bot; ok None = info."""
    rows = []

    def check(name, value, ok, bar, note=""):
        rows.append((name, value, bar, ok, note))

    on = [d for d in lines if d.get("ok")]
    hours = len(on) * 5 / 60.0
    check("On the network", "%.1f h, %d of %d runs" % (hours, len(on), len(lines)), None, "info")
    if not on:
        return rows
    stackchan = any(d.get("board") == "stackchan" for d in on)

    names = sorted({d["name"] for d in on if d.get("name")})
    boards = sorted({d["board"] for d in on if d.get("board")})
    fws = [d["fw"] for d in on if d.get("fw")]
    notes = []
    if len(names) > 1:
        notes.append("names " + ", ".join(names))
    if len(boards) > 1:
        notes.append("boards " + ", ".join(boards))
    check("Firmware", fws[-1] if fws else "?", None, "info", "; ".join(notes))

    rb = [d for d in on if d.get("reboot")]
    kinds = {}
    for d in rb:
        kinds.setdefault(d["reboot"].get("kind", "unknown"), []).append(d)
    crashes = [d for d in on if d.get("newCrash") or d.get("reboot", {}).get("kind") == "crash"]
    check("Crashes", len(crashes), len(crashes) <= BAR["new_crash_max"], str(BAR["new_crash_max"]),
          "; ".join("%s %s %s" % (hm(d["t"]), (d.get("crash") or {}).get("task") or "",
                                  d.get("capture", "")) for d in crashes[:3]))
    unk = kinds.get("unknown", [])
    check("Reboots, cause unknown", len(unk), len(unk) <= BAR["reboots_max"], str(BAR["reboots_max"]),
          ("on WiFi before and after; a quick replug looks the same. " if unk else "") +
          "; ".join("%s %s" % (hm(d["t"]), d.get("capture", "")) for d in unk[:3]))
    other = ["%d %s" % (len(kinds[k]), k) for k in ("update", "power", "restart") if kinds.get(k)]
    check("Other restarts", sum(len(kinds.get(k, [])) for k in ("update", "power", "restart")), None, "info",
          ", ".join(other))

    if stackchan:
        heals = counter_delta(on, dig("touch", "heals"))
        taps = counter_delta(on, dig("touch", "front")) + counter_delta(on, dig("touch", "back"))
        contacts = counter_delta(on, dig("touch", "contacts"))
        ignored = counter_delta(on, dig("touch", "ignored"))
        # Real pats fire taps too, so taps alone can't be graded day to day. A
        # storm or latch (the chip's self-heal) only happens with no finger there.
        check("Phantom head touch (storms)", heals, heals <= BAR["touch_heals_max"], str(BAR["touch_heals_max"]),
              "%d taps fired, %d contacts, %d ignored" % (taps, contacts, ignored))

    nets = [d for d in on if d.get("net")]
    if nets:
        dead = max(counter_delta(nets, dig("net", "dead")), counter_delta(nets, dig("net", "deadEver")))
        rs = [d["net"]["rssi"] for d in nets if d["net"].get("rssi")]
        check("WiFi dead links", dead, dead <= 0, "0", "weakest %d dBm" % min(rs) if rs else "")

    lows = [(d["tasks"]["heapMinFree"], d["t"]) for d in on if (d.get("tasks") or {}).get("heapMinFree") is not None]
    if lows:
        lo, t = min(lows)
        floor = BAR["heap_min_free_floor"]
        rl = next(((d["tasks"]["ramLow"], d) for d in on
                   if (d.get("tasks") or {}).get("heapMinFree") == lo and d["tasks"].get("ramLow")), None)
        when = "first seen %s" % hm(t)
        if rl:   # the bot's own record of the low: uptime -> wall clock
            low, d = rl
            if d.get("upSec") is not None and low.get("sec") is not None:
                when = "at %s, busy: %s" % (hm(d["t"] - d["upSec"] + low["sec"]), (low.get("busy") or "-").strip() or "-")
        check("Internal RAM least free", lo, lo >= floor, ">= %d" % floor, when)
    af = counter_delta(on, dig("tasks", "allocFails"))
    afp = counter_delta(on, dig("tasks", "allocFailsPsram"))
    check("Failed allocations", af, af <= 0, "0", "%d in PSRAM" % afp if afp else "")

    if wakes is not None and (wakes or stackchan):
        false = [w for w in wakes if not w[1]]
        cap = WEEK_BAR["false_wakes_per_day_max"] * max(window, 1.0)
        check("False wakes", "%d of %d wakes" % (len(false), len(wakes)), len(false) <= cap,
              "<= %.0f (%.0f/day)" % (cap, WEEK_BAR["false_wakes_per_day_max"]),
              "; ".join("%s score %.2f" % (hm(t), sc) for t, _, sc in false[:3]))
    return rows


def attribute_wakes(wakes, lines):
    """{bot id: [wake]}: each wake goes to the bot that had that IP nearest in time
    (DHCP can move an IP between bots over a week). Returns (by_id, unmatched)."""
    by_ip = {}
    for d in lines:
        if d.get("ok") and d.get("ip"):
            by_ip.setdefault(d["ip"], []).append((d["t"], d["id"]))
    out, unmatched = {}, 0
    start = min((d["t"] for d in lines), default=0) - 600
    for ip, ws in (wakes or {}).items():
        for w in ws:
            if w[0] < start:   # before the logger ran: nothing to match it to
                continue
            near = [(abs(t - w[0]), bid) for t, bid in by_ip.get(ip, []) if abs(t - w[0]) < 3600]
            if near:
                out.setdefault(min(near)[1], []).append(w)
            else:
                unmatched += 1
    return out, unmatched


def build(days, show_all):
    until = dt.datetime.now().timestamp()
    since = until - days * 86400
    lines = load_lines(since)
    wakes = read_wakes(since, until)
    by_id = {}
    for d in lines:
        if d.get("id"):
            by_id.setdefault(d["id"], []).append(d)
    bot_wakes, unmatched = attribute_wakes(wakes, lines)
    covered = (lines[-1]["t"] - lines[0]["t"]) / 86400 if len(lines) > 1 else 0.0

    results = []
    for bid, bl in by_id.items():
        rows = grade(bl, bot_wakes.get(bid, []) if wakes is not None else None, days)
        graded = [r for r in rows if r[3] is not None]
        if not graded:
            verdict = "away"
        else:
            verdict = "PASS" if all(r[3] for r in graded) else "FAIL"
        results.append((label(bl), bid, bl, rows, verdict))
    results.sort(key=lambda r: ({"FAIL": 0, "PASS": 1, "away": 2}[r[4]], r[0].lower()))

    out = ["# vizBot health, last %g days" % days, "",
           "- %s to %s; the log covers %.1f days, %d bots" % (
               dt.datetime.fromtimestamp(since).strftime("%Y-%m-%d %H:%M"),
               dt.datetime.fromtimestamp(until).strftime("%Y-%m-%d %H:%M"), covered, len(results))]
    if wakes is None:
        out.append("- vizbrain.log not found: false wakes not graded")
    elif unmatched:
        out.append("- %d wake(s) from an IP no logged bot had at the time" % unmatched)
    out += ["", "| Bot | MAC | Board | Firmware | On WiFi | Verdict | Failing |", "|---|---|---|---|---|---|---|"]
    for name, bid, bl, rows, verdict in results:
        on = [d for d in bl if d.get("ok")]
        last = on[-1] if on else bl[-1]
        failing = ", ".join(r[0] for r in rows if r[3] is False)
        out.append("| %s | %s | %s | %s | %.1f h | %s | %s |" % (
            name, bid, last.get("board", "?"), last.get("fw", "?"), len(on) * 5 / 60.0, verdict, failing))

    for name, bid, bl, rows, verdict in results:
        if verdict == "away" or (verdict == "PASS" and not show_all):
            continue
        out += ["", "## %s (%s): %s" % (name, bid, verdict), "", "| Check | Result | Bar | |", "|---|---|---|---|"]
        for cname, value, bar, rok, note in rows:
            mark = "info" if rok is None else ("ok" if rok else "FAIL")
            out.append("| %s | %s%s | %s | %s |" % (cname, value, " (%s)" % note if note else "", bar, mark))
    if not results:
        out += ["", "No log lines in this window. Is the logger running? python3 tools/health/healthlog.py --status"]
    out += ["", "Details show for failing bots (--all for every bot). Reboot dumps: ~/Library/Logs/vizbot-health/reboots/"]
    ok = bool(results) and all(r[4] != "FAIL" for r in results)
    return "\n".join(out) + "\n", ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--days", type=float, default=7.0)
    ap.add_argument("--all", action="store_true", help="details for every bot, not just failing ones")
    ap.add_argument("--save", action="store_true", help="also write the report to the log folder")
    a = ap.parse_args()
    text, ok = build(a.days, a.all)
    print(text, end="")
    if a.save:
        d = os.path.join(LOG_DIR, "reports")
        os.makedirs(d, exist_ok=True)
        p = os.path.join(d, "health-%s.md" % dt.datetime.now().strftime("%Y-%m-%d"))
        with open(p, "w") as f:
            f.write(text)
        print("Saved %s" % p)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
