#!/usr/bin/env python3
"""vizBot day-to-day health logger (runs on the Mac).

Every 5 minutes launchd runs this once. It reads each bot's health over WiFi
with plain GETs and appends one JSON line per bot to

  ~/Library/Logs/vizbot-health/health-YYYY-MM-DD.jsonl

Nothing on a bot is changed, and USB serial is never opened. A bot that does
not answer is logged as offline ("ok": false), not as an error.

When a bot's uptime drops (it rebooted) or a new crash dump shows up, its
/debug/crash (and on a StackChan /debug/powerlog and /debug/power) is saved
right away to

  ~/Library/Logs/vizbot-health/reboots/YYYY-MM-DD/<bot>-HHMMSS/

  python3 tools/health/healthlog.py --install     start the launchd job
  python3 tools/health/healthlog.py --uninstall   stop it (logs are kept)
  python3 tools/health/healthlog.py --status      job state + latest line per bot
  python3 tools/health/healthlog.py --once        one poll now, by hand

Bots come from bots.json (hostnames only) plus any vizbot-*.local that mDNS
finds. The weekly grade is tools/health/report.py.

Standard library only, Python 3.9+ (launchd runs it with /usr/bin/python3).
"""

import argparse
import datetime as dt
import glob
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.request

HOME = os.path.expanduser("~")
LOG_DIR = os.environ.get("VIZBOT_HEALTH_DIR") or os.path.join(HOME, "Library", "Logs", "vizbot-health")
APP_DIR = os.path.join(HOME, "Library", "Application Support", "vizbot-health")
LABEL = "xyz.kpow.vizbot-health"
PLIST = os.path.join(HOME, "Library", "LaunchAgents", LABEL + ".plist")
STATE = os.path.join(LOG_DIR, "state.json")
HERE = os.path.dirname(os.path.abspath(__file__))

INTERVAL_S = 300        # launchd StartInterval
DISCOVER_S = 3600       # mDNS browse at most once an hour
REBOOT_SLACK_S = 120    # uptime this far below the expected value = a reboot
KEEP_DAYS = 120         # daily logs older than this are deleted


# ---- HTTP + names -----------------------------------------------------------

def fetch(ip, path, timeout=6):
    try:
        with urllib.request.urlopen("http://%s%s" % (ip, path), timeout=timeout) as r:
            if r.status != 200:
                return None
            return r.read().decode("utf-8", "replace")
    except Exception:
        return None


def get(ip, path, timeout=6):
    body = fetch(ip, path, timeout)
    if body is None:
        return None
    try:
        return json.loads(body)
    except ValueError:
        return None


def resolve(host, timeout=5.0):
    """IPv4 address of a .local name, or None. getaddrinfo has no timeout of its
    own, so it runs in a thread that is left behind if mDNS never answers."""
    out = []

    def run():
        try:
            out.append(socket.getaddrinfo(host, 80, socket.AF_INET, socket.SOCK_STREAM)[0][4][0])
        except Exception:
            pass

    t = threading.Thread(target=run, daemon=True)
    t.start()
    t.join(timeout)
    return out[0] if out else None


