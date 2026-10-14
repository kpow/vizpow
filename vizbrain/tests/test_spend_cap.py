"""Spend caps (usage.over_cap) and what the brain does once one is reached.
Run: python -m unittest discover -s tests"""

import datetime as dt
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from vizbrain import brain, usage

CAPS = {"daily_cap_usd": 2.0, "monthly_cap_usd": 20.0}


class NoCalls:
    """A stand-in Anthropic client that fails the test if it is ever used."""

    def __getattr__(self, name):
        raise AssertionError(f"Claude was called ({name}) while over the cap")


class SpendCap(unittest.TestCase):
    def setUp(self):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        self.path = Path(d.name) / "usage.json"
        for name, value in (("PATH", self.path), ("SETTINGS_DIR", Path(d.name))):
            p = mock.patch.object(usage, name, value)
            p.start()
            self.addCleanup(p.stop)

    def spent(self, today: float, earlier_this_month: float = 0.0):
        day = dt.date.today()
        data = {day.isoformat(): {"usd": today, "calls": 1}}
        if earlier_this_month:
            data[day.strftime("%Y-%m-00")] = {"usd": earlier_this_month, "calls": 1}
        self.path.write_text(json.dumps(data))

    def test_under_both_caps(self):
        self.assertIsNone(usage.over_cap(CAPS))        # no usage file yet
        self.spent(1.99, 10)
        self.assertIsNone(usage.over_cap(CAPS))

    def test_day_then_month(self):
        self.spent(2.0)
        self.assertEqual(usage.over_cap(CAPS), "day")
        self.spent(0.5, 19.5)
        self.assertEqual(usage.over_cap(CAPS), "month")

    def test_zero_turns_a_cap_off(self):
        self.spent(50)
        self.assertIsNone(usage.over_cap({"daily_cap_usd": 0, "monthly_cap_usd": 0}))
        self.assertIsNone(usage.over_cap({}))

    def test_record_adds_up_and_leaves_no_temp_file(self):
        tokens = SimpleNamespace(input_tokens=1_000_000, output_tokens=0)
        usage.record("claude-haiku-4-5", tokens)
        usage.record("claude-haiku-4-5", tokens)
        self.assertEqual(usage.summary()["today_usd"], 2.0)
        self.assertEqual(usage.over_cap(CAPS), "day")
        self.assertEqual([p.name for p in self.path.parent.iterdir()], ["usage.json"])

    def make_brain(self, client):
        b = brain.Brain({**CAPS, "session_idle_s": 300}, toolbox=None, api_key=None)
        b.client = client
        return b

    def test_capped_turn_says_so_without_calling_claude(self):
        self.spent(2.5)
        b = self.make_brain(NoCalls())
        result = b.turn("what's the weather")
        self.assertEqual(result["text"], brain.CAP_TEXT["day"])
        self.assertEqual(result["tools"], ["capped (day)"])
        self.assertEqual(b.history, [])

    def test_capped_event_is_silent(self):
        self.spent(2.5)
        self.assertEqual(self.make_brain(NoCalls()).quick("[event]", images=[b"jpeg"])["text"], "")

    def test_no_key_means_no_cap(self):
        self.spent(2.5)
        self.assertIsNone(self.make_brain(None).capped())


if __name__ == "__main__":
    unittest.main()
