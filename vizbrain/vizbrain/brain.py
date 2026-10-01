"""Conversation: Claude with the tool registry, or a small rule-based stand-in
when no API key is configured (so the voice pipeline can still be tested).

History design: within a turn the tool loop is append-only. Between turns we
keep only plain text (what was heard, what he said), so trimming old turns
never edits a request the API has seen thinking blocks for.
"""

from __future__ import annotations

import datetime as dt
import re
import threading
import time

from .tools import Toolbox

MAX_TOOL_ROUNDS = 6

PERSONALITY_STYLE = {
    "chill": "laid-back, warm and unhurried; dry humor; never in a rush",
    "hyper": "bubbly, excitable and fast; lots of enthusiasm; loves exclamation",
    "grumpy": "a lovable grump; sarcastic and reluctant, but always helps in the end",
}

SYSTEM_PROMPT = """You are vizBot, a small desk robot (an M5Stack Stackchan) who lives in Kevin's office lab. \
You have a face on a screen, a head that can pan and tilt, a ring of LEDs in your base, a microphone and a speaker. \
Kevin talks to you by patting your head and speaking.

How to answer:
- Everything you write is spoken aloud by a text-to-speech voice. Reply in one to three short, natural sentences.
- No markdown, lists, emoji, URLs or code. Spell out symbols and units the way a person would say them.
- Use your tools to be expressive: set a fitting face for your reply, and nod, shake or look around when it fits.
- When Kevin asks you to change lights or the keyboard, use the lab tools, then confirm briefly in words.
- If a tool fails, say so plainly in a few words.
- If you didn't catch what was said, ask Kevin to say it again.
"""


