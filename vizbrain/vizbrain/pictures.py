"""Pictures for the bot's screen: album covers a music connector points at.

A connector result that says "Cover: https://..." (spotify-mcp's now_playing
and play do) gets that cover shown on the bot for a few seconds, without a
second model call. Only Spotify's image hosts are fetched. The Mac's own
`sips` turns the cover into the small baseline JPEG the bot can decode.
"""

from __future__ import annotations

import re
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request
from pathlib import Path

SIZE = 240                      # the bot draws it 240x240, centred on its 320x240 screen
SHOW_MS = 8000
MAX_DOWNLOAD = 2 * 1024 * 1024
MAX_FOR_BOT = 90 * 1024         # the bot takes up to 96 KB
COVER = re.compile(r"\bCover: (https://\S+?)(?=[\s)\]]|$)")
HOSTS = re.compile(r"^(i\.scdn\.co|mosaic\.scdn\.co|image-cdn-[a-z0-9-]+\.spotifycdn\.com|[a-z0-9-]+\.spotifycdn\.com)$")


def cover_url(text: str) -> str | None:
    """The cover a tool result points at, if it is one we'll fetch."""
    m = COVER.search(text or "")
    if not m:
        return None
    url = m.group(1).rstrip(".,;")
    host = urllib.parse.urlparse(url).hostname or ""
    return url if HOSTS.match(host) else None


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "vizbrain"})
    with urllib.request.urlopen(req, timeout=5) as r:
        data = r.read(MAX_DOWNLOAD + 1)
    if len(data) > MAX_DOWNLOAD:
        raise ValueError("picture too big")
    return data


def for_bot(image: bytes) -> bytes:
    """Any image macOS can read, as a 240x240 baseline JPEG small enough for the bot."""
    with tempfile.TemporaryDirectory() as tmp:
        src, out = Path(tmp) / "in", Path(tmp) / "out.jpg"
        src.write_bytes(image)
        for quality in (80, 60, 40):
            subprocess.run(["sips", "-s", "format", "jpeg", "-s", "formatOptions", str(quality),
                            "-z", str(SIZE), str(SIZE), str(src), "--out", str(out)],
                           capture_output=True, timeout=10, check=True)
            jpeg = out.read_bytes()
            if len(jpeg) <= MAX_FOR_BOT:
                return jpeg
    raise ValueError("picture still too big for the bot")


class Shower:
    """Shows covers on the bot in the background, skipping one it just showed."""

    def __init__(self, get_bot, firmware, repeat_s: float = 30.0):
        self.get_bot = get_bot          # () -> Bot | None
        self.firmware = firmware        # () -> the bot's firmware version, or None
        self.repeat_s = repeat_s
        self._last = ("", 0.0)

    def supported(self) -> bool:
        from .bot import ART_FIRMWARE, firmware_ok
        return firmware_ok(self.firmware(), ART_FIRMWARE)

    def show(self, url: str) -> None:
        last_url, at = self._last
        if url == last_url and time.time() - at < self.repeat_s:
            return
        bot = self.get_bot()
        if bot is None or not self.supported():
            return
        self._last = (url, time.time())
        try:
            bot.art(for_bot(fetch(url)), SHOW_MS)
        except Exception as e:  # noqa: BLE001 - a missing cover never breaks a turn
            print(f"[art] not shown: {e}")
