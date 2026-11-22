#!/usr/bin/env python3
"""StackChan vizBot soak scorecard.

Leaves a bot alone for a set time, reads the health counters vizBot already
serves, and scores the run PASS or FAIL against a fixed bar. Nothing on the bot
is changed: every request is a plain GET of a status page.

  python3 tools/soak/scorecard.py 10.0.0.168                 24 h run
  python3 tools/soak/scorecard.py 10.0.0.168 --hours 2       shorter run
  python3 tools/soak/scorecard.py --report tools/soak/runs/<run>   score a run again
  python3 tools/soak/scorecard.py 10.0.0.168 --load heavy    above-normal use

--load drives the bot the way people use it, so the soak measures him working,
not idling: spoken voice turns (this Mac says "Hey vizBot ..." out loud through
its speaker, so the bot must be within earshot), head gestures, sounds, photos,
speech bubbles and screen gestures, at random times. "normal" is a busy day;
"heavy" is 4-5x that, plus a burst every 20 minutes that fires a voice turn,
photo, head move, sound and screen gesture within a few seconds of each other,
on purpose, to make the subsystems collide. Every action and its result goes
to events.jsonl and is scored. Head pads are never driven: nobody touches the
head, so any head tap is still a phantom.

Each run gets a folder in tools/soak/runs/: samples.jsonl (one line per poll)
and report.md (rewritten every 5 minutes, so a run can be read while it goes).
Hands off the bot while it runs: any head-touch tap counts as a phantom.
--load none is the idle soak.

Standard library only.
"""

import argparse
import datetime as dt
import json
import os
import random
import subprocess
import threading
import sys
import time
import urllib.request

FAST_S = 60          # /state and /brain/status
SLOW_S = 300         # touch, tasks, crash, servo, power: every 5 min
REPORT_S = 300

# The pass bar. Changing a number here changes what "stable" means: do it on
# purpose, in its own commit, never to make a run pass.
BAR = {
    "reach_pct_min": 99.0,        # polls answered
    "miss_run_max": 3,            # polls missed in a row (3 = 3 min unreachable)
    "reboots_max": 0,
    "new_crash_max": 0,
    "fw_changes_max": 0,
    "phantom_taps_max": 0,        # head taps fired, hands off
    "phantom_holds_max": 0,       # 2 s head holds fired (chill), hands off
    "touch_heals_max": 0,         # touch chip self-heals
    "brain_failures_max": 0,
    "brain_connect_fails_max": 0,
    "mic_restarts_max": 0,
    "wake_send_fails_max": 0,
    "wake_streaming_pct_min": 99.0,   # of polls, when wake is enabled
    "servo_reboots_max": 0,
    "heap_min_free_floor": 16384,     # internal RAM least-ever free, bytes
    "stack_free_floor": 512,          # least free stack of any task, bytes
    "psram_drop_max": 262144,         # PSRAM free, first poll to last, bytes
    "battery_drop_on_usb_max": 5,     # battery % lost while USB was present
}


# Actions per hour. Head moves stay gentle (the BSP's proven presets, at most
# one every 3 min on average even on heavy): no thrashing the servos.
PROFILES = {
    "none":   {},
    "normal": {"voice": 3, "head": 6, "sound": 6, "photo": 2, "bubble": 4, "screen": 6},
    "heavy":  {"voice": 12, "head": 20, "sound": 30, "photo": 10, "bubble": 20, "screen": 30, "burst": 3},
}
QUESTIONS = ["What time is it?", "Tell me a fun fact.", "How are you feeling?",
             "What's the weather like for a robot?", "Say something nice.", "What day is it?"]
BAR_USAGE = {"action_ok_pct_min": 98.0, "voice_answered_pct_min": 90.0}


def get(host, path, timeout=6):
    try:
        with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as r:
            return json.loads(r.read().decode("utf-8", "replace"))
    except Exception:
        return None


