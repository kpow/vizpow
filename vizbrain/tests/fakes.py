"""A scripted stand-in for the Anthropic client, for testing the tool loop.

    claude = FakeClaude(reply(text("Let me check."), tool_use("get_weather")),
                        reply(text("Sunny and 70.")))
    b = make_brain(claude, box)
    b.turn("what's the weather?")
    claude.requests   # every request the brain sent, deep-copied

Each request pops the next reply. Streaming yields the events the brain
reads (content_block_start, text_delta) built from the same reply.
"""

from __future__ import annotations

import contextlib
import copy
import io
import unittest
from types import SimpleNamespace
from unittest import mock

from vizbrain import brain, config, usage


def text(t: str) -> SimpleNamespace:
    return SimpleNamespace(type="text", text=t)


def tool_use(name: str, input: dict | None = None, id: str | None = None) -> SimpleNamespace:
    return SimpleNamespace(type="tool_use", name=name, input=input or {}, id=id or f"toolu_{name}")


def reply(*blocks, stop: str | None = None) -> SimpleNamespace:
    if stop is None:
        stop = "tool_use" if any(b.type == "tool_use" for b in blocks) else "end_turn"
    return SimpleNamespace(content=list(blocks), stop_reason=stop, model="claude-sonnet-5-5",
                           usage=SimpleNamespace(input_tokens=10, output_tokens=5,
                                                 cache_creation_input_tokens=0, cache_read_input_tokens=0))


class FakeClaude:
    def __init__(self, *replies):
        self.replies = list(replies)
        self.requests: list[dict] = []
        self.messages = _Api(self)
        self.beta = SimpleNamespace(messages=_Api(self))

    def _next(self, params: dict):
        self.requests.append(copy.deepcopy(params))
        if not self.replies:
            raise AssertionError(f"unexpected request #{len(self.requests)}: no scripted reply left")
        return self.replies.pop(0)


class _Api:
    def __init__(self, claude: FakeClaude):
        self.claude = claude

    def create(self, **params):
        return self.claude._next(params)

    def stream(self, **params):
        return _Stream(self.claude._next(params))


class _Stream:
    def __init__(self, message):
        self.message = message

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def __iter__(self):
        for block in self.message.content:
            yield SimpleNamespace(type="content_block_start", content_block=SimpleNamespace(type=block.type))
            if block.type == "text":
                yield SimpleNamespace(type="content_block_delta",
                                      delta=SimpleNamespace(type="text_delta", text=block.text))

    def get_final_message(self):
        return self.message


def make_brain(claude: FakeClaude, box, **settings) -> brain.Brain:
    b = brain.Brain({**config.DEFAULTS, "user_name": "Dana", "web_search": False, **settings}, box, api_key=None)
    b.client, b.key_state = claude, "present"
    return b


class BrainTest(unittest.TestCase):
    """Keeps spend accounting and its log lines out of the way of the test."""

    def setUp(self):
        for patch in (mock.patch.object(usage, "record", return_value=0.0),
                      mock.patch.object(usage, "over_cap", return_value=None),
                      contextlib.redirect_stdout(io.StringIO())):
            patch.__enter__()
            self.addCleanup(patch.__exit__, None, None, None)
