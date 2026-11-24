#!/usr/bin/env python3
"""vizBot day-to-day health logger (runs on the Mac).

Every 5 minutes launchd runs this once. Each run:

1. Sweeps the Mac's /24 for anything answering on port 80 (about 3 s), and
   asks each one for /state. Whatever answers like a vizBot is a bot, under
   any name, firmware or board: bots come and go, get renamed and reflashed.
2. Identifies each bot by its WiFi MAC address, read from the Mac's ARP table
   (no firmware change needed). The MAC is the bot's id in every log line.
3. Reads each bot's health with plain GETs and appends one JSON line per bot to
   ~/Library/Logs/vizbot-health/health-YYYY-MM-DD.jsonl
   A bot seen in the last 7 days that is not on the network gets an
   "ok": false line (away, not an error).

When a bot's uptime drops or a new crash dump shows up, its /debug/crash and
/debug/tasks (and on a StackChan /debug/powerlog and /debug/power, which has
the reset reason) are saved right away to
  ~/Library/Logs/vizbot-health/reboots/YYYY-MM-DD/<name>-HHMMSS/

  python3 tools/health/healthlog.py --install     start the launchd job
  python3 tools/health/healthlog.py --uninstall   stop it (logs are kept)
  python3 tools/health/healthlog.py --status      job state + bots seen
  python3 tools/health/healthlog.py --once        one run now, by hand

Optional local config (never in the repo, it holds MACs):
  ~/Library/Application Support/vizbot-health/config.json
  {"names": {"aa:bb:cc:dd:ee:ff": "desk pendant"}, "ignore": ["aa:bb:..."]}
See bots.example.json. Weekly grade: tools/health/report.py.

USB serial is never opened. Standard library only, Python 3.9+ (launchd runs
it with /usr/bin/python3).
"""

import argparse
import concurrent.futures as cf
import datetime as dt
import glob
import ipaddress
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import urllib.request

HOME = os.path.expanduser("~")
LOG_DIR = os.environ.get("VIZBOT_HEALTH_DIR") or os.path.join(HOME, "Library", "Logs", "vizbot-health")
APP_DIR = os.path.join(HOME, "Library", "Application Support", "vizbot-health")
CONFIG = os.path.join(APP_DIR, "config.json")
LABEL = "xyz.kpow.vizbot-health"
PLIST = os.path.join(HOME, "Library", "LaunchAgents", LABEL + ".plist")
STATE = os.path.join(LOG_DIR, "state.json")
HERE = os.path.dirname(os.path.abspath(__file__))

INTERVAL_S = 300        # launchd StartInterval
AWAY_DAYS = 7           # log "away" lines for a bot seen this recently
NOT_BOT_S = 3600        # a port-80 host that isn't a vizBot is skipped this long
REBOOT_SLACK_S = 120    # uptime this far below the expected value = a reboot
KEEP_DAYS = 120         # daily logs older than this are deleted


# ---- network -----------------------------------------------------------------

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


def my_subnet(override=None):
    """The /24 the Mac is on (a wider LAN is capped to the Mac's own /24)."""
    if override:
        return ipaddress.ip_network(override, strict=False)
    try:
        r = subprocess.run(["route", "-n", "get", "default"], capture_output=True, text=True, timeout=5)
        iface = re.search(r"interface:\s*(\S+)", r.stdout).group(1)
        ip = subprocess.run(["ipconfig", "getifaddr", iface], capture_output=True, text=True,
                            timeout=5).stdout.strip()
        return ipaddress.ip_network(ip + "/24", strict=False)
    except Exception:
        return None


def port80(ip, timeout=0.5):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((ip, 80))
        return True
    except Exception:
        return False
    finally:
        s.close()


def arp_table():
    """{ip: mac} from the Mac's ARP cache, MACs normalised to aa:bb:cc:dd:ee:ff."""
    out = {}
    try:
        r = subprocess.run(["arp", "-an"], capture_output=True, text=True, timeout=10)
    except Exception:
        return out
    for m in re.finditer(r"\((\d+\.\d+\.\d+\.\d+)\) at ([0-9a-fA-F:]+)", r.stdout):
        parts = m.group(2).split(":")
        if len(parts) == 6:
            out[m.group(1)] = ":".join(p.lower().zfill(2) for p in parts)
    return out


