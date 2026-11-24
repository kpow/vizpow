#!/usr/bin/env python3
"""Interaction suite for vizBot on StackChan: every interaction once, then all at once.

Each test drives one interaction, checks that it did what it should, and checks
that nothing else broke while it ran: no phantom head taps or holds, no touch
chip self-heal, no reboot, no new crash dump, no refused allocation, internal
RAM above the floor, no AO dropping events it should not.

  python3 tools/soak/interactions.py 10.0.0.168            automatic tests (~12 min)
  python3 tools/soak/interactions.py 10.0.0.168 --touch    + guided touch tests (needs you)
  python3 tools/soak/interactions.py 10.0.0.168 --only voice,head

Voice tests speak through this Mac's output (`say`): its speaker must reach the bot.
Report: tools/soak/runs/<time>-interactions/report.md
"""
import argparse
import json
import os
import subprocess
import sys
import threading
import time
import urllib.request

RAM_FLOOR = 16384          # same as the soak bar (scorecard.py)
LOOP_STALL_MAX_MS = 200    # longest loop period allowed during a test
MK_GESTURE, MK_RECOVER = 2, 3   # ScMotionKind (stackchan_base.h)
RESULTS = []
HOST = ""


# ---- HTTP -------------------------------------------------------------------

def req(path, method="GET", timeout=8, raw=False):
    try:
        r = urllib.request.Request(f"http://{HOST}{path}", method=method)
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            body = resp.read()
            if raw:
                return resp.status, body
            return json.loads(body.decode("utf-8", "replace"))
    except Exception as e:
        return (None, str(e).encode()) if raw else None


def touch():
    t = req("/debug/touchraw") or {}
    f = t.get("fired") or {}
    return {"front": f.get("front", 0), "back": f.get("back", 0), "hold": f.get("hold", 0),
            "heals": t.get("heals", 0), "contacts": t.get("contactCount", 0),
            "refs": t.get("refs", 0), "needRef": t.get("needRef")}


def tasks():
    return req("/debug/tasks") or {}


def timeline(n=400):
    return (req(f"/debug/timeline?n={n}") or {}).get("events", [])


def up_ms():
    return (req("/debug/timeline?n=1") or {}).get("upMs", 0)


def loop_max_ms():
    s = req("/state") or {}
    return ((s.get("loop") or {}).get("periodMaxUs", 0)) // 1000


def brain():
    return req("/brain/status") or {}


def wait(cond, timeout, step=0.25):
    end = time.time() + timeout
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return None


# ---- invariants ---------------------------------------------------------------

def snapshot():
    k = tasks()
    c = req("/debug/crash") or {}
    return {"touch": touch(), "upSec": k.get("upSec", 0), "allocFails": k.get("allocFails", 0),
            "low": k.get("heapMinFree", 0), "overlaps": k.get("netOverlaps", 0),
            "crash": (c.get("haveDump"), c.get("pc"), c.get("elfSha")),
            "fw": (req("/state") or {}).get("firmwareVersion")}


def invariants(before, after, touch_ok=False):
    """What must not change during a test (unless the test is about touch)."""
    bad = []
    bt, at = before["touch"], after["touch"]
    if not touch_ok:
        for k in ("front", "back", "hold"):
            if at[k] != bt[k]:
                bad.append(f"phantom {k} x{at[k] - bt[k]}")
    if at["heals"] != bt["heals"]:
        bad.append(f"touch self-heal x{at['heals'] - bt['heals']}")
    if after["upSec"] < before["upSec"]:
        bad.append("REBOOTED")
    if after["crash"] != before["crash"]:
        bad.append(f"new crash dump {after['crash']}")
    if after["allocFails"] != before["allocFails"]:
        bad.append(f"refused allocations x{after['allocFails'] - before['allocFails']}")
    if after["low"] and after["low"] < RAM_FLOOR:
        bad.append(f"internal RAM low {after['low']} < {RAM_FLOOR}")
    if after["fw"] != before["fw"]:
        bad.append(f"firmware changed {after['fw']}")
    return bad


