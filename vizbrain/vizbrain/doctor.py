"""`vizbrain doctor`: check everything a working vizBot needs, top to bottom,
and print a report that can be pasted into an email.

Every check runs even when an earlier one fails. The report holds no secrets
and nothing that was said: the key is shown as a length, the home folder as ~,
and the log is only counted.
"""

from __future__ import annotations

import importlib.metadata
import json
import os
import platform
import plistlib
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

from . import __version__, config, install
from .bot import MIN_FIRMWARE, Bot, BotError, discover_stackchans, firmware_ok

OK, WARN, FAIL, SKIP = " OK ", "WARN", "FAIL", "SKIP"
PACKAGES = ("anthropic", "mlx-whisper", "kokoro-onnx", "onnxruntime", "numpy")


def _home(text: object) -> str:
    return str(text).replace(str(Path.home()), "~")


def _ago(seconds: float) -> str:
    seconds = int(seconds)
    if seconds < 120:
        return f"{seconds} s"
    return f"{seconds // 60} min" if seconds < 7200 else f"{seconds // 3600} h"


class Report:
    def __init__(self):
        self.lines: list[str] = []
        self.problems = 0
        self.warnings = 0

    def add(self, status: str, text: str, fix: str = "") -> None:
        self.lines.append(f"[{status}] {_home(text)}")
        if fix:
            self.lines.append(f"       Fix: {_home(fix)}")
        if status == FAIL:
            self.problems += 1
        elif status == WARN:
            self.warnings += 1

    def text(self) -> str:
        bar = "-" * 60
        n, w = self.problems, self.warnings
        if n:
            end = f"{n} problem{'s' if n != 1 else ''} found. Try the Fix line under each one."
        elif w:
            end = f"No problems found. {w} thing{'s' if w != 1 else ''} worth a look (marked WARN)."
        else:
            end = "Everything checks out."
        return "\n".join([bar, f"vizbrain doctor, {time.strftime('%Y-%m-%d %H:%M')}", *self.lines, bar, end,
                          "Still stuck? Copy everything between the lines and email it to Kevin."])


# ---- this Mac -------------------------------------------------------------------

def check_system(r: Report) -> None:
    mac = platform.mac_ver()[0] or "unknown"
    rosetta = subprocess.run(["sysctl", "-n", "sysctl.proc_translated"],
                             capture_output=True, text=True).stdout.strip() == "1"
    if platform.machine() != "arm64" or rosetta:
        return r.add(FAIL, f"vizbrain {__version__}, macOS {mac}, but not running natively on Apple silicon",
                     "vizbrain needs an M1 or newer Mac. If this is one, Terminal is set to open using Rosetta: "
                     "untick that in Terminal's Get Info, then run the install line again.")
    r.add(OK, f"vizbrain {__version__}, macOS {mac}, Apple silicon, Python {platform.python_version()}")


def check_packages(r: Report) -> None:
    missing, have = [], []
    for name in PACKAGES:
        try:
            have.append(f"{name} {importlib.metadata.version(name)}")
        except importlib.metadata.PackageNotFoundError:
            missing.append(name)
    if missing:
        return r.add(FAIL, f"Missing parts: {', '.join(missing)}", "Run your install line again.")
    r.add(OK, "Parts: " + ", ".join(have))


def check_settings(r: Report) -> dict:
    path = config.SETTINGS_PATH
    try:
        changed = json.loads(path.read_text())
        if not isinstance(changed, dict):
            raise ValueError("not a JSON object")
    except FileNotFoundError:
        r.add(OK, "Settings: all defaults")
        return config.load()
    except (OSError, ValueError) as e:
        r.add(FAIL, f"Settings file can't be read ({e}); defaults are being used",
              f"Fix the typo in {path}, or delete the file and run: vizbrain setup")
        return config.load()
    unknown = sorted(k for k in changed if k not in config.DEFAULTS)
    if unknown:
        r.add(WARN, f"Settings: unknown names are ignored: {', '.join(unknown)}")
    r.add(OK, f"Settings: {len(changed)} changed ({', '.join(sorted(changed)) or 'none'})")
    return config.load()


# ---- Claude ------------------------------------------------------------------------

def check_internet(r: Report) -> bool:
    try:
        socket.create_connection(("api.anthropic.com", 443), timeout=5).close()
    except OSError as e:
        r.add(FAIL, f"This Mac can't reach Claude (api.anthropic.com): {e}",
              "Check the Mac's internet connection. If a VPN is on, try with it off.")
        return False
    r.add(OK, "Internet: Claude is reachable")
    return True


