"""Getting vizbrain onto a Mac and keeping it there: first-run setup (a few
questions and the model downloads), the login agent, update and uninstall.

install.sh drives these in order on a new Mac: setup, set-key, install-service.
Each one is safe to run again on its own.
"""

from __future__ import annotations

import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from . import config
from .bot import MIN_FIRMWARE, Bot, BotError, discover_stackchans, firmware_ok
from .speech import KOKORO_DIR

LABEL = "com.kpow.vizbrain"
PLIST_PATH = Path.home() / "Library" / "LaunchAgents" / f"{LABEL}.plist"
LOG_DIR = Path.home() / "Library" / "Logs" / "vizbrain"
# The personal install link this copy came from (install.sh saves it).
LINK_PATH = config.SETTINGS_DIR / "install-link"
FIRMWARE_PAGE = "https://kpow.xyz/vizbot"

# The Kokoro voice (model-files-v1.0 from the kokoro-onnx project): name -> (sha256, bytes).
KOKORO_URL = "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/"
KOKORO_FILES = {
    "kokoro-v1.0.onnx": ("7d5df8ecf7d4b1878015a32686053fd0eebe2bc377234608764cc0ef3636a6c5", 325_532_387),
    "voices-v1.0.bin": ("bca610b8308e8d99f32e6fe4197e7ec01679264efed0cac9140fe9c29f1fbf7d", 28_214_398),
}


# ---- setup: questions ---------------------------------------------------------

def ask(question: str, default: str = "", interactive: bool = True) -> str:
    """One answer from the terminal; Enter (or no terminal) takes the default."""
    if not interactive or not sys.stdin.isatty():
        return default
    try:
        answer = input(f"{question} [{default}] " if default else f"{question} ").strip()
    except EOFError:
        return default
    return answer or default


def geocode(city: str) -> list[dict]:
    """Places matching a city name (Open-Meteo, no key), best match first."""
    url = ("https://geocoding-api.open-meteo.com/v1/search?count=5&language=en&format=json&name="
           + urllib.parse.quote(city))
    with urllib.request.urlopen(url, timeout=8) as r:
        results = json.load(r).get("results") or []
    return [{"city": p["name"], "region": p.get("admin1", ""), "country": p.get("country_code", ""),
             "timezone": p.get("timezone", ""), "lat": round(p["latitude"], 4),
             "lon": round(p["longitude"], 4)} for p in results]


def place_name(loc: dict) -> str:
    return ", ".join(x for x in (loc.get("city"), loc.get("region"), loc.get("country")) if x)


def ask_location(current: dict, interactive: bool) -> dict:
    """The city question. Returns the location to save (unchanged if skipped)."""
    have = place_name(current) if current.get("lat") is not None else ""
    city = ask("What city are you in? (for the weather and local answers)", have, interactive)
    if not city or city == have:
        return current
    try:
        places = geocode(city)
    except (OSError, ValueError) as e:
        print(f"  Couldn't look that up ({e}). Run `vizbrain setup` again later to set it.")
        return current
    if not places:
        print(f"  No place called '{city}' found. Run `vizbrain setup` again to try another spelling.")
        return current
    pick = places[0]
    if len(places) > 1:
        for i, p in enumerate(places, 1):
            print(f"  {i}. {place_name(p)}")
        n = ask("Which one?", "1", interactive)
        if n.isdigit() and 1 <= int(n) <= len(places):
            pick = places[int(n) - 1]
    print(f"  Location: {place_name(pick)}")
    return pick


def check_bot(location: dict) -> bool:
    """Look for the bot, say what was found, and give it the location. True if one is usable."""
    print("Looking for vizBot on this network...")
    bots = discover_stackchans()
    if not bots:
        print("  No vizBot found. Check that he's switched on and on the same Wi-Fi as this Mac\n"
              "  (not a guest network). vizbrain keeps looking once it's running, so you can carry on.")
        return False
    usable = False
    for b in bots:
        if firmware_ok(b["firmware"]):
            print(f"  Found {b['name']} at {b['host']} (firmware {b['firmware']}).")
            usable = True
            if location.get("lat") is not None:
                try:
                    Bot(b["host"]).set_location(location["lat"], location["lon"])
                except BotError as e:
                    print(f"  Couldn't set his location: {e}")
        else:
            need = ".".join(map(str, MIN_FIRMWARE))
            print(f"  Found {b['name']} at {b['host']}, but his firmware is {b['firmware']} and this\n"
                  f"  brain needs {need} or newer. Update him at {FIRMWARE_PAGE}, then switch him\n"
                  f"  fully off and on.")
    return usable