def run(name, fn, touch_ok=False):
    print(f"\n== {name}", flush=True)
    before = snapshot()
    loop_max_ms()                       # start a fresh loop-stats window (after snapshot's /state)
    t0 = time.time()
    try:
        ok, detail = fn()
    except Exception as e:              # a test bug is a failure, not a crash of the suite
        ok, detail = False, f"exception: {e}"
    time.sleep(1.5)
    stall = loop_max_ms()               # before snapshot(): its /state read resets the window
    after = snapshot()
    bad = invariants(before, after, touch_ok)
    if stall > LOOP_STALL_MAX_MS:
        bad.append(f"loop stalled {stall} ms")
    passed = ok and not bad
    RESULTS.append({"test": name, "pass": passed, "detail": detail, "broke": bad,
                    "secs": round(time.time() - t0, 1), "loopMaxMs": stall, "ramLow": after["low"]})
    print(f"   {'PASS' if passed else 'FAIL'}  {detail}" + (f"  | broke: {', '.join(bad)}" if bad else ""),
          flush=True)
    return passed


# ---- voice --------------------------------------------------------------------

def say(text):
    subprocess.run(["say", text], check=False)


def wait_state(state, timeout):
    return wait(lambda: brain().get("state") == state, timeout, 0.15)


def voice_turn(question, start):
    """start(): begins the turn. Returns (ok, detail)."""
    t0 = brain().get("turns", 0)
    if not start():
        return False, "listening never started"
    say(question)
    if not wait(lambda: brain().get("turns", 0) > t0, 40, 0.5):
        b = brain()
        return False, f"no answer (state {b.get('state')}, error '{b.get('lastError')}')"
    b = brain()
    heard, reply = b.get("lastHeard", ""), b.get("lastReply", "")
    wait_state("idle", 40)              # the reply and any follow-up listen finish
    ok = len(heard) > 3 and len(reply) > 0
    return ok, f"heard '{heard[:45]}' -> '{reply[:40]}'"


def start_by_wake(tries=3):
    def start():
        for _ in range(tries):
            say("Hey vizBot")
            if wait_state("listening", 8):
                return True
            time.sleep(3)
        return False
    return start


def start_by_listen():
    req("/brain/listen", "POST")
    return wait_state("listening", 5) is not None


def t_voice_wake():
    return voice_turn("How many legs does a spider have?", start_by_wake())


def t_voice_listen():
    return voice_turn("What is two plus three?", lambda: start_by_listen())


# ---- head ---------------------------------------------------------------------

def motion_ends(since_ms, kind):
    return [e for e in timeline() if e[0] >= since_ms and e[1] == "motionEnd" and e[3] == kind]


def t_gesture(name):
    def fn():
        t = up_ms()
        r = req(f"/bot/head/preset?name={name}")
        if r is None:
            return False, "request failed"
        ends = wait(lambda: motion_ends(t, MK_GESTURE), 6)
        if not ends:
            return False, f"{name}: no motionEnd within 6 s"
        e = ends[0]
        return e[4] == 1, f"{name} finished in {e[0] - t} ms, ok={e[4]}"
    return fn


def t_move():
    req("/bot/head/set_angles?yaw=150&time=600")
    time.sleep(1.2)
    req("/bot/head/recenter")
    time.sleep(1.2)
    drops = [e for e in timeline(60) if e[1] == "motionDropped"]
    return True, f"moved and recentered ({len(drops)} drops in recent timeline)"


def t_reinit():
    t = up_ms()
    r = req("/bot/servo/reinit")
    if r is None:
        return False, "request failed"
    ends = wait(lambda: motion_ends(t, MK_RECOVER), 15)
    if not ends:
        return False, "no recover end within 15 s"
    e = ends[0]
    ref = wait(lambda: [x for x in timeline() if x[0] > e[0] and x[1] == "touchRef"], 15)
    return e[4] == 1 and bool(ref), (f"rail cycle {e[0] - t} ms ok={e[4]}, "
                                      f"touch reference {'re-taken' if ref else 'NOT re-taken'}")