def check_key(r: Report, settings: dict, online: bool) -> str | None:
    """Returns the key (never printed) or None."""
    source = "the ANTHROPIC_API_KEY environment variable" if os.environ.get("ANTHROPIC_API_KEY") else "the Keychain"
    key = config.api_key()
    if not key:
        r.add(FAIL, "No Claude API key saved, so vizBot can only do his few built-in replies",
              "Run: vizbrain set-key   (paste the key you were sent)")
        return None
    shown = f"{key[:7]}… ({len(key)} characters, from {source})"
    if not online:
        r.add(SKIP, f"Claude API key {shown}: not tested, no internet")
        return key
    import anthropic
    from .brain import explain, make_client
    try:
        make_client(settings, key).models.retrieve(settings["model"])
    except anthropic.NotFoundError:
        r.add(FAIL, f"Claude accepted the key {shown}, but it can't use the model {settings['model']}",
              "Email Kevin: the key needs access to that model.")
    except anthropic.APIConnectionError as e:
        r.add(SKIP, f"Claude API key {shown}: couldn't finish the test ({type(e).__name__})")
    except Exception as e:  # noqa: BLE001 - whatever it is, report it
        _, state = explain(e)
        if "not scoped to a workspace" in str(e).lower():
            r.add(FAIL, f"The key {shown} is a personal key, and no workspace ID is set for it",
                  "Run: vizbrain set-key   (it asks for the workspace ID, which starts with wrkspc_)")
        elif state == "rejected":
            r.add(FAIL, f"Claude rejected the key {shown}",
                  "Email Kevin for a working key, then run: vizbrain set-key")
        else:
            r.add(FAIL, f"Claude API key {shown}: the test failed ({type(e).__name__}: {str(e)[:80]})")
    else:
        r.add(OK, f"Claude accepted the key {shown}; model {settings['model']}")
    return key


# ---- models -------------------------------------------------------------------------

def check_models(r: Report, settings: dict) -> None:
    try:
        from huggingface_hub import snapshot_download
        snapshot_download(settings["stt_model"], local_files_only=True)
        r.add(OK, "Ears (speech recognition): downloaded")
    except Exception:  # noqa: BLE001 - any reason it isn't usable from the cache
        r.add(FAIL, "Ears (speech recognition): not downloaded, so he can't hear",
              "Run: vizbrain setup   (downloads about 480 MB)")
    if all(install.file_ok(install.KOKORO_DIR / name, sha, size)
           for name, (sha, size) in install.KOKORO_FILES.items()):
        r.add(OK, "Voice: his own voice (Kokoro) is downloaded")
    else:
        r.add(WARN, "Voice: his own voice isn't downloaded; he's using a plain Mac voice",
              "Run: vizbrain setup   (downloads about 350 MB)")


# ---- the running copy ------------------------------------------------------------------

def check_running(r: Report, settings: dict, key: str | None) -> dict | None:
    from .server import port_holder
    port = settings["port"]
    h = install.health(port)
    if h is None:
        holder = port_holder(port)
        if holder and "already running" not in holder:
            r.add(FAIL, holder)
        elif holder:
            r.add(FAIL, "vizbrain is running but not answering",
                  "Run: vizbrain restart")
        else:
            r.add(FAIL, "vizbrain isn't running, so vizBot has no brain",
                  "Run: vizbrain install-service" if not install.service_loaded() else "Run: vizbrain restart")
        return None
    version = h.get("version")
    if version is None:
        r.add(WARN, "An older vizbrain is running (it doesn't report its version)", "Run: vizbrain restart")
        return h
    up = f"up {_ago(h.get('uptime_s', 0))}"
    if version != __version__:
        r.add(WARN, f"vizbrain {version} is running, but {__version__} is installed ({up})", "Run: vizbrain restart")
    else:
        r.add(OK, f"vizbrain {version} is running ({up})")
    state = h.get("key")
    if state == "none" and key:
        r.add(FAIL, "The running vizbrain started before the key was saved", "Run: vizbrain restart")
    elif state == "rejected":
        r.add(FAIL, "The running vizbrain had its key rejected by Claude",
              "Email Kevin for a working key, then run: vizbrain set-key")
    elif state == "no_credit":
        r.add(FAIL, "Claude says the account is out of credit", "Email Kevin: the account needs topping up.")
    ears = h.get("ears")
    if ears == "loading":
        r.add(WARN, "The running vizbrain is still loading his ears; give it a few minutes")
    elif ears == "failed":
        r.add(FAIL, "The running vizbrain couldn't load his ears", "Run: vizbrain setup   then: vizbrain restart")
    if h.get("capped"):
        r.add(WARN, f"He has reached the spending limit for the {h['capped']} and won't answer until it resets")
    err = h.get("last_error")
    if err:
        r.add(WARN, f"Last Claude error, {_ago(time.time() - err['at'])} ago: {err['what']}")
    return h


