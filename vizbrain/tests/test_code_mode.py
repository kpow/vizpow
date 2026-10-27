"""Code mode: a code-mode connector (vizself) gets its own look-up budget, an
opening line instead of the filler, quips into the live clip with a beat
signal to the bot, then "found it" and the answer (or a shrug).
Run: python -m unittest discover -s tests"""

import json
import random
import tempfile
import threading
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from vizbrain import bot, brain, codemode, config, speech, tools

from fakes import BrainTest, FakeClaude, make_brain, reply, text, tool_use
from test_personal import fake_vizmac, fake_wled


class Hub:
    """vizself (code mode, its own budget) and kpow (the shared budget)."""

    CONF = {"vizself": {"name": "vizself", "code_mode": True, "budget": {"max_lookups": 10, "lookup_budget_s": 60.0}},
            "kpow": {"name": "kpow", "code_mode": False, "budget": None}}

    def __init__(self):
        self.calls: list[str] = []
        self.live = {n: SimpleNamespace(conf=c) for n, c in self.CONF.items()}

    def api_list(self):
        return [{"name": n, "description": "x", "input_schema": {"type": "object"}}
                for n in ("vizself__search_code", "kpow__books")]

    def owns(self, name):
        return name.split("__")[0] in self.CONF

    def reads(self, name):
        return True if self.owns(name) else None

    def conf_of(self, name):
        return self.CONF.get(name.split("__")[0])

    def replaced(self):
        return set()

    def run(self, name, args, timeout=None):
        self.calls.append(name)
        return "voice_client.h plays the reply.", False


class FakeBot:
    def __init__(self):
        self.code_calls: list[dict] = []

    def code(self, **params):
        self.code_calls.append(params)
        return {"code": True}


def until(cond, timeout=2.0):
    end = time.time() + timeout
    while time.time() < end and not cond():
        time.sleep(0.01)
    return cond()


class CodeTurn(BrainTest):
    def setUp(self):
        super().setUp()
        self.hub, self.bot = Hub(), FakeBot()
        self.spoken: list[str] = []
        self.quips: list[str] = []

    def turn(self, *replies):
        box = tools.Toolbox(lambda: None, fake_wled(), fake_vizmac(),
                            memory=SimpleNamespace(prompt_text=lambda: "", remember=lambda f: "saved"),
                            routines=SimpleNamespace(prompt_text=lambda: ""), connectors=self.hub)
        claude = FakeClaude(*replies)
        b = make_brain(claude, box)
        b.get_bot = lambda: self.bot
        splitter = speech.SentenceSplitter(self.spoken.append, lambda kind, value: None)
        result = b.turn("how do you see?", on_text=splitter.feed,
                        on_quip=lambda line, after: self.quips.append(line))
        splitter.flush()
        return b, claude, result

    def test_a_code_look_up_plays_the_show_and_says_found_it_before_the_answer(self):
        _, _, result = self.turn(reply(tool_use("vizself__search_code")), reply(text("[face:happy] With my camera.")))
        self.assertIn(self.quips[0], codemode.OPENER)     # the opener replaces the "One sec." filler
        self.assertIn(self.quips[1], codemode.FOUND_IT)
        self.assertEqual(self.spoken, ["With my camera."])
        self.assertEqual(result["text"], "With my camera.")
        self.assertTrue(until(lambda: len(self.bot.code_calls) == 2))
        self.assertEqual(self.bot.code_calls, [{"v": 1, "why": "vizself"}, {"found": 1}])

    def test_talk_between_code_look_ups_is_not_said(self):
        self.turn(reply(tool_use("vizself__search_code", id="a")),
                  reply(text("That wasn't it. Let me look again."), tool_use("vizself__read_code", id="b")),
                  reply(text("[face:happy] Springy eyelids.")))
        self.assertEqual(self.spoken, ["Springy eyelids."])
        self.assertEqual(sum(q in codemode.FOUND_IT for q in self.quips), 1)
        self.assertTrue(until(lambda: len(self.bot.code_calls) == 2))
        self.assertEqual(self.bot.code_calls, [{"v": 1, "why": "vizself"}, {"found": 1}])   # one show, no restart

    def test_no_answer_shrugs_out_of_code_mode(self):
        self.turn(reply(tool_use("vizself__search_code")), reply(stop="end_turn"))
        self.assertTrue(until(lambda: {"v": 0} in self.bot.code_calls))

    def test_the_system_prompt_names_the_code_connector(self):
        b, _, _ = self.turn(reply(text("Hi.")))
        self.assertIn("Tools starting with vizself__ are your own source code", b.system_prompt)

    def test_a_code_connector_has_its_own_budget_and_the_others_keep_the_shared_one(self):
        code = [tool_use("vizself__search_code", {"query": str(i)}, id=f"c{i}") for i in range(11)]
        other = [tool_use("kpow__books", {"query": str(i)}, id=f"k{i}") for i in range(5)]
        _, claude, _ = self.turn(reply(*code, *other), reply(text("Done.")))
        self.assertEqual(self.hub.calls.count("vizself__search_code"), 10)
        self.assertEqual(self.hub.calls.count("kpow__books"), 4)
        results = claude.requests[1]["messages"][-1]["content"]
        self.assertIn("no time left", results[10]["content"])
        self.assertIn("no time left", results[15]["content"])

    def test_more_rounds_while_digging_through_code(self):
        rounds = [reply(tool_use("vizself__search_code", {"query": str(i)}, id=f"c{i}")) for i in range(9)]
        _, claude, _ = self.turn(*rounds, reply(text("Found it all.")))
        self.assertEqual(len(claude.requests), 10)   # past MAX_TOOL_ROUNDS (6)


