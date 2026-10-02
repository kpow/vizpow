"""Long-term memory and routines, each a small JSON file next to settings.json.

Memory: short facts vizBot was asked to keep ("Kevin's dog is Biscuit"). They
go into the prompt every turn, so keep them few and short.

Routines: things to do at a time of day ("08:30 weekdays: good morning with
the weather"). The server's scheduler runs each one at most once a day.
"""

from __future__ import annotations

import datetime as dt
import json
import re
import threading

from .config import SETTINGS_DIR

MAX_FACTS = 60
DAY_NAMES = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]


class _JsonList:
    """A JSON list on disk. Reloads when the file changes underneath (a hand
    edit, or another vizbrain process such as the MCP server)."""

    def __init__(self, name: str):
        self.path = SETTINGS_DIR / name
        self._lock = threading.RLock()
        self._mtime = None
        self.items: list[dict] = []
        self._reload()

    def _reload(self) -> None:
        try:
            mtime = self.path.stat().st_mtime
        except OSError:
            return
        if mtime != self._mtime:
            try:
                self.items = json.loads(self.path.read_text())
                self._mtime = mtime
            except (OSError, json.JSONDecodeError):
                pass

    def _save(self) -> None:
        SETTINGS_DIR.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.items, indent=1))
        tmp.replace(self.path)
        self._mtime = self.path.stat().st_mtime


class Memory(_JsonList):
    def __init__(self):
        super().__init__("memory.json")

    def remember(self, fact: str) -> str:
        fact = " ".join(fact.split()).strip().rstrip(".")
        if not fact:
            raise ValueError("nothing to remember")
        with self._lock:
            self._reload()
            if any(f["fact"].lower() == fact.lower() for f in self.items):
                return f"already remembered: {fact}"
            self.items.append({"fact": fact, "added": dt.date.today().isoformat()})
            self.items = self.items[-MAX_FACTS:]
            self._save()
        return f"remembered: {fact}"

    def forget(self, about: str) -> str:
        key = about.lower().strip()
        with self._lock:
            self._reload()
            gone = [f["fact"] for f in self.items if key in f["fact"].lower()]
            self.items = [f for f in self.items if key not in f["fact"].lower()]
            self._save()
        return f"forgot: {'; '.join(gone)}" if gone else f"nothing remembered about '{about}'"

    def prompt_text(self) -> str:
        with self._lock:
            self._reload()
            return "\n".join(f"- {f['fact']}" for f in self.items)


class Routines(_JsonList):
    def __init__(self):
        super().__init__("routines.json")

    @staticmethod
    def _parse_days(days: str) -> str:
        d = days.strip().lower()
        if d in ("daily", "every day", "everyday"):
            return "daily"
        if d in ("weekdays", "weekends"):
            return d
        parts = [p.strip()[:3] for p in re.split(r"[,\s]+", d) if p.strip()]
        if parts and all(p in DAY_NAMES for p in parts):
            return ",".join(parts)
        raise ValueError("days must be daily, weekdays, weekends, or names like mon,wed,fri")

    @staticmethod
    def _parse_time(t: str) -> str:
        m = re.fullmatch(r"\s*(\d{1,2}):(\d{2})\s*(am|pm)?\s*", t.lower())
        if not m:
            raise ValueError("time must look like 08:30 or 5:15 pm")
        h, mi, ap = int(m.group(1)), int(m.group(2)), m.group(3)
        if ap == "pm" and h < 12:
            h += 12
        if ap == "am" and h == 12:
            h = 0
        if not (0 <= h < 24 and 0 <= mi < 60):
            raise ValueError("time out of range")
        return f"{h:02d}:{mi:02d}"

    def add(self, time: str, days: str, what: str) -> str:
        item = {"time": self._parse_time(time), "days": self._parse_days(days),
                "what": " ".join(what.split()), "last_run": ""}
        with self._lock:
            self._reload()
            n = max([int(r["id"][1:]) for r in self.items if r.get("id", "").startswith("r")] + [0]) + 1
            item["id"] = f"r{n}"
            self.items.append(item)
            self._save()
        return f"added routine {item['id']}: {item['days']} at {item['time']}: {item['what']}"

    def remove(self, which: str) -> str:
        key = which.lower().strip()
        with self._lock:
            self._reload()
            gone = [r for r in self.items if r["id"] == key or key in r["what"].lower()]
            self.items = [r for r in self.items if r not in gone]
            self._save()
        return ("removed " + "; ".join(f"{r['id']} ({r['what']})" for r in gone)) if gone \
            else f"no routine matches '{which}'"

    def prompt_text(self) -> str:
        with self._lock:
            self._reload()
            return "\n".join(f"- {r['id']}: {r['days']} at {r['time']}: {r['what']}" for r in self.items)

    def due(self, now: dt.datetime) -> list[dict]:
        """Routines whose time has come today (within 10 minutes) and haven't run today."""
        today, dow = now.date().isoformat(), DAY_NAMES[now.weekday()]
        out = []
        with self._lock:
            self._reload()
            for r in self.items:
                days = r["days"]
                day_ok = (days == "daily" or (days == "weekdays" and now.weekday() < 5)
                          or (days == "weekends" and now.weekday() >= 5) or dow in days.split(","))
                h, m = map(int, r["time"].split(":"))
                at = now.replace(hour=h, minute=m, second=0, microsecond=0)
                if day_ok and r.get("last_run") != today and at <= now < at + dt.timedelta(minutes=10):
                    out.append(dict(r))
        return out

    def mark_run(self, rid: str, day: str) -> None:
        with self._lock:
            self._reload()
            for r in self.items:
                if r["id"] == rid:
                    r["last_run"] = day
            self._save()
