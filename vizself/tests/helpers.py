"""A tiny vizpow-shaped git repo with two tagged releases, built fresh per test class."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

EYES_V1 = """\
#ifndef BOT_EYES_H
#define BOT_EYES_H

// Blink every few seconds: close, hold, open.
void updateBlink(unsigned long now) {
  if (now > nextBlinkMs) {
    blinkStart(now);
  }
}

void blinkStart(unsigned long now) {
  blinking = true;
}

#endif
"""
CONFIG = '#define FIRMWARE_VERSION "1.1.0"\n#define SERVO_PIN 12\n'
BRAIN = '__version__ = "0.2.0"\n\n\ndef speak(text):\n    """Say text out loud."""\n    return text\n'
SELF_MAP = """\
# vizBot self-map

Source: the v1.0.0 release.

## Overview: what vizBot is

A small desk robot with a face and two servos.

## vizbot/bot_eyes.h

Draws the eyes and makes them blink. `updateBlink()`, `blinkStart()`.

Questions: how do your eyes work? do you ever wink?

## vizbot/config.h

Version and pins. `FIRMWARE_VERSION`, `SERVO_PIN`.
"""
HARDWARE = """\
# vizBot hardware

## Stackchan

Two SCS0009 servos on pin 12.

## Waveshare ESP32-S3-Touch-LCD-1.69 (pendant)

A small round-cornered screen.
"""


def make_repo() -> Path:
    root = Path(tempfile.mkdtemp(prefix="vizself-test-"))

    def git(*args):
        subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)

    def write(path, text):
        (root / path).parent.mkdir(parents=True, exist_ok=True)
        (root / path).write_text(text)

    git("init", "-q", "-b", "main")
    git("config", "user.email", "t@example.com")
    git("config", "user.name", "Test")
    git("remote", "add", "origin", "https://github.com/kpow/vizpow.git")
    write("vizbot/bot_eyes.h", EYES_V1)
    write("vizbot/config.h", CONFIG.replace("1.1.0", "1.0.0"))
    write("vizbrain/vizbrain/__init__.py", BRAIN)
    write("docs/vizself/self-map.md", SELF_MAP)
    write("docs/vizself/hardware.md", HARDWARE)
    write("docs/plan-eyes.md", "# Eyes\n\n## Why blink\n\nBlinking makes him look alive.\n")
    write("poc_wifi/main.ino", "void setup() {}\n")
    git("add", "-A")
    git("commit", "-q", "-m", "feat: eyes that blink")
    git("tag", "v1.0.0")
    write("vizbot/config.h", CONFIG)
    write("vizbot/bot_eyes.h", EYES_V1.replace("close, hold, open", "close fast, open slow"))
    git("commit", "-q", "-am", "chore: version 1.1.0")
    git("tag", "v1.1.0")
    git("tag", "vizbrain-v0.2.0")
    write("vizbot/config.h", CONFIG.replace("1.1.0", "1.2.0-wip"))
    git("commit", "-q", "-am", "wip: not released")
    write("vizbot/config.h", "uncommitted junk")
    return root