# ---- setup: downloads ---------------------------------------------------------

def download(url: str, dest: Path, sha256: str, size: int = 0) -> None:
    """Fetch url to dest, checked against sha256. Downloads to dest.part and
    renames, so a half-finished file is never mistaken for the real one."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + ".part")
    h = hashlib.sha256()
    done, shown = 0, -1
    with urllib.request.urlopen(url, timeout=30) as r, open(part, "wb") as f:
        total = int(r.headers.get("Content-Length") or size or 0)
        while chunk := r.read(1 << 20):
            f.write(chunk)
            h.update(chunk)
            done += len(chunk)
            pct = done * 100 // total if total else 0
            if pct != shown:
                shown = pct
                print(f"\r  {dest.name}: {pct}% of {total / 1e6:.0f} MB", end="", flush=True)
    print()
    if h.hexdigest() != sha256:
        part.unlink()
        raise RuntimeError(f"{dest.name} arrived damaged (checksum mismatch). Run `vizbrain setup` again.")
    part.replace(dest)


def file_ok(path: Path, sha256: str, size: int) -> bool:
    if not path.exists() or path.stat().st_size != size:
        return False
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest() == sha256


def fetch_voice() -> None:
    for name, (sha, size) in KOKORO_FILES.items():
        if file_ok(KOKORO_DIR / name, sha, size):
            continue
        download(KOKORO_URL + name, KOKORO_DIR / name, sha, size)
    print("  Voice: ready.")


def fetch_ears(model: str) -> None:
    from huggingface_hub import snapshot_download
    try:
        snapshot_download(model, local_files_only=True)
    except Exception:  # noqa: BLE001 - not in the cache yet
        print(f"  Downloading the speech-recognition model (about 480 MB)...")
        snapshot_download(model)
    print("  Ears: ready.")


def setup(interactive: bool = True) -> int:
    """First-run setup: who and where, the bot, and the models. Safe to re-run."""
    settings = config.load()
    print("vizbrain setup\n")
    name = ask("What should vizBot call you?", config.user_name(settings), interactive)
    if name != config.user_name({}):     # only store a name that differs from the account's
        settings["user_name"] = name
    settings["location"] = ask_location(settings.get("location") or {}, interactive)
    config.save(settings)
    print()
    check_bot(settings["location"])
    print("\nGetting his voice and ears (skipped if already here)...")
    try:
        fetch_voice()
        fetch_ears(settings["stt_model"])
    except Exception as e:  # noqa: BLE001 - say it plainly; setup can be run again
        print(f"\nA download failed: {e}\nCheck this Mac's internet connection, then run: vizbrain setup")
        return 1
    if service_loaded():
        restart()                        # settings are read at startup
    print("\nSetup done.")
    return 0


# ---- the login agent ------------------------------------------------------------

def plist_bytes(python: str = sys.executable, log_dir: Path = LOG_DIR) -> bytes:
    """The LaunchAgent that runs `vizbrain serve` at login, for this install
    and this user. It runs the installed interpreter directly, so it doesn't
    depend on PATH."""
    log = str(log_dir / "vizbrain.log")
    return plistlib.dumps({
        "Label": LABEL,
        "ProgramArguments": [python, "-m", "vizbrain", "serve"],
        "EnvironmentVariables": {"PYTHONUNBUFFERED": "1"},
        "RunAtLoad": True,
        # Back up after a crash, but not after a clean exit (port taken, `vizbrain stop`).
        "KeepAlive": {"SuccessfulExit": False},
        "ProcessType": "Interactive",
        "StandardOutPath": log,
        "StandardErrorPath": log,
    })


def _launchctl(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["launchctl", *args], capture_output=True, text=True)


def _target() -> str:
    return f"gui/{os.getuid()}/{LABEL}"


def service_loaded() -> bool:
    return _launchctl("print", _target()).returncode == 0


def health(port: int | None = None, timeout: float = 5.0) -> dict | None:
    port = port or config.load()["port"]
    try:
        with urllib.request.urlopen(f"http://localhost:{port}/v1/health", timeout=timeout) as r:
            return json.load(r)
    except (OSError, ValueError):
        return None


def wait_healthy(seconds: float = 60) -> dict | None:
    end = time.time() + seconds
    while time.time() < end:
        h = health()
        if h:
            return h
        time.sleep(1)
    return None


def install_service() -> int:
    """Run vizbrain at login from now on, and start it now."""
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    PLIST_PATH.parent.mkdir(parents=True, exist_ok=True)
    if service_loaded():
        _launchctl("bootout", _target())
        for _ in range(20):               # bootout returns before the old copy is gone
            if not service_loaded():
                break
            time.sleep(0.25)
    PLIST_PATH.write_bytes(plist_bytes())
    r = _launchctl("bootstrap", f"gui/{os.getuid()}", str(PLIST_PATH))
    if r.returncode != 0:
        print(f"Couldn't start the login agent: {r.stderr.strip() or r.stdout.strip()}")
        return 1
    print("Starting vizbrain...")
    h = wait_healthy()
    if not h:
        print(f"vizbrain didn't come up within a minute. Its log: {LOG_DIR / 'vizbrain.log'}")
        return 1
    print(f"vizbrain is running and will start at login. Claude: {'online' if h.get('claude') else 'no API key yet'}."
          f" Bot: {h.get('bot') or 'not found yet'}.")
    return 0


def restart() -> int:
    if not service_loaded():
        print("vizbrain isn't set up to run at login. Run: vizbrain install-service")
        return 1
    _launchctl("kickstart", "-k", _target())
    print("Restarted vizbrain." if wait_healthy(30) else
          f"Restarted, but it isn't answering yet. Its log: {LOG_DIR / 'vizbrain.log'}")
    return 0


def stop() -> int:
    """Stop vizbrain until the next login (or `vizbrain restart`)."""
    if not service_loaded():
        print("vizbrain isn't running as a login agent.")
        return 0
    _launchctl("bootout", _target())
    print("Stopped. It starts again at next login; to start it now: vizbrain install-service")
    return 0


# ---- update and uninstall ----------------------------------------------------------

def update() -> int:
    """Fetch the installer from this copy's install link and run it in update
    mode (it restarts vizbrain when done). exec, so this Python is gone before
    its environment is replaced."""
    try:
        link = LINK_PATH.read_text().strip()
    except OSError:
        link = ""
    if not link:
        print("This copy has no install link saved, so it can't update itself.\n"
              "Email Kevin for your install link, then paste the line he sends into Terminal.")
        return 1
    try:
        with urllib.request.urlopen(link, timeout=20) as r:
            script = r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        if e.code == 404:
            print("Your install link isn't active any more. Email Kevin for a new one.")
        else:
            print(f"The install site answered with an error ({e.code}). Try again in a few minutes.")
        return 1
    except OSError as e:
        print(f"Couldn't reach the install site ({e}). Check this Mac's internet connection.")
        return 1
    if not script.startswith("#!") or "vizbrain" not in script:
        print("The install link didn't return the installer. Email Kevin for a new link.")
        return 1
    os.execvp("bash", ["bash", "-c", script, "vizbrain-install", "--update"])
    return 1  # not reached


def uninstall(purge: bool = False, assume_yes: bool = False) -> int:
    """Remove the login agent and the program. --purge also removes everything
    vizbrain stored: the API key, settings, memory, logs and downloaded models."""
    leftovers = [config.SETTINGS_DIR, LOG_DIR,
                 Path.home() / ".cache" / "huggingface" / "hub" / "models--mlx-community--whisper-small.en-mlx"]
    if purge and not assume_yes:
        print("This also deletes vizBot's memory and routines, your settings, the API key and the models:")
        for p in leftovers:
            print(f"  {p}")
        if ask("Type yes to delete them:", "no").lower() != "yes":
            print("Nothing was removed.")
            return 1
    if service_loaded():
        _launchctl("bootout", _target())
    PLIST_PATH.unlink(missing_ok=True)
    print("Removed the login agent.")
    if purge:
        subprocess.run(["security", "delete-generic-password", "-s", "vizbrain", "-a", "anthropic"],
                       capture_output=True)
        for p in leftovers:
            shutil.rmtree(p, ignore_errors=True)
        print("Removed the API key, settings, memory, logs and models.")
    else:
        print(f"Kept your settings and vizBot's memory in {config.SETTINGS_DIR}\n"
              f"(remove those too with: vizbrain uninstall --purge)")
    uv = shutil.which("uv") or str(Path.home() / ".local" / "bin" / "uv")
    if "uv/tools" in sys.prefix.replace(os.sep, "/") and os.path.exists(uv):
        print("Removing the vizbrain program...")
        os.execv(uv, [uv, "tool", "uninstall", "vizbrain"])
    print("vizbrain itself was installed by hand (not by the installer), so its files are still here:\n"
          f"  {sys.prefix}")
    return 0
