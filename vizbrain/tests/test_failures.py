"""What he says when Claude can't be reached, and what the log keeps.
Run: python -m unittest discover -s tests"""

import io
import unittest
from types import SimpleNamespace
from unittest import mock

import anthropic

from vizbrain import brain, config, server, usage

REQUEST = SimpleNamespace(method="POST", url="https://api.anthropic.com/v1/messages")


def api_error(cls, status, message="nope"):
    response = SimpleNamespace(request=REQUEST, status_code=status, headers={})
    return cls(message, response=response, body={"error": {"message": message}})


class Explain(unittest.TestCase):
    def test_each_failure_gets_its_own_words(self):
        req = REQUEST
        cases = [
            (api_error(anthropic.AuthenticationError, 401), "key isn't working", "rejected"),
            (api_error(anthropic.PermissionDeniedError, 403), "key isn't working", "rejected"),
            (api_error(anthropic.NotFoundError, 404), "can't use this model", None),
            (api_error(anthropic.RateLimitError, 429), "slow down", None),
            (api_error(anthropic.InternalServerError, 529), "busy right now", None),
            (api_error(anthropic.BadRequestError, 400, "Your credit balance is too low"), "out of credit", "no_credit"),
            (anthropic.APIConnectionError(request=req), "can't reach Claude", None),
            (anthropic.APITimeoutError(request=req), "can't reach Claude", None),
            (TypeError("unexpected keyword"), "vizbrain doctor", None),
        ]
        for err, words, key_state in cases:
            text, state = brain.explain(err)
            self.assertIn(words, text, type(err).__name__)
            self.assertEqual(state, key_state, type(err).__name__)

    def test_a_rate_limit_is_not_blamed_on_the_wifi(self):
        self.assertNotIn("internet", brain.explain(api_error(anthropic.RateLimitError, 429))[0])


class KeyState(unittest.TestCase):
    def make(self, client):
        box = SimpleNamespace(api_list=lambda: [], wled=SimpleNamespace(cached_names=lambda: []),
                              memory=SimpleNamespace(prompt_text=lambda: ""),
                              routines=SimpleNamespace(prompt_text=lambda: ""), cached_weather=lambda: "fine")
        b = brain.Brain({**config.DEFAULTS, "user_name": "Dana", "web_search": False}, box, api_key=None)
        b.client, b.key_state = client, "present"
        return b

    def failing(self, err):
        def boom(**kw):
            raise err
        return SimpleNamespace(messages=SimpleNamespace(create=boom),
                               beta=SimpleNamespace(messages=SimpleNamespace(create=boom)))

    def test_no_key(self):
        b = brain.Brain(dict(config.DEFAULTS), None, api_key=None)
        self.assertEqual(b.key_state, "none")

    def test_rejected_key_is_spoken_and_remembered(self):
        with mock.patch.object(usage, "over_cap", return_value=None), mock.patch("sys.stdout", io.StringIO()):
            b = self.make(self.failing(api_error(anthropic.AuthenticationError, 401)))
            result = b.turn("hello")
        self.assertIn("run vizbrain set-key", result["text"])
        self.assertEqual(b.key_state, "rejected")
        self.assertIn("AuthenticationError", b.last_error["what"])

    def test_event_with_a_bad_key_stays_quiet_and_does_not_crash(self):
        with mock.patch.object(usage, "over_cap", return_value=None), mock.patch("sys.stdout", io.StringIO()):
            b = self.make(self.failing(api_error(anthropic.RateLimitError, 429)))
            self.assertEqual(b.quick("[Event: lean in]")["text"], "")
        self.assertEqual(b.key_state, "present")       # a rate limit says nothing about the key

    def test_stand_in_says_how_to_add_a_key(self):
        box = SimpleNamespace(run=lambda *a: ("ok", False))
        b = brain.Brain({**config.DEFAULTS, "user_name": "Dana"}, box, api_key=None)
        self.assertIn("run vizbrain set-key", b.turn("what is the capital of France")["text"])


class Log(unittest.TestCase):
    def test_lines_are_stamped_once_each(self):
        out = io.StringIO()
        s = server.Stamped(out)
        with mock.patch.object(server.time, "strftime", return_value="10-13 20:00:00 "):
            print("[bot] using 192.168.1.80", file=s)
            s.write("Traceback\n  File x\n")
            s.write("partial")
            s.write(" line\n")
        self.assertEqual(out.getvalue().splitlines(), [
            "10-13 20:00:00 [bot] using 192.168.1.80", "10-13 20:00:00 Traceback",
            "10-13 20:00:00   File x", "10-13 20:00:00 partial line"])

    def test_default_log_setting_keeps_words_out(self):
        self.assertFalse(config.DEFAULTS["debug_log"])


if __name__ == "__main__":
    unittest.main()