def check_agent(r: Report) -> None:
    if not install.PLIST_PATH.exists():
        return r.add(WARN, "Not set to start at login", "Run: vizbrain install-service")
    try:
        program = plistlib.loads(install.PLIST_PATH.read_bytes())["ProgramArguments"][0]
    except Exception:  # noqa: BLE001
        return r.add(FAIL, "The login agent file is damaged", "Run: vizbrain install-service")
    if not os.path.exists(program):
        return r.add(FAIL, f"The login agent points at a program that's gone ({program})",
                     "Run: vizbrain install-service")
    agent = plistlib.loads(install.PLIST_PATH.read_bytes())
    if (install.BUNDLE_ID not in agent.get("AssociatedBundleIdentifiers", [])
            or not (install.APP_PATH / "Contents" / "Info.plist").exists()):
        r.add(WARN, "The login agent isn't linked to vizbrain.app, so macOS may block it from the local network",
              "Run: vizbrain install-service")
    out = install._launchctl("print", install._target())
    if out.returncode != 0:
        return r.add(WARN, "Set to start at login, but not loaded right now", "Run: vizbrain install-service")
    runs = re.search(r"runs = (\d+)", out.stdout)
    code = re.search(r"last exit code = (\S+)", out.stdout)
    runs = int(runs.group(1)) if runs else 0
    note = f"{runs} start{'s' if runs != 1 else ''} since login"
    if runs > 5:
        r.add(WARN, f"Starts at login, but it has restarted {runs} times (last exit code "
                    f"{code.group(1) if code else '?'}); something is crashing it",
              f"Email Kevin this report and the file {install.LOG_DIR / 'vizbrain.log'}")
    else:
        r.add(OK, f"Starts at login ({note})")


def check_mac(r: Report) -> str | None:
    """This Mac's address on the LAN, and whether it will fall asleep."""
    ip = None
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("192.0.2.1", 9))          # no packet is sent; this only picks the interface
            ip = s.getsockname()[0]
    except OSError:
        pass
    # e.g. " sleep                1 (sleep prevented by coreaudiod, powerd)"
    pm = subprocess.run(["pmset", "-g"], capture_output=True, text=True).stdout
    sleep = re.search(r"^\s*sleep\s+(\d+)(.*)$", pm, re.M)
    if ip is None:
        r.add(FAIL, "This Mac isn't on a network", "Join the same Wi-Fi as vizBot.")
    elif sleep and int(sleep.group(1)) > 0 and "prevented" not in sleep.group(2):
        r.add(WARN, f"This Mac is {ip} and is set to sleep after {sleep.group(1)} min idle; "
                    "vizBot can't think while the Mac is asleep",
              "Keep the Mac awake while you want him to answer (plugged in, with sleep turned off "
              "in System Settings).")
    else:
        r.add(OK, f"This Mac is {ip} on the network"
                  + (" (set to sleep when idle, but something is keeping it awake right now)"
                     if sleep and int(sleep.group(1)) > 0 else " and stays awake"))
    return ip


# ---- the bot ----------------------------------------------------------------------------