class Show(unittest.TestCase):
    def test_quips_come_every_period_from_a_shuffle_bag_with_a_beat_signal(self):
        b, said = FakeBot(), []

        def say(line, after):
            said.append(line)
            if after:
                after(48000)   # 1 s of 24 kHz s16 audio
        show = codemode.CodeShow(lambda: b, say, rng=random.Random(1), every_s=0.02, jitter_s=0)
        show.start(opener=False)
        self.assertTrue(until(lambda: len(said) >= len(codemode.WAITING)))
        show.found()
        quips = [q for q in said if q in codemode.WAITING][:len(codemode.WAITING)]
        self.assertEqual(sorted(quips), sorted(codemode.WAITING))   # each once before any repeat
        self.assertTrue(until(lambda: any("quip" in c for c in b.code_calls)))
        beat = next(c for c in b.code_calls if "quip" in c)
        self.assertEqual(beat["quip"], 1000)
        self.assertIn(bot.EXPRESSIONS[beat["expr"]], codemode.QUIP_FACES)

    def test_found_once_and_no_shrug_after_it(self):
        b, said = FakeBot(), []
        show = codemode.CodeShow(lambda: b, lambda line, after: said.append(line), every_s=60)
        show.start(opener=True)
        show.found()
        show.found()
        show.end()
        self.assertTrue(until(lambda: len(b.code_calls) >= 2))
        time.sleep(0.05)
        self.assertEqual(b.code_calls, [{"v": 1, "why": "vizself"}, {"found": 1}])
        self.assertEqual(len(said), 2)   # the opener and one "found it"

    def test_a_bot_without_code_mode_is_fine(self):
        class Old:
            def code(self, **params):
                raise bot.BotError("this vizBot's firmware has no code mode yet")
        show = codemode.CodeShow(lambda: Old(), lambda line, after: None, every_s=60)
        show.start(opener=False)
        show.end()


class MouthQuips(unittest.TestCase):
    def test_a_quip_is_spoken_but_not_part_of_the_reply_and_after_gets_its_length(self):
        clip = speech.LiveClip()
        got = threading.Event()
        lengths = []
        with mock.patch.object(speech, "synthesize", lambda text, voice, rate: b"\0" * 4800):
            mouth = speech.Mouth(clip, "v", 1)
            mouth.say("Hello.")
            mouth.say("A joke.", after=lambda n: (lengths.append(n), got.set()), record=False)
            self.assertTrue(got.wait(2))
            mouth.close()
        self.assertEqual(mouth.spoken, ["Hello."])
        self.assertEqual(lengths, [4800])


class Versions(unittest.TestCase):
    def test_a_bench_build_counts_as_its_release(self):
        self.assertTrue(bot.firmware_ok("3.9.1-code.3", (3, 9, 1)))
        self.assertFalse(bot.firmware_ok("3.9.0-code.3", (3, 9, 1)))
        self.assertFalse(bot.firmware_ok(None))


class Config(unittest.TestCase):
    def servers(self, **servers):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "connectors.json"
            path.write_text(json.dumps({"mcpServers": servers}))
            with mock.patch.object(config, "CONNECTORS_PATH", path):
                return config.connectors()

    def test_budget_and_code_mode(self):
        conns, problems = self.servers(vizself={"command": "vizself", "code_mode": True, "max_lookups": 50,
                                                "lookup_budget_s": 60})
        self.assertEqual(problems, [])
        self.assertTrue(conns["vizself"]["code_mode"])
        self.assertEqual(conns["vizself"]["budget"], {"max_lookups": 20, "lookup_budget_s": 60.0})   # clamped

    def test_code_mode_must_be_a_boolean(self):
        _, problems = self.servers(x={"command": "x", "code_mode": "yes"})
        self.assertEqual(problems, ['connector x: "code_mode" should be true or false'])


if __name__ == "__main__":
    unittest.main()