def poll(host, slow):
    s = {"t": time.time()}
    st = get(host, "/state")
    s["ok"] = st is not None
    if not st:
        return s
    sysd = st.get("sys") or {}
    s["fw"] = st.get("firmwareVersion")
    s["upSec"] = sysd.get("upSec")
    b = get(host, "/brain/status")
    if b:
        w = b.get("wake") or {}
        s["brain"] = {
            "enabled": b.get("enabled"), "state": b.get("state"),
            "turns": b.get("turns"), "failures": b.get("failures"),
            "connectFails": b.get("connectFails"), "micRestarts": b.get("micRestarts"),
            "psramFree": b.get("psramFree"),
            "wakeEnabled": w.get("enabled"), "wakeStreaming": w.get("streaming"),
            "wakeSendFails": w.get("sendFails"), "wakeMicRestarts": w.get("micRestarts"),
        }
    if slow:
        t = get(host, "/debug/touchraw", timeout=10)
        if t:
            f = t.get("fired") or {}
            s["touch"] = {
                "front": f.get("front"), "back": f.get("back"), "hold": f.get("hold"),
                "rejected": f.get("rejected"), "contacts": t.get("contactCount"),
                "heals": t.get("heals"), "chipStarts": t.get("chipStarts"),
            }
        k = get(host, "/debug/tasks")
        if k:
            sf = k.get("stackFree") or {}
            s["tasks"] = {
                "heapMinFree": k.get("heapMinFree"), "heapFree": k.get("heapFree"),
                "stackMin": min(sf.values()) if sf else None,
                "stackMinTask": min(sf, key=sf.get) if sf else None,
            }
        c = get(host, "/debug/crash")
        if c is not None:
            s["crash"] = {"haveDump": c.get("haveDump"), "pc": c.get("pc"),
                          "elfSha": c.get("elfSha"), "task": c.get("task"),
                          "reason": c.get("reason")}
        v = get(host, "/debug/servo")
        if v:
            s["servo"] = {"reboots": v.get("reboots")}
        p = get(host, "/debug/power")
        if p:
            s["power"] = {"resetReason": p.get("resetReason"), "batteryPct": p.get("batteryPct"),
                          "vbusMv": p.get("vbusMv"), "charging": p.get("charging")}
    return s


# ---- usage driver ---------------------------------------------------------------

class Driver(threading.Thread):
    def __init__(self, host, profile, events_path, seed):
        super().__init__(daemon=True)
        self.host, self.rates = host, PROFILES[profile]
        self.out = open(events_path, "a")
        self.rng = random.Random(seed)
        self.stop = threading.Event()
        self.lock = threading.Lock()

    def log(self, action, ok, detail=""):
        with self.lock:
            self.out.write(json.dumps({"t": time.time(), "action": action, "ok": bool(ok), "detail": detail}) + "\n")
            self.out.flush()

    def hit(self, action, path):
        try:
            with urllib.request.urlopen(f"http://{self.host}{path}", timeout=10) as r:
                ok = r.status == 200
        except Exception as e:
            ok = False
            path += f"  ({type(e).__name__})"
        self.log(action, ok, path)
        return ok

    def voice(self):
        """Speak a wake phrase and a question out loud, then check the bot
        answered: brain turns went up and it is idle again."""
        before = get(self.host, "/brain/status")
        if not before or before.get("state") != "idle":
            self.log("voice", False, "bot not idle before the turn")
            return
        q = self.rng.choice(QUESTIONS)
        try:
            subprocess.run(["say", "Hey vizBot"], timeout=15)
            time.sleep(2.5)
            subprocess.run(["say", q], timeout=20)
        except Exception as e:
            self.log("voice", False, f"say failed: {e}")
            return
        deadline = time.time() + 60
        after = None
        while time.time() < deadline:
            time.sleep(3)
            after = get(self.host, "/brain/status")
            if after and after.get("state") == "idle" and (after.get("turns") or 0) > (before.get("turns") or 0):
                break
        answered = bool(after) and (after.get("turns") or 0) > (before.get("turns") or 0) and \
            (after.get("failures") or 0) == (before.get("failures") or 0)
        self.log("voice", answered, f"{q} | heard: {(after or {}).get('lastHeard', '')!s:.60} | "
                                    f"err: {(after or {}).get('lastError', '')}")

    def act(self, kind):
        r = self.rng
        if kind == "voice":
            self.voice()
        elif kind == "head":
            self.hit("head", "/bot/head/preset?name=" + r.choice(["nod", "shake", "lookup", "left", "right"]))
        elif kind == "sound":
            self.hit("sound", f"/bot/sound?seq={r.randint(1, 10)}")
        elif kind == "photo":
            self.hit("photo", "/bot/photo/capture")
        elif kind == "bubble":
            self.hit("bubble", "/bot/say?text=" + r.choice(["Hi!", "Testing", "Beep", "Hello"]) + "&dur=2500")
        elif kind == "screen":
            self.hit("screen", "/debug/touch?g=" + r.choice(["tap", "left", "right"]) + "&x=160&y=120")
        elif kind == "burst":   # everything at once, on purpose
            threads = [threading.Thread(target=self.act, args=(k,)) for k in ("voice", "photo", "head", "sound", "screen")]
            for i, t in enumerate(threads):
                t.start()
                time.sleep(r.uniform(0.2, 1.5))
            for t in threads:
                t.join(timeout=90)
            self.log("burst", True, "voice+photo+head+sound+screen")

    def run(self):
        if not self.rates:
            return
        total = sum(self.rates.values())
        kinds, weights = list(self.rates), list(self.rates.values())
        while not self.stop.is_set():
            # Poisson arrivals at the profile's total rate; the kind by its share.
            if self.stop.wait(self.rng.expovariate(total / 3600.0)):
                break
            self.act(self.rng.choices(kinds, weights)[0])


