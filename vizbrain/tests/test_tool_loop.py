"""The tool loop in Brain._claude_turn: when a tool's result needs a second model call."""

import unittest

from vizbrain.tools import _obj

from fakes import BrainTest, FakeClaude, make_brain, reply, text, tool_use
from test_personal import toolbox


def box_with_fakes():
    """A real Toolbox whose weather and base-LED tools don't touch the network or a bot."""
    box = toolbox()
    ran: list[tuple] = []
    box._add("get_weather", "Get the local weather.", _obj({}),
             lambda: ran.append(("get_weather",)) or "Sunny, 70F.", info=True)
    box._add("set_base_leds", "Set the base LEDs.", _obj({"color": {"type": "string"}}),
             lambda color=None: ran.append(("set_base_leds", color)) or f"base ring {color}")
    box._add("fail_leds", "An action tool that fails.", _obj({}), lambda: 1 / 0)
    box.ran = ran
    return box


def tool_results(request: dict) -> list[dict]:
    last = request["messages"][-1]
    return [b for b in last["content"] if isinstance(b, dict) and b.get("type") == "tool_result"]


class ToolLoop(BrainTest):
    def setUp(self):
        super().setUp()
        self.box = box_with_fakes()

    def run_turn(self, *replies, on_text=None):
        claude = FakeClaude(*replies)
        result = make_brain(claude, self.box).turn("hi", on_text=on_text)
        self.assertEqual(claude.replies, [], "a scripted reply was never asked for")
        return claude, result

    def test_action_tool_with_its_confirmation_takes_one_call(self):
        claude, result = self.run_turn(
            reply(text("[face:happy] Going red."), tool_use("set_base_leds", {"color": "red"})))
        self.assertEqual(len(claude.requests), 1)
        self.assertEqual(result["text"], "Going red.")
        self.assertEqual(self.box.ran, [("set_base_leds", "red")])

    def test_action_tool_without_words_gets_a_follow_up_call(self):
        claude, result = self.run_turn(
            reply(tool_use("set_base_leds", {"color": "red"})),
            reply(text("Done, it's red.")))
        self.assertEqual(len(claude.requests), 2)
        self.assertEqual(result["text"], "Done, it's red.")

    def test_failed_action_gets_a_follow_up_call(self):
        claude, result = self.run_turn(
            reply(text("Flashing now."), tool_use("fail_leds")),
            reply(text("That didn't work.")))
        self.assertEqual(len(claude.requests), 2)
        self.assertTrue(tool_results(claude.requests[1])[0]["is_error"])

    def test_lookup_tool_result_is_read_and_replaces_the_filler(self):
        claude, result = self.run_turn(
            reply(text("Let me check."), tool_use("get_weather")),
            reply(text("[face:happy] It's sunny and 70.")))
        self.assertEqual(len(claude.requests), 2)
        self.assertEqual(tool_results(claude.requests[1])[0]["content"], "Sunny, 70F.")
        self.assertEqual(result["text"], "It's sunny and 70.")

    def test_any_tool_flagged_as_a_lookup_gets_its_result_read(self):
        self.box._add("kpow__recent_build_log", "What Kevin built lately.", _obj({}),
                      lambda: "Oct 12: a battery meter.", info=True)
        claude, result = self.run_turn(
            reply(text("One sec."), tool_use("kpow__recent_build_log")),
            reply(text("You added a battery meter.")))
        self.assertEqual(len(claude.requests), 2)
        self.assertEqual(result["text"], "You added a battery meter.")

    def test_streamed_turn_speaks_the_words_as_they_arrive(self):
        heard: list[str] = []
        self.run_turn(reply(text("Let me check."), tool_use("get_weather")),
                      reply(text("Sunny and 70.")), on_text=heard.append)
        self.assertIn("Let me check.", "".join(heard))
        self.assertIn("Sunny and 70.", "".join(heard))

    def test_the_tool_list_is_identical_on_every_call_of_a_turn(self):
        claude, _ = self.run_turn(reply(text("Let me check."), tool_use("get_weather")),
                                  reply(text("Sunny.")))
        self.assertEqual(claude.requests[0]["tools"], claude.requests[1]["tools"])


class Reads(unittest.TestCase):
    def test_lookup_built_ins_are_read_and_actions_are_not(self):
        box = toolbox()
        for name in ("get_weather", "look", "music_status", "list_lab_devices"):
            self.assertTrue(box.reads(name), name)
        for name in ("set_base_leds", "wled_set", "remember", "music_control"):
            self.assertFalse(box.reads(name), name)

    def test_a_name_that_is_not_a_built_in_is_read(self):
        self.assertTrue(toolbox().reads("kpow__books"))


if __name__ == "__main__":
    unittest.main()