def sweep(net, state, now):
    """[(ip, mac, /state)] for every vizBot on the subnet right now."""
    hosts = [str(h) for h in net.hosts()]
    with cf.ThreadPoolExecutor(64) as ex:
        live = [ip for ip, ok in zip(hosts, ex.map(port80, hosts)) if ok]
    arp = arp_table()    # the connects above filled it
    not_bot = state.setdefault("notBot", {})
    for k in [k for k, t in not_bot.items() if now - t > NOT_BOT_S]:
        del not_bot[k]
    ask = [ip for ip in live if arp.get(ip, ip) not in not_bot]

    def check(ip):
        st = get(ip, "/state", timeout=5)
        return st if isinstance(st, dict) and family(st) else None

    with cf.ThreadPoolExecutor(16) as ex:
        sts = list(ex.map(check, ask))
    found = []
    for ip, st in zip(ask, sts):
        mac = arp.get(ip)
        if st is None:
            if mac:   # by MAC, so a bot that later takes this IP is still asked
                not_bot[mac] = now
            continue
        found.append((ip, mac or "ip-" + ip, st))
    return found


# ---- state -------------------------------------------------------------------

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


# ---- one bot -----------------------------------------------------------------

def family(st):
    """'vizbot' (vizbot/ firmware: StackChan, 1.69, 1.3...), 'vizmini' (vizmini/
    firmware on the C3), or None for anything else."""
    if st.get("firmwareVersion") and "sys" in st:
        return "vizbot"
    if st.get("fw") and st.get("name") and "minHeap" in st:
        return "vizmini"
    return None


def poll_mini(mac, ip, st, prev, now, nick):
    """vizMini serves only /state: no uptime, crash dump or task stats. A reboot
    shows as its least-ever free heap going UP (it can only fall within a boot);
    a reboot whose new low is already below the old one is missed."""
    line = {"t": round(now, 1), "id": mac, "ok": True, "family": "vizmini", "name": st.get("name"), "ip": ip,
            "fw": st.get("fw"), "board": "vizmini-c3",
            "tasks": {"heapMinFree": st.get("minHeap"), "heapFree": st.get("heap")},
            "net": {"rssi": st.get("rssi")}}
    if nick:
        line["nick"] = nick
    lo, plo = st.get("minHeap"), prev.get("heapMinFree")
    if lo is not None and plo is not None and lo > plo:
        gap = now - prev["t"]
        r = {"update": prev.get("fw") != line["fw"], "fwFrom": prev.get("fw"), "boardFrom": prev.get("board"),
             "away": gap > 1.6 * INTERVAL_S, "gapSec": int(gap), "reason": None, "seenBy": "minHeap rose"}
        r["kind"] = reboot_kind(r, False)
        line["reboot"] = r
    new = {"t": now, "fw": line["fw"], "board": "vizmini-c3", "name": line["name"], "ip": ip, "heapMinFree": lo}
    return line, new


CRASH_REASONS = {4: "panic", 5: "int watchdog", 6: "task watchdog", 7: "watchdog", 9: "brownout"}


def capture(label, ip, stackchan, now, why):
    stamp = dt.datetime.fromtimestamp(now)
    safe = re.sub(r"[^A-Za-z0-9_.-]", "_", label)
    d = os.path.join(LOG_DIR, "reboots", stamp.strftime("%Y-%m-%d"), "%s-%s" % (safe, stamp.strftime("%H%M%S")))
    os.makedirs(d, exist_ok=True)
    files = [("crash.json", "/debug/crash"), ("tasks.json", "/debug/tasks")]
    if stackchan:
        files += [("powerlog.csv", "/debug/powerlog"), ("power.json", "/debug/power")]
    got = {}
    for fn, path in files:
        body = fetch(ip, path, timeout=15)
        if body is not None:
            with open(os.path.join(d, fn), "w") as f:
                f.write(body)
            got[fn] = body
    with open(os.path.join(d, "why.txt"), "w") as f:
        f.write(why + "\n")
    reason = None
    if "power.json" in got:
        try:
            reason = json.loads(got["power.json"]).get("resetReason")
        except ValueError:
            pass
    return os.path.relpath(d, LOG_DIR), sorted(got), reason