# ---- scoring ------------------------------------------------------------------

def counter_delta(samples, pick):
    """Growth of a counter over the run. A counter that drops (the bot rebooted)
    starts a new segment, and its new value counts as growth from zero."""
    total, last = 0, None
    for s in samples:
        v = pick(s)
        if v is None:
            continue
        if last is not None:
            total += v - last if v >= last else v
        last = v
    return total


def dig(*keys):
    def f(s):
        cur = s
        for k in keys:
            if not isinstance(cur, dict):
                return None
            cur = cur.get(k)
        return cur
    return f


def score(samples, events=()):
    rows = []   # (check, value, bar, ok, note)

    def check(name, value, ok, bar, note=""):
        rows.append((name, value, bar, ok, note))

    n = len(samples)
    answered = [s for s in samples if s.get("ok")]
    reach = 100.0 * len(answered) / n if n else 0.0
    run = worst = 0
    for s in samples:
        run = 0 if s.get("ok") else run + 1
        worst = max(worst, run)
    check("Reachable", f"{reach:.1f}%", reach >= BAR["reach_pct_min"], f">= {BAR['reach_pct_min']}%")
    check("Longest unreachable", f"{worst} polls", worst <= BAR["miss_run_max"], f"<= {BAR['miss_run_max']}")

    reboots, last = 0, None
    for s in answered:
        u = s.get("upSec")
        if u is None:
            continue
        if last is not None and u < last:
            reboots += 1
        last = u
    reasons = sorted({s["power"]["resetReason"] for s in samples if s.get("power", {}).get("resetReason") is not None})
    check("Reboots", reboots, reboots <= BAR["reboots_max"], "0",
          f"reset reasons seen: {reasons} (9 brownout, 4 panic, 6/7 watchdog)" if reboots else "")

    fws = [s["fw"] for s in answered if s.get("fw")]
    changes = sum(1 for a, b in zip(fws, fws[1:]) if a != b)
    check("Firmware changed", changes, changes <= BAR["fw_changes_max"], "0",
          f"{fws[0]} -> {fws[-1]}" if changes else (fws[0] if fws else ""))

    crashes = [s["crash"] for s in samples if s.get("crash")]
    first = crashes[0] if crashes else {}
    new = 0
    for c in crashes[1:]:
        if c.get("haveDump") and (c.get("pc"), c.get("elfSha")) != (first.get("pc"), first.get("elfSha")):
            new = 1
            first = c
    check("New crash dump", new, new <= BAR["new_crash_max"], "0",
          f"{first.get('task')} {first.get('reason')} pc {first.get('pc')}" if new else "")

    taps = counter_delta(samples, dig("touch", "front")) + counter_delta(samples, dig("touch", "back"))
    holds = counter_delta(samples, dig("touch", "hold"))
    heals = counter_delta(samples, dig("touch", "heals"))
    contacts = counter_delta(samples, dig("touch", "contacts"))
    check("Phantom head taps", taps, taps <= BAR["phantom_taps_max"], "0", f"{contacts} contacts logged")
    check("Phantom head holds", holds, holds <= BAR["phantom_holds_max"], "0")
    check("Touch chip self-heals", heals, heals <= BAR["touch_heals_max"], "0")

    bf = counter_delta(samples, dig("brain", "failures"))
    bc = counter_delta(samples, dig("brain", "connectFails"))
    mr = counter_delta(samples, dig("brain", "micRestarts")) + counter_delta(samples, dig("brain", "wakeMicRestarts"))
    ws = counter_delta(samples, dig("brain", "wakeSendFails"))
    check("Brain failures", bf, bf <= BAR["brain_failures_max"], "0")
    check("Brain connect fails", bc, bc <= BAR["brain_connect_fails_max"], "0")
    check("Mic restarts", mr, mr <= BAR["mic_restarts_max"], "0")
    check("Wake send fails", ws, ws <= BAR["wake_send_fails_max"], "0")
    we = [s["brain"] for s in samples if s.get("brain", {}).get("wakeEnabled")]
    if we:
        pct = 100.0 * sum(1 for b in we if b.get("wakeStreaming")) / len(we)
        check("Wake word streaming", f"{pct:.1f}%", pct >= BAR["wake_streaming_pct_min"],
              f">= {BAR['wake_streaming_pct_min']}%")

    sr = counter_delta(samples, dig("servo", "reboots"))
    check("Servo bus reboots", sr, sr <= BAR["servo_reboots_max"], "0")

    hm = [s["tasks"]["heapMinFree"] for s in samples if s.get("tasks", {}).get("heapMinFree") is not None]
    if hm:
        check("Internal RAM least free", min(hm), min(hm) >= BAR["heap_min_free_floor"],
              f">= {BAR['heap_min_free_floor']}")
    sm = [(s["tasks"]["stackMin"], s["tasks"]["stackMinTask"]) for s in samples
          if s.get("tasks", {}).get("stackMin") is not None]
    if sm:
        lo = min(sm)
        check("Task stack least free", lo[0], lo[0] >= BAR["stack_free_floor"],
              f">= {BAR['stack_free_floor']}", lo[1])
    ps = [s["brain"]["psramFree"] for s in samples if s.get("brain", {}).get("psramFree") is not None]
    if len(ps) >= 2:
        drop = ps[0] - ps[-1]
        check("PSRAM lost", drop, drop <= BAR["psram_drop_max"], f"<= {BAR['psram_drop_max']}")

    usb = [s["power"] for s in samples if s.get("power", {}).get("batteryPct") is not None]
    if usb and all((p.get("vbusMv") or 0) > 4000 for p in usb):
        drop = usb[0]["batteryPct"] - min(p["batteryPct"] for p in usb)
        check("Battery lost on USB", f"{drop}%", drop <= BAR["battery_drop_on_usb_max"],
              f"<= {BAR['battery_drop_on_usb_max']}%")
    elif usb:
        check("Battery lost on USB", "n/a", True, "", "USB was not present the whole run")

    acts = [e for e in events if e["action"] not in ("voice", "burst")]
    if acts:
        pct = 100.0 * sum(e["ok"] for e in acts) / len(acts)
        bad = sorted({e["action"] for e in acts if not e["ok"]})
        check("Usage actions answered", f"{pct:.1f}% of {len(acts)}", pct >= BAR_USAGE["action_ok_pct_min"],
              f">= {BAR_USAGE['action_ok_pct_min']}%", f"failed: {', '.join(bad)}" if bad else "")
    voices = [e for e in events if e["action"] == "voice"]
    if voices:
        pct = 100.0 * sum(e["ok"] for e in voices) / len(voices)
        check("Spoken voice turns answered", f"{pct:.1f}% of {len(voices)}",
              pct >= BAR_USAGE["voice_answered_pct_min"], f">= {BAR_USAGE['voice_answered_pct_min']}%")
    return rows