# ---- camera, screen, LEDs, FX, cloud ----------------------------------------------------

def t_photos():
    sizes = []
    for _ in range(3):
        code, body = req("/bot/photo/capture?sound=0", timeout=20, raw=True)
        if code != 200 or not body.startswith(b"\xff\xd8"):
            return False, f"photo failed: {code} {body[:60]!r}"
        sizes.append(len(body))
        time.sleep(1)
    return True, f"3 JPEGs {sizes} B"


def t_screen():
    code, body = req("/debug/screen", timeout=10, raw=True)
    return code == 200 and len(body) == 320 * 240 * 2, f"{code}, {len(body)} B"


def t_leds():
    k0 = (tasks().get("baseLeds") or {})
    mode0 = (req("/bot/base_leds/mode") or {}).get("mode")
    modes = []
    for m in range(0, 12):
        r = req(f"/bot/base_leds/mode?mode={m}")
        if not r or r.get("mode") != m:
            break
        modes.append(m)
        time.sleep(1.5)
    req("/bot/base_leds/set?r=0&g=40&b=80")
    time.sleep(1)
    if mode0 is not None:
        req(f"/bot/base_leds/mode?mode={mode0}")
    k1 = (tasks().get("baseLeds") or {})
    fails = k1.get("fails", 0) - k0.get("fails", 0)
    pushes = k1.get("pushes", 0) - k0.get("pushes", 0)
    return fails == 0 and pushes > 0, f"{len(modes)} modes, {pushes} pushes, {fails} write failures"


def mic_events(since_ms):
    return [e for e in timeline() if e[0] >= since_ms and e[1] in ("micStart", "micStop")]


def t_fx():
    t = up_ms()
    req("/bot/audiofx?v=1", "POST")
    time.sleep(5)
    req("/bot/audiofx?v=0", "POST")
    time.sleep(2)
    ev = mic_events(t)
    return not ev, f"mic edges during FX on/off: {len(ev)} (want 0: the mic stays up)"


def cloud_syncs(since_ms):
    ev = [e for e in timeline() if e[0] >= since_ms and e[1].startswith("netTx") and e[3] == 2]
    return [e for e in ev if e[1] == "netTxEnd"]


def t_cloud():
    t = up_ms()
    req("/cloud/sync")
    done = wait(lambda: cloud_syncs(t), 30)
    return bool(done), (f"sync done {done[0][0] - t} ms after asking" if done else "no sync within 30 s")


# ---- everything at once ------------------------------------------------------------

def t_combo():
    o0 = tasks().get("netOverlaps", 0)
    out = {}

    def turn():
        out["voice"] = voice_turn("What color is the sky on a clear day?", lambda: start_by_listen())

    th = threading.Thread(target=turn)
    th.start()
    time.sleep(0.5)
    req("/cloud/sync")
    req("/bot/head/preset?name=nod")
    code, body = req("/bot/photo/capture?sound=0", timeout=20, raw=True)
    req("/bot/base_leds/mode?mode=2")
    code2, _ = req("/debug/screen", timeout=10, raw=True)
    th.join(90)
    ok_v, det_v = out.get("voice", (False, "turn thread did not finish"))
    o1 = tasks().get("netOverlaps", 0)
    ok = ok_v and code == 200 and code2 == 200 and o1 == o0
    return ok, f"voice: {det_v}; photo {code}; screen {code2}; voice/cloud overlaps {o1 - o0}"


# ---- touch (needs a person) ------------------------------------------------------------

def guided(prompt, key, timeout=12):
    t0 = touch()
    input(f"\n>>> {prompt}, then press Enter here right after... ")
    got = wait(lambda: touch()[key] > t0[key], timeout)
    time.sleep(2)
    t1 = touch()
    extra = {k: t1[k] - t0[k] for k in ("front", "back", "hold") if k != key and t1[k] != t0[k]}
    n = t1[key] - t0[key]
    ok = bool(got) and n == 1 and not extra
    return ok, f"{key} fired x{n}" + (f", also {extra}" if extra else "")