class Brain:
    def __init__(self, settings: dict, toolbox: Toolbox, api_key: str | None):
        self.settings = settings
        self.toolbox = toolbox
        self.history: list[dict] = []
        self.last_turn = 0.0
        self.lock = threading.Lock()
        self.client = None
        if api_key:
            import anthropic
            headers = {}
            if settings.get("workspace_id"):
                # Required for identity-linked keys that aren't scoped to one workspace.
                headers["anthropic-workspace-id"] = settings["workspace_id"]
            if api_key.startswith("sk-ant-usr"):
                # Personal (identity-linked) keys: send as Authorization: Bearer.
                self.client = anthropic.Anthropic(auth_token=api_key, default_headers=headers,
                                                  max_retries=1, timeout=30.0)
            else:
                self.client = anthropic.Anthropic(api_key=api_key, default_headers=headers,
                                                  max_retries=1, timeout=30.0)

    @property
    def online(self) -> bool:
        return self.client is not None

    def _context(self, personality: str) -> str:
        style = PERSONALITY_STYLE.get(personality.lower(), "friendly and curious")
        now = dt.datetime.now().strftime("%A %B %-d, %-I:%M %p")
        return f"Current personality: {personality} — {style}.\nLocal time: {now}."

    def turn(self, heard: str, personality: str = "Chill") -> dict:
        """Run one conversational turn. Returns {text, tools}."""
        with self.lock:
            if time.time() - self.last_turn > self.settings["session_idle_s"]:
                self.history = []
            self.last_turn = time.time()
            if self.client is None:
                result = self._offline_turn(heard)
            else:
                try:
                    result = self._claude_turn(heard, personality)
                except Exception as e:  # noqa: BLE001 - say it out loud rather than go silent
                    print(f"[brain] Claude call failed: {e}")
                    msg = str(e).lower()
                    if "credit balance" in msg:
                        text = "My Claude brain is out of credits. Add some in the Claude Console."
                    elif "authentication" in msg or "api key" in msg:
                        text = "My Claude API key isn't working."
                    else:
                        text = "I couldn't reach my Claude brain just now."
                    result = {"text": text, "tools": [f"error: {e}"[:200]]}
            self.history += [{"role": "user", "content": heard},
                             {"role": "assistant", "content": result["text"] or "(no reply)"}]
            keep = self.settings["history_turns"] * 2
            self.history = self.history[-keep:]
            return result

    # ---- Claude ---------------------------------------------------------

    def _claude_turn(self, heard: str, personality: str) -> dict:
        import anthropic

        system = [
            {"type": "text", "text": SYSTEM_PROMPT},
            {"type": "text", "text": self._context(personality)},
        ]
        messages = list(self.history) + [{"role": "user", "content": heard}]
        tools = self.toolbox.api_list()
        used: list[str] = []
        text_parts: list[str] = []

        for _ in range(MAX_TOOL_ROUNDS):
            resp = self._create(system, messages, tools)
            if resp.stop_reason == "refusal":
                return {"text": "Hmm, I'd better not answer that one.", "tools": used}
            text_parts = [b.text for b in resp.content if getattr(b, "type", "") == "text" and b.text.strip()]
            calls = [b for b in resp.content if getattr(b, "type", "") == "tool_use"]
            if resp.stop_reason != "tool_use" or not calls:
                break
            # Append the assistant turn unchanged (thinking/progress blocks included).
            messages.append({"role": "assistant", "content": resp.content})
            results = []
            for call in calls:
                out, is_err = self.toolbox.run(call.name, call.input)
                used.append(f"{call.name}({_short(call.input)}) -> {out}")
                results.append({"type": "tool_result", "tool_use_id": call.id,
                                "content": out, "is_error": is_err})
            messages.append({"role": "user", "content": results})

        return {"text": _speakable(" ".join(text_parts)), "tools": used}

    def _create(self, system, messages, tools):
        import anthropic

        model = self.settings["model"]
        common = dict(model=model, max_tokens=1024, system=system, messages=messages,
                      tools=tools, cache_control={"type": "ephemeral"})
        if model == "claude-sonnet-5-5":
            # Lowest-latency thinking setting on Sonnet 5.5, plus server-side
            # refusal fallback. Retried plainly if the API rejects either.
            try:
                return self.client.beta.messages.create(
                    **common, thinking={"type": "between_tools"},
                    output_config={"effort": self.settings["effort"]},
                    betas=["server-side-fallback-2026-07-01"], fallbacks="default",
                )
            except anthropic.BadRequestError as e:
                print(f"[brain] fast path rejected ({e}); retrying plain")
        return self.client.messages.create(
            **common, output_config={"effort": self.settings["effort"]},
        )

    # ---- No API key: tiny rule-based stand-in ---------------------------

    def _offline_turn(self, heard: str) -> dict:
        h = heard.lower()
        used: list[str] = []

        def run(tool, **args):
            out, _ = self.toolbox.run(tool, args)
            used.append(f"{tool}({_short(args)}) -> {out}")
            return out

        if not h.strip():
            run("set_expression", name="confused")
            return {"text": "Sorry, I didn't catch that.", "tools": used}
        if "time" in h:
            run("set_expression", name="thinking")
            return {"text": dt.datetime.now().strftime("It's %-I:%M %p."), "tools": used}
        color = next((c for c in sorted(_COLOR_WORDS, key=len, reverse=True) if c in h), None)
        if color and any(w in h for w in ("light", "lamp", "led", "ring", "keyboard", "make", "turn")):
            if "keyboard" in h:
                run("keyboard_flash", color=color, ms=3000)
            else:
                run("set_base_leds", color=color)
            run("set_expression", name="happy")
            return {"text": f"Done. Going {color}.", "tools": used}
        if any(w in h for w in ("hello", "hi ", "hey", "good morning")):
            run("set_expression", name="happy")
            run("head_gesture", name="nod")
            return {"text": "Hey Kevin! Good to see you.", "tools": used}
        run("set_expression", name="thinking")
        return {"text": f"I heard: {heard.strip()[:80]}. My Claude brain needs an API key before I can really think.",
                "tools": used}


_COLOR_WORDS = ["red", "orange", "yellow", "green", "teal", "cyan", "blue", "purple",
                "violet", "magenta", "pink", "white", "gold", "lime"]


def _short(args) -> str:
    s = ", ".join(f"{k}={v}" for k, v in (args or {}).items())
    return s[:80]


def _speakable(text: str) -> str:
    """Strip markdown the TTS would read aloud."""
    text = re.sub(r"[*_`#>]+", "", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def bubble_text(text: str, limit: int = 90) -> str:
    """First sentence (or a clipped prefix) for the bot's speech bubble."""
    m = re.match(r"(.+?[.!?])(\s|$)", text)
    first = m.group(1) if m else text
    if len(first) <= limit:
        return first
    return first[: limit - 3].rsplit(" ", 1)[0] + "..."