def report(run_dir):
    path = os.path.join(run_dir, "samples.jsonl")
    with open(path) as f:
        samples = [json.loads(l) for l in f if l.strip()]
    meta = {}
    mp = os.path.join(run_dir, "meta.json")
    if os.path.exists(mp):
        meta = json.load(open(mp))
    events = []
    ep = os.path.join(run_dir, "events.jsonl")
    if os.path.exists(ep):
        with open(ep) as f:
            events = [json.loads(l) for l in f if l.strip()]
    rows = score(samples, events) if samples else []
    passed = bool(rows) and all(r[3] for r in rows)
    hours = (samples[-1]["t"] - samples[0]["t"]) / 3600 if len(samples) > 1 else 0
    target = meta.get("hours")
    done = target is not None and hours >= target - 0.05
    verdict = ("PASS" if passed else "FAIL") if done else ("RUNNING, failing" if not passed else "RUNNING, clean so far")
    fws = [s.get("fw") for s in samples if s.get("fw")]
    lines = [
        f"# Soak scorecard: {verdict}",
        "",
        f"- Bot: {meta.get('host', '?')}  firmware {fws[-1] if fws else '?'}",
        f"- Started {dt.datetime.fromtimestamp(samples[0]['t']).strftime('%Y-%m-%d %H:%M') if samples else '?'}, "
        f"ran {hours:.2f} h of {target} h, {len(samples)} polls",
        f"- Load: {meta.get('load', 'none')}  (seed {meta.get('seed')}), {len(events)} usage events",
        f"- Note: {meta.get('note', '')}",
        "",
        "| Check | Result | Bar | |",
        "|---|---|---|---|",
    ]
    for name, value, bar, ok, note in rows:
        lines.append(f"| {name} | {value}{' (' + note + ')' if note else ''} | {bar} | {'ok' if ok else 'FAIL'} |")
    lines += ["", "A run passes only when every row is ok after the full time. Nobody touches the bot."]
    with open(os.path.join(run_dir, "report.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return verdict, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", help="bot IP, e.g. 10.0.0.168")
    ap.add_argument("--hours", type=float, default=24.0)
    ap.add_argument("--note", default="", help="what this run is testing")
    ap.add_argument("--load", choices=list(PROFILES), default="normal", help="usage to drive (default normal)")
    ap.add_argument("--seed", type=int, default=None, help="repeat a run's exact action schedule")
    ap.add_argument("--report", metavar="RUN_DIR", help="score an existing run and stop")
    a = ap.parse_args()

    if a.report:
        verdict, rows = report(a.report)
        print(open(os.path.join(a.report, "report.md")).read())
        sys.exit(0 if verdict == "PASS" else 1)
    if not a.host:
        ap.error("give a bot IP, or --report RUN_DIR")

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M")
    run_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "runs", f"{stamp}-{a.host}")
    os.makedirs(run_dir, exist_ok=True)
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    json.dump({"host": a.host, "hours": a.hours, "note": a.note, "load": a.load, "seed": seed,
               "bar": BAR, "barUsage": BAR_USAGE, "profile": PROFILES[a.load]},
              open(os.path.join(run_dir, "meta.json"), "w"), indent=1)
    print(f"Soak run: {run_dir}  load {a.load}  seed {seed}", flush=True)
    driver = Driver(a.host, a.load, os.path.join(run_dir, "events.jsonl"), seed)
    driver.start()

    end = time.time() + a.hours * 3600
    next_slow = next_report = 0.0
    with open(os.path.join(run_dir, "samples.jsonl"), "a") as out:
        while True:
            now = time.time()
            slow = now >= next_slow
            s = poll(a.host, slow)
            if slow and s.get("ok"):
                next_slow = now + SLOW_S
            out.write(json.dumps(s) + "\n")
            out.flush()
            if now >= next_report or now >= end:
                next_report = now + REPORT_S
                verdict, rows = report(run_dir)
                fails = [r[0] for r in rows if not r[3]]
                h = (now - (end - a.hours * 3600)) / 3600
                print(f"{dt.datetime.now():%H:%M} {h:5.2f}h  {verdict}" + (f"  failing: {', '.join(fails)}" if fails else ""),
                      flush=True)
            if now >= end:
                driver.stop.set()
                break
            time.sleep(max(1.0, FAST_S - (time.time() - now)))
    verdict, _ = report(run_dir)
    print(open(os.path.join(run_dir, "report.md")).read())
    sys.exit(0 if verdict == "PASS" else 1)


if __name__ == "__main__":
    main()