def reboot_kind(r, new_crash):
    """crash: a panic/watchdog/brownout reset, or a new crash dump with it.
    update: firmware or board changed. power: power-on reset, or the bot was
    away (likely unplugged). restart: a software restart. unknown: rebooted while
    on the network, and the board can't say why (only CoreS3 reports a reason)."""
    reason = r.get("reason")
    if new_crash or reason in CRASH_REASONS:
        return "crash"
    if r["update"]:
        return "update"
    if reason == 1 or (reason is None and r["away"]):
        return "power"
    if reason == 3:
        return "restart"
    return "unknown"


def poll_bot(mac, ip, st, prev, now, nick):
    """One log line for a bot found this run, and its new state."""
    sysd = st.get("sys") or {}
    board = st.get("boardType")
    stackchan = board == "stackchan"
    name = st.get("deviceName") or st.get("hostname") or ip
    line = {"t": round(now, 1), "id": mac, "ok": True, "name": name, "ip": ip, "fw": st.get("firmwareVersion"),
            "board": board, "upSec": sysd.get("upSec")}
    if nick:
        line["nick"] = nick

    k = get(ip, "/debug/tasks")
    if k:
        net = k.get("net") or {}
        line["tasks"] = {"heapMinFree": k.get("heapMinFree"), "heapFree": k.get("heapFree"),
                         "allocFails": k.get("allocFails"), "allocFailsPsram": k.get("allocFailsPsram")}
        if k.get("ramLows"):   # newest low: its uptime + busy flags say when and what
            line["tasks"]["ramLow"] = k["ramLows"][-1]
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

    why = []
    if crash_id and "crashId" in prev and crash_id != prev["crashId"]:
        line["newCrash"] = True
        why.append("new crash dump %s" % crash_id)
    up = line["upSec"]
    reboot = None
    if up is not None and prev.get("upSec") is not None:
        expected = prev["upSec"] + (now - prev["t"])
        if up + REBOOT_SLACK_S < expected:
            gap = now - prev["t"]
            reboot = {"update": prev.get("fw") != line["fw"] or prev.get("board") != board,
                      "fwFrom": prev.get("fw"), "boardFrom": prev.get("board"), "lastUpSec": prev["upSec"],
                      # away = missed at least one run, so it may have been unplugged
                      "away": gap > 1.6 * INTERVAL_S, "gapSec": int(gap)}
            why.append("reboot: uptime %s s, expected ~%d s, last seen %d s ago%s" % (
                up, expected, gap, ", firmware %s -> %s" % (prev.get("fw"), line["fw"]) if reboot["update"] else ""))
    if why:
        line["capture"], line["captured"], reason = capture(nick or name, ip, stackchan, now, "; ".join(why))
        if reboot is not None:
            reboot["reason"] = reason
    if reboot is not None:
        reboot["kind"] = reboot_kind(reboot, line.get("newCrash"))
        line["reboot"] = reboot

    new = {"t": now, "upSec": up, "fw": line["fw"], "board": board, "name": name, "ip": ip}
    if c is not None:
        new["crashId"] = crash_id
    elif "crashId" in prev:   # crash page missed this time: keep the baseline
        new["crashId"] = prev["crashId"]
    return line, new