def t_touch_front():
    return guided("Pat the FRONT of his head once", "front")


def t_touch_back():
    return guided("Pat the BACK of his head once", "back")


def t_touch_hold():
    return guided("Hold your hand on his head for 3 seconds", "hold", 15)


def t_hands_off():
    t0 = touch()
    time.sleep(180)
    t1 = touch()
    n = sum(t1[k] - t0[k] for k in ("front", "back", "hold"))
    return n == 0, f"3 min hands off: {n} taps/holds, {t1['contacts'] - t0['contacts']} raw contacts"


# ---- main ------------------------------------------------------------------------

TESTS = [
    ("boot", "Boot: touch reference taken", lambda: (touch()["refs"] >= 1 and not touch()["needRef"],
                                                     f"refs {touch()['refs']}")),
    ("voice", "Voice: wake word turn", t_voice_wake),
    ("voice", "Voice: listen button turn", t_voice_listen),
    ("head", "Head: nod", t_gesture("nod")),
    ("head", "Head: shake", t_gesture("shake")),
    ("head", "Head: move + recenter", t_move),
    ("head", "Head: servo reinit (rail cycle)", t_reinit),
    ("camera", "Camera: 3 photos", t_photos),
    ("screen", "Screen capture", t_screen),
    ("leds", "Base LEDs: every mode + solid colour", t_leds),
    ("fx", "Audio FX on/off", t_fx),
    ("cloud", "Cloud: manual sync", t_cloud),
    ("combo", "All at once: voice + cloud + nod + photo + LEDs + screen", t_combo),
]
TOUCH_TESTS = [
    ("touch", "Touch: front pat", t_touch_front),
    ("touch", "Touch: back pat", t_touch_back),
    ("touch", "Touch: hold", t_touch_hold),
]


def main():
    global HOST
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("--touch", action="store_true", help="add the guided touch tests")
    ap.add_argument("--only", default="", help="comma list: boot,voice,head,camera,screen,leds,fx,cloud,combo,touch,idle")
    ap.add_argument("--no-idle", action="store_true", help="skip the 3-minute hands-off check")
    a = ap.parse_args()
    HOST = a.host
    only = set(filter(None, a.only.split(",")))
    if not req("/state"):
        sys.exit(f"{HOST} does not answer")
    start = snapshot()
    print(f"vizBot {HOST}  fw {start['fw']}  up {start['upSec']} s")
    plan = TESTS + (TOUCH_TESTS if a.touch or "touch" in only else [])
    for group, name, fn in plan:
        if only and group not in only:
            continue
        run(name, fn, touch_ok=(group == "touch"))
    if not a.no_idle and (not only or "idle" in only):
        run("Hands off for 3 min: no phantom taps", t_hands_off)

    end = snapshot()
    n_pass = sum(r["pass"] for r in RESULTS)
    verdict = "PASS" if n_pass == len(RESULTS) else "FAIL"
    stamp = time.strftime("%Y%m%d-%H%M")
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "runs", f"{stamp}-interactions")
    os.makedirs(out, exist_ok=True)
    lines = [f"# Interaction suite: {verdict} ({n_pass}/{len(RESULTS)})", "",
             f"- Bot {HOST}, firmware {end['fw']}, up {end['upSec']} s, run {stamp}",
             f"- Internal RAM low {end['low']} B, refused allocations {end['allocFails']}", "",
             "| Test | Result | Detail | Broke | Loop max |", "|---|---|---|---|---|"]
    for r in RESULTS:
        lines.append(f"| {r['test']} | {'ok' if r['pass'] else '**FAIL**'} | {r['detail']} | "
                     f"{', '.join(r['broke']) or '-'} | {r['loopMaxMs']} ms |")
    open(os.path.join(out, "report.md"), "w").write("\n".join(lines) + "\n")
    open(os.path.join(out, "results.json"), "w").write(json.dumps(RESULTS, indent=1))
    print("\n" + "\n".join(lines))
    print(f"\nReport: {out}/report.md")
    sys.exit(0 if verdict == "PASS" else 1)


if __name__ == "__main__":
    main()