def discover(prefix, seconds=3.0):
    """Instance names starting with prefix seen by a short mDNS browse."""
    try:
        p = subprocess.Popen(["dns-sd", "-B", "_http._tcp", "local."],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    except OSError:
        return []
    time.sleep(seconds)
    p.terminate()
    try:
        out, _ = p.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
    names = set()
    for line in out.splitlines():
        parts = line.split(None, 6)
        if len(parts) == 7 and parts[1] == "Add" and parts[6].strip().lower().startswith(prefix):
            names.add(parts[6].strip().lower())
    return sorted(names)


# ---- state ------------------------------------------------------------------

def load_json(path, default):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def save_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(obj, f, indent=1, sort_keys=True)
    os.replace(tmp, path)


def bot_list(cfg, state, now):
    bots = {b["name"]: b["host"] for b in cfg.get("bots", [])}
    prefix = cfg.get("discover")
    if prefix:
        if now - state.get("discoveredAt", 0) >= DISCOVER_S:
            found = discover(prefix)
            if found:   # an empty browse (network blip) keeps the last list
                state["discovered"] = found
            state["discoveredAt"] = now
        for inst in state.get("discovered", []):
            host = inst + ".local"
            if host not in bots.values():
                bots[inst[len(prefix):] or inst] = host
    return bots


# ---- one bot ----------------------------------------------------------------

def find_bot(host, cached_ip):
    """(ip, /state) for host. Tries the cached IP first and checks the bot there
    is the right one (DHCP can hand the address to someone else)."""
    if cached_ip:
        st = get(cached_ip, "/state")
        if st and str(st.get("hostname", "")).lower() == host.lower():
            return cached_ip, st
    ip = resolve(host)
    if ip and ip != cached_ip:
        st = get(ip, "/state")
        if st:
            return ip, st
    return cached_ip, None


def capture(name, ip, stackchan, now, why):
    stamp = dt.datetime.fromtimestamp(now)
    d = os.path.join(LOG_DIR, "reboots", stamp.strftime("%Y-%m-%d"), "%s-%s" % (name, stamp.strftime("%H%M%S")))
    os.makedirs(d, exist_ok=True)
    files = [("crash.json", "/debug/crash"), ("tasks.json", "/debug/tasks")]
    if stackchan:
        files += [("powerlog.csv", "/debug/powerlog"), ("power.json", "/debug/power")]
    got = []
    for fn, path in files:
        body = fetch(ip, path, timeout=15)
        if body is not None:
            with open(os.path.join(d, fn), "w") as f:
                f.write(body)
            got.append(fn)
    with open(os.path.join(d, "why.txt"), "w") as f:
        f.write(why + "\n")
    return os.path.relpath(d, LOG_DIR), got


def poll_bot(name, host, prev, now):
    """One log line for a bot, and its new state."""
    line = {"t": round(now, 1), "bot": name, "host": host}
    ip, st = find_bot(host, prev.get("ip"))
    if not st:
        line["ok"] = False
        return line, prev
    sysd = st.get("sys") or {}
    board = st.get("boardType")
    stackchan = board == "stackchan"
    line.update({"ok": True, "ip": ip, "fw": st.get("firmwareVersion"), "board": board,
                 "upSec": sysd.get("upSec")})

    k = get(ip, "/debug/tasks")
    if k:
        net = k.get("net") or {}
        line["tasks"] = {"heapMinFree": k.get("heapMinFree"), "heapFree": k.get("heapFree"),
                         "allocFails": k.get("allocFails"), "allocFailsPsram": k.get("allocFailsPsram")}
        line["net"] = {"dead": net.get("dead"), "deadEver": net.get("deadEver"),
                       "deadRestarts": net.get("deadRestarts"), "rssi": net.get("rssi"),
                       "lastDead": net.get("lastDead")}
    if stackchan:
        t = get(ip, "/debug/touchraw", timeout=10)
        if t:
            f = t.get("fired") or {}
            line["touch"] = {"contacts": t.get("contactCount"), "ignored": t.get("ignoredContacts"),
                             "heals": t.get("heals"), "front": f.get("front"), "back": f.get("back"),
                             "hold": f.get("hold"), "rejected": f.get("rejected")}
    c = get(ip, "/debug/crash")
    crash_id = None
    if c is not None:
        line["crash"] = {"haveDump": c.get("haveDump"), "pc": c.get("pc"), "elfSha": c.get("elfSha"),
                         "task": c.get("task"), "reason": (c.get("reason") or "")[:160] or None}
        if c.get("haveDump"):
            crash_id = "%s/%s" % (c.get("pc"), c.get("elfSha"))

    # Reboot: uptime well below what it would be had the bot kept running.
    why = []
    up = line["upSec"]
    if up is not None and prev.get("upSec") is not None:
        expected = prev["upSec"] + (now - prev["t"])
        if up + REBOOT_SLACK_S < expected:
            update = bool(prev.get("fw")) and prev.get("fw") != line["fw"]
            line["reboot"] = {"update": update, "fwFrom": prev.get("fw"), "lastUpSec": prev["upSec"]}
            why.append("reboot (uptime %s s, expected ~%d s)%s" % (up, expected,
                       ", firmware %s -> %s" % (prev.get("fw"), line["fw"]) if update else ""))
    if crash_id and "crashId" in prev and crash_id != prev["crashId"]:
        line["newCrash"] = True
        why.append("new crash dump %s" % crash_id)
    if why:
        line["capture"], line["captured"] = capture(name, ip, stackchan, now, "; ".join(why))

    new = {"ip": ip, "t": now, "upSec": up, "fw": line["fw"]}
    if c is not None:
        new["crashId"] = crash_id
    elif "crashId" in prev:   # crash page missed this time: keep the baseline
        new["crashId"] = prev["crashId"]
    return line, new


def poll_all():
    os.makedirs(LOG_DIR, exist_ok=True)
    cfg = load_json(os.path.join(HERE, "bots.json"), {})
    state = load_json(STATE, {})
    now = time.time()
    bots = bot_list(cfg, state, now)
    per = state.setdefault("bots", {})
    lines = []
    for name, host in sorted(bots.items()):
        line, per[name] = poll_bot(name, host, per.get(name, {}), now)
        lines.append(line)
    day = dt.datetime.fromtimestamp(now).strftime("%Y-%m-%d")
    with open(os.path.join(LOG_DIR, "health-%s.jsonl" % day), "a") as f:
        for line in lines:
            f.write(json.dumps(line, sort_keys=True) + "\n")
    save_json(STATE, state)
    prune(now)
    return lines


def prune(now):
    cutoff = dt.datetime.fromtimestamp(now - KEEP_DAYS * 86400).strftime("%Y-%m-%d")
    for p in glob.glob(os.path.join(LOG_DIR, "health-*.jsonl")):
        if os.path.basename(p)[7:17] < cutoff:
            os.remove(p)


# ---- launchd ----------------------------------------------------------------

PLIST_XML = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>{label}</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/bin/python3</string>
    <string>{script}</string>
    <string>--once</string>
  </array>
  <key>StartInterval</key><integer>{interval}</integer>
  <key>RunAtLoad</key><true/>
  <key>ProcessType</key><string>Background</string>
  <key>LowPriorityIO</key><true/>
  <key>Nice</key><integer>10</integer>
  <key>StandardOutPath</key><string>{log}</string>
  <key>StandardErrorPath</key><string>{log}</string>
</dict>
</plist>
"""


def launchctl(*args):
    return subprocess.run(["launchctl"] + list(args), capture_output=True, text=True)


def install():
    """Copy the logger out of the repo (a worktree can vanish) and load the job."""
    os.makedirs(APP_DIR, exist_ok=True)
    os.makedirs(LOG_DIR, exist_ok=True)
    os.makedirs(os.path.dirname(PLIST), exist_ok=True)
    for fn in ("healthlog.py", "bots.json"):
        shutil.copy2(os.path.join(HERE, fn), os.path.join(APP_DIR, fn))
    with open(PLIST, "w") as f:
        f.write(PLIST_XML.format(label=LABEL, script=os.path.join(APP_DIR, "healthlog.py"),
                                 interval=INTERVAL_S, log=os.path.join(LOG_DIR, "healthlog.log")))
    domain = "gui/%d" % os.getuid()
    launchctl("bootout", "%s/%s" % (domain, LABEL))
    r = launchctl("bootstrap", domain, PLIST)
    if r.returncode != 0:
        sys.exit("launchctl bootstrap failed: %s" % (r.stderr.strip() or r.returncode))
    print("Installed %s: every %d s, logs in %s" % (LABEL, INTERVAL_S, LOG_DIR))


def uninstall():
    launchctl("bootout", "gui/%d/%s" % (os.getuid(), LABEL))
    for p in (PLIST,):
        if os.path.exists(p):
            os.remove(p)
    shutil.rmtree(APP_DIR, ignore_errors=True)
    print("Removed %s. Logs kept in %s" % (LABEL, LOG_DIR))


def status():
    r = launchctl("print", "gui/%d/%s" % (os.getuid(), LABEL))
    if r.returncode != 0:
        print("Job not loaded.")
    else:
        seen = {}
        for l in r.stdout.splitlines():   # first of each: nested sections repeat "state ="
            for k in ("state =", "runs =", "last exit code ="):
                if l.strip().startswith(k) and k not in seen:
                    seen[k] = l.strip()
        print("Job loaded: " + ", ".join(seen.values()))
    files = sorted(glob.glob(os.path.join(LOG_DIR, "health-*.jsonl")))
    if not files:
        print("No log lines yet.")
        return
    latest = {}
    with open(files[-1]) as f:
        for l in f:
            if l.strip():
                d = json.loads(l)
                latest[d["bot"]] = d
    for name, d in sorted(latest.items()):
        when = dt.datetime.fromtimestamp(d["t"]).strftime("%H:%M")
        if not d.get("ok"):
            print("%-6s %s  offline" % (name, when))
        else:
            print("%-6s %s  fw %s  up %.1f h  RAM low %s" % (name, when, d.get("fw"), (d.get("upSec") or 0) / 3600,
                                                           (d.get("tasks") or {}).get("heapMinFree")))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--once", action="store_true", help="poll every bot once (what launchd runs)")
    g.add_argument("--install", action="store_true")
    g.add_argument("--uninstall", action="store_true")
    g.add_argument("--status", action="store_true")
    a = ap.parse_args()
    if a.install:
        install()
    elif a.uninstall:
        uninstall()
    elif a.status:
        status()
    elif a.once:
        lines = poll_all()
        if sys.stdout.isatty():   # by hand; under launchd stdout is the error log
            for line in lines:
                print(json.dumps(line, sort_keys=True))
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
