"""Nothing assumes whose house this is: the name he uses, where he is, and
which gear he offers to control. Run: python -m unittest discover -s tests"""

import time
import unittest
from types import SimpleNamespace
from unittest import mock

from vizbrain import brain, config, tools
from vizbrain.lab import Wled


def fake_wled(*names):
    return SimpleNamespace(cached_names=lambda: sorted(names), names=lambda: sorted(names))


def fake_vizmac(up=False, spotify=False):
    return SimpleNamespace(cached_available=lambda: up, spotify_installed=lambda: spotify,
                           available=lambda: up)


def toolbox(wled=None, vizmac=None, bot=None, location=None):
    return tools.Toolbox(lambda: bot, wled or fake_wled(), vizmac or fake_vizmac(),
                         memory=SimpleNamespace(prompt_text=lambda: ""),
                         routines=SimpleNamespace(prompt_text=lambda: ""), location=location)


class Name(unittest.TestCase):
    def test_prompt_uses_the_configured_name(self):
        b = brain.Brain({"user_name": "Dana"}, toolbox(), api_key=None)
        self.assertIn("who lives on Dana's desk", b.system_prompt)
        self.assertIn("ask Dana to say it again", b.system_prompt)
        self.assertNotIn("Kevin", b.system_prompt)
        self.assertNotIn("{user}", b.system_prompt)
        self.assertNotIn("{faces}", b.system_prompt)

    def test_name_defaults_to_the_mac_account(self):
        run = SimpleNamespace(stdout="Dana Q. Example\n")
        with mock.patch.object(config.subprocess, "run", return_value=run):
            self.assertEqual(config.user_name({"user_name": ""}), "Dana")
        with mock.patch.object(config.subprocess, "run", side_effect=OSError):
            self.assertEqual(config.user_name({}), "friend")

    def test_stand_in_greets_by_name(self):
        b = brain.Brain({**config.DEFAULTS, "user_name": "Dana"}, toolbox(bot=None), api_key=None)
        self.assertEqual(b.turn("hello there")["text"], "Hey Dana! Good to see you.")

    def test_context_mentions_lights_only_when_there_are_some(self):
        with mock.patch.object(tools.Toolbox, "cached_weather", lambda self: "Now: 70°F."):
            none = brain.Brain({"user_name": "Dana"}, toolbox(), None)._context("Chill")
            some = brain.Brain({"user_name": "Dana"}, toolbox(fake_wled("Desk Lamp")), None)._context("Chill")
        self.assertNotIn("WLED", none)
        self.assertIn("Lights (WLED names for wled_set): Desk Lamp. Dana may call them", some)


class Gear(unittest.TestCase):
    ALWAYS = {"move_head", "set_base_leds", "get_weather", "look", "remember", "add_routine", "play_sound"}

    def offered(self, **kw):
        return {t["name"] for t in toolbox(**kw).api_list()}

    def test_a_home_with_no_gear_gets_no_gear_tools(self):
        offered = self.offered()
        self.assertFalse(offered & (tools.WLED_TOOLS | tools.VIZMAC_TOOLS | tools.SPOTIFY_TOOLS))
        self.assertLessEqual(self.ALWAYS, offered)

    def test_each_kind_of_gear_brings_its_own_tools(self):
        self.assertLessEqual(tools.WLED_TOOLS, self.offered(wled=fake_wled("Desk Lamp")))
        self.assertLessEqual(tools.VIZMAC_TOOLS, self.offered(vizmac=fake_vizmac(up=True)))
        spotify = self.offered(vizmac=fake_vizmac(spotify=True))
        self.assertLessEqual(tools.SPOTIFY_TOOLS, spotify)
        self.assertFalse(spotify & tools.VIZMAC_TOOLS)

    def test_every_gated_tool_exists(self):
        names = set(toolbox().tools)
        self.assertLessEqual(tools.WLED_TOOLS | tools.VIZMAC_TOOLS | tools.SPOTIFY_TOOLS, names)

    def test_all_lights_with_no_lights_is_an_error_not_a_success(self):
        w = Wled(None)
        w._last_scan = time.time()          # scanned just now, found nothing
        box = tools.Toolbox(lambda: None, w, fake_vizmac())
        out, is_err = box.run("wled_set", {"device": "all", "on": True})
        self.assertTrue(is_err)
        self.assertIn("no WLED lights found", out)


class Location(unittest.TestCase):
    def test_timezone_comes_from_this_mac(self):
        with mock.patch.object(config.os.path, "realpath",
                               return_value="/var/db/timezone/zoneinfo/America/Chicago"):
            self.assertEqual(config.location({"location": {}}), {"timezone": "America/Chicago"})
            self.assertEqual(config.location({"location": {"city": "Austin", "timezone": "US/Central"}}),
                             {"city": "Austin", "timezone": "US/Central"})

    def test_default_location_names_no_city(self):
        self.assertEqual(config.DEFAULTS["location"], {})

    def weather(self, **kw):
        with mock.patch.object(tools, "weather", lambda lat, lon, metric=False: f"weather at {lat},{lon}{' metric' if metric else ''}"):
            return toolbox(**kw).run("get_weather", {})

    def test_weather_prefers_the_setting_then_the_bot(self):
        bot = SimpleNamespace(state=lambda: {"weatherLat": "41.88", "weatherLon": "-87.63"})
        self.assertEqual(self.weather(bot=bot, location={"lat": 30.27, "lon": -97.74}),
                         ("weather at 30.2700,-97.7400", False))
        self.assertEqual(self.weather(bot=bot), ("weather at 41.8800,-87.6300", False))

    def test_weather_without_a_location_says_so(self):
        out, is_err = self.weather()
        self.assertTrue(is_err)
        self.assertIn("no location set", out)

    def test_bot_coordinates_must_be_numbers(self):
        bot = SimpleNamespace(state=lambda: {"weatherLat": "37.5&x=1", "weatherLon": "-77.4"})
        self.assertTrue(self.weather(bot=bot)[1])


if __name__ == "__main__":
    unittest.main()