def poll_all():
    os.makedirs(LOG_DIR, exist_ok=True)
    cfg = load_json(CONFIG, {})
    names, ignore = cfg.get("names", {}), set(cfg.get("ignore", []))
    state = load_json(STATE, {})
    per = state.setdefault("bots", {})
    now = time.time()
    net = my_subnet(cfg.get("subnet"))
    found = sweep(net, state, now) if net else []
    found = [f for f in found if f[1] not in ignore]

    lines = []
    with cf.ThreadPoolExecutor(8) as ex:
        jobs = [ex.submit(poll_mini if family(st) == "vizmini" else poll_bot,
                          mac, ip, st, per.get(mac, {}), now, names.get(mac)) for ip, mac, st in found]
        for (ip, mac, st), j in zip(found, jobs):
            try:
                line, per[mac] = j.result()
            except Exception as e:   # one bad answer must not lose the others
                line = {"t": round(now, 1), "id": mac, "ok": True, "ip": ip, "error": repr(e)[:200]}
            lines.append(line)
    seen = {mac for _, mac, _ in found}
    for mac, p in sorted(per.items()):
        if mac not in seen and mac not in ignore and now - p.get("t", 0) < AWAY_DAYS * 86400:
            line = {"t": round(now, 1), "id": mac, "ok": False, "name": p.get("name")}
            if names.get(mac):
                line["nick"] = names[mac]
            lines.append(line)
    state["lastRun"] = {"t": now, "subnet": str(net), "found": len(found)}

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


# ---- launchd -----------------------------------------------------------------

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
    shutil.copy2(os.path.join(HERE, "healthlog.py"), os.path.join(APP_DIR, "healthlog.py"))
    if not os.path.exists(CONFIG):
        save_json(CONFIG, {"names": {}, "ignore": []})
    st = load_json(STATE, None)
    if st and st.pop("notBot", None) is not None:   # a new version may know more kinds of bot
        save_json(STATE, st)
    with open(PLIST, "w") as f:
        f.write(PLIST_XML.format(label=LABEL, script=os.path.join(APP_DIR, "healthlog.py"),
                                 interval=INTERVAL_S, log=os.path.join(LOG_DIR, "healthlog.log")))
    domain = "gui/%d" % os.getuid()
    launchctl("bootout", "%s/%s" % (domain, LABEL))
    r = launchctl("bootstrap", domain, PLIST)
    if r.returncode != 0:
        sys.exit("launchctl bootstrap failed: %s" % (r.stderr.strip() or r.returncode))
    print("Installed %s: every %d s, logs in %s" % (LABEL, INTERVAL_S, LOG_DIR))
    print("Nicknames and ignores: %s" % CONFIG)


def uninstall():
    launchctl("bootout", "gui/%d/%s" % (os.getuid(), LABEL))
    for p in (PLIST, os.path.join(APP_DIR, "healthlog.py")):
        if os.path.exists(p):
            os.remove(p)
    print("Removed %s. Logs kept in %s, config kept in %s" % (LABEL, LOG_DIR, CONFIG))


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
    st = load_json(STATE, {})
    lr = st.get("lastRun")
    if lr:
        print("Last run %s: swept %s, %d bot(s) on the network" % (
            dt.datetime.fromtimestamp(lr["t"]).strftime("%H:%M"), lr["subnet"], lr["found"]))
    bots = st.get("bots", {})
    if not bots:
        print("No bots seen yet.")
        return
    names = load_json(CONFIG, {}).get("names", {})
    now = time.time()
    print("%-18s %-17s %-16s %-12s %s" % ("bot", "mac", "board", "fw", "last seen"))
    labels = {mac: names.get(mac) or p.get("name") or mac for mac, p in bots.items()}
    dup = {l for l in labels.values() if list(labels.values()).count(l) > 1}
    for mac in labels:   # cloned bots can share a name: tag with the end of the MAC
        if labels[mac] in dup:
            labels[mac] = "%s #%s" % (labels[mac], mac.replace(":", "")[-4:])
    for mac, p in sorted(bots.items(), key=lambda kv: -kv[1].get("t", 0)):
        ago = now - p.get("t", 0)
        when = "now" if ago < 1.6 * INTERVAL_S else ("%.0f min ago" % (ago / 60) if ago < 7200
                                                     else "%.1f h ago" % (ago / 3600))
        print("%-18s %-17s %-16s %-12s %s" % (labels[mac], mac, p.get("board"), p.get("fw"), when))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--once", action="store_true", help="one run now (what launchd runs)")
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