def check_bot(r: Report, settings: dict, h: dict | None, my_ip: str | None) -> None:
    host = settings["bot_host"] or (h or {}).get("bot")
    name = host
    if not host:
        bots = discover_stackchans()
        if bots:
            host, name = bots[0]["host"], f"{bots[0]['name']} at {bots[0]['host']}"
    if not host:
        return r.add(FAIL, "No vizBot found on this network",
                     "Check he's switched on and on the same Wi-Fi as this Mac (not a guest network). "
                     "If he is, put his address in settings.json as \"bot_host\".")
    bot = Bot(host)
    try:
        state = bot.state()
        status = bot._get("/brain/status")
    except BotError as e:
        return r.add(FAIL, f"vizBot was at {host} but isn't answering ({str(e)[:70]})",
                     "Check he's switched on. If he just moved networks, run: vizbrain restart")
    fw = state.get("firmwareVersion")
    name = f"{state.get('deviceName') or 'vizBot'} at {host}"
    if state.get("boardType") != "stackchan" or not isinstance(status, dict):
        return r.add(FAIL, f"Found {name}, but this firmware has no voice (board: {state.get('boardType')})",
                     f"Flash the Stackchan build from {install.FIRMWARE_PAGE}")
    if not firmware_ok(fw):
        need = ".".join(map(str, MIN_FIRMWARE))
        r.add(FAIL, f"Found {name}, but his firmware is {fw} and this brain needs {need} or newer",
              f"Update him at {install.FIRMWARE_PAGE}, then switch him fully off and on.")
    else:
        r.add(OK, f"Found {name}, firmware {fw}")
    if not status.get("enabled", True):
        r.add(FAIL, "Voice is switched off on the bot", f"Open http://{host}/ and turn on Brain (voice).")
    port = settings["port"]
    want = f"{my_ip}:{port}" if my_ip else None
    points_at = status.get("brainHost") or status.get("resolved") or "nothing yet"
    if want and points_at != want:
        r.add(FAIL if h else WARN, f"vizBot is looking for his brain at {points_at}, but this Mac is {want}",
              "Run: vizbrain restart   (it tells him the new address within a minute)")
    elif want:
        r.add(OK, f"vizBot is pointed at this Mac ({want}): {status.get('turns', 0)} turns, "
                  f"{status.get('failures', 0)} failed since he started")
    try:
        pair_id = json.loads(config.PAIRING_PATH.read_text())["token"].split(".")[0]
    except (OSError, ValueError, KeyError, AttributeError):
        pair_id = None
    if "pairId" in status and pair_id and status["pairId"] != pair_id:
        r.add(WARN, "vizBot isn't paired with this brain yet (it pairs within a minute of vizbrain starting)",
              "If this stays, run: vizbrain restart")
    elif "pairId" in status and pair_id:
        r.add(OK, "vizBot is paired with this brain")
    if status.get("lastError") and status["lastError"] != "no speech detected":   # that one is just a missed tap
        r.add(WARN, f"vizBot's last error: {status['lastError']}")
    # The running copy talks to the bot once a minute. Terminal reaching the
    # bot when the background copy can't is what a Local Network denial looks like.
    if h and h.get("version"):
        seen = h.get("bot_seen_s_ago")
        if seen is not None and seen < 180:
            r.add(OK, f"The running vizbrain reached vizBot {_ago(seen)} ago")
        elif h.get("uptime_s", 0) > 120:
            r.add(FAIL, "Terminal can reach vizBot, but the running vizbrain can't"
                        + (f" ({h['bot_error']})" if h.get("bot_error") else ""),
                  "System Settings > Privacy & Security > Local Network: turn on vizbrain, then run: "
                  "vizbrain restart. No vizbrain entry there? Run: vizbrain install-service")


def check_log(r: Report) -> None:
    log = install.LOG_DIR / "vizbrain.log"
    try:
        size = log.stat().st_size
        with open(log, "rb") as f:
            f.seek(max(0, size - 400_000))
            tail = f.read().decode("utf-8", "replace")
    except OSError:
        return r.add(OK, "Log: none yet")
    crashes = tail.count("Traceback (most recent call last)")
    if crashes:
        r.add(WARN, f"Log: {crashes} error trace{'s' if crashes != 1 else ''} recently "
                    f"({log}, {size / 1e6:.1f} MB)",
              f"If he's misbehaving, attach that file when you email Kevin.")
    else:
        r.add(OK, f"Log: no errors recently ({size / 1e6:.1f} MB)")


def run(copy: bool = False) -> int:
    r = Report()

    def step(fn, *args):
        try:
            return fn(r, *args)
        except Exception as e:  # noqa: BLE001 - one broken check must not hide the rest
            r.add(WARN, f"A check couldn't run ({fn.__name__}: {type(e).__name__}: {str(e)[:80]})")
            return None

    step(check_system)
    step(check_packages)
    settings = step(check_settings) or config.load()
    online = bool(step(check_internet))
    key = step(check_key, settings, online)
    step(check_models, settings)
    h = step(check_running, settings, key)
    step(check_agent)
    my_ip = step(check_mac)
    step(check_bot, settings, h, my_ip)
    step(check_log)
    text = r.text()
    print(text)
    if copy:
        subprocess.run(["pbcopy"], input=text, text=True)
        print("(Copied to the clipboard.)")
    return 1 if r.problems else 0
