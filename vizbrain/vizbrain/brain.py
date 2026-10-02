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

from . import usage
from .bot import EXPRESSIONS, GESTURES
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
- Everything you write is spoken aloud by a text-to-speech voice. Keep replies to one or two short, natural sentences (about 30 words at most) unless Kevin asks for more.
- No markdown, lists, emoji, URLs or code. Spell out symbols and units the way a person would say them.
- Start every reply with a face tag for how you feel, like [face:happy]. Faces: {faces}.
- When a head gesture fits, add one gesture tag right after the face, like [gesture:nod]. Gestures: nod, shake, lookup, lookdown, left, right. Most replies need none.
- Tags are silent: they set your face and head, and are never spoken. Don't use tools for faces or gestures.
- Use tools only to change things: lab lights, the keyboard, your base LEDs, or pointing your head at a specific angle. For those, write your short spoken confirmation in the same reply as the tool call, as if it already worked; you'll hear back only if a tool fails.
- For weather, answer from the weather in your context; call get_weather only if it's missing.
- You have a camera in your head: use look when asked what you see, or to see who's there.
- When Kevin tells you something worth keeping (names, preferences, plans), save it with remember. Your memories are in your context.
- Messages in brackets that start with Event or Routine come from your sensors or your schedule, not from Kevin talking. Respond to them naturally and briefly, as yourself.
- If a tool fails, say so plainly in a few words.
- Kevin's messages may start with a [System note ...] listing tools you ran in your previous reply and their results. It comes from the system, not Kevin. Treat those actions as done, don't redo them unless asked, and don't mention the note.
- If you didn't catch what was said, ask Kevin to say it again.
"""


TAG_RE = re.compile(r"\[(face|gesture)\s*:\s*([a-z_]+)\]", re.I)

# Faces and gestures ride in the reply as tags (one model call instead of a
# tool round trip each); the lab-device list is in the prompt for the same reason.
CLAUDE_SKIP_TOOLS = {"set_expression", "head_gesture", "list_lab_devices"}
# Tools whose result the model must read before it can answer.
INFO_TOOLS = {"get_weather", "list_lab_devices", "look"}


class Brain:
    def __init__(self, settings: dict, toolbox: Toolbox, api_key: str | None):
        self.settings = settings
        self.toolbox = toolbox
        self.history: list[dict] = []
        self.pending_note = ""
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
        lights = ", ".join(self.toolbox.wled.cached_names()) or "none found yet"
        mem = self.toolbox.memory.prompt_text() or "(nothing yet)"
        routines = self.toolbox.routines.prompt_text() or "(none)"
        return (f"Current personality: {personality} — {style}.\nLocal time: {now}.\n"
                f"Things you remember:\n{mem}\nYour routines:\n{routines}\n"
                f"Lab lights (WLED names for wled_set): {lights}. "
                f"Kevin may call them by a nickname; pick the closest name.\n"
                f"Weather (refreshed every 15 minutes): {self.toolbox.cached_weather() or 'not loaded yet; use get_weather'}")

    def turn(self, heard: str, personality: str = "Chill", on_text=None) -> dict:
        """Run one conversational turn. Returns {text, tools}."""
        with self.lock:
            if time.time() - self.last_turn > self.settings["session_idle_s"]:
                self.history = []
                self.pending_note = ""
            user_msg = self.pending_note + heard
            self.last_turn = time.time()
            if self.client is None:
                result = self._offline_turn(heard)
            else:
                try:
                    result = self._claude_turn(user_msg, personality, on_text)
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
            actions = [t for t in result.get("tools", []) if "(" in t]   # real tool calls, not face= tags
            self.history += [{"role": "user", "content": user_msg},
                             {"role": "assistant", "content": result["text"] or "(no reply)"}]
            # Remember what was done, not just what was said, so the next
            # message doesn't redo it. It rides on the NEXT user message as a
            # labeled system note: inside his own reply he took it for words he
            # wrote and apologized for them.
            self.pending_note = ("[System note, not from Kevin: in your last reply you ran: "
                                 + "; ".join(a[:140] for a in actions) + "]\n") if actions else ""
            keep = self.settings["history_turns"] * 2
            self.history = self.history[-keep:]
            return result

    # ---- Claude ---------------------------------------------------------

    def _claude_turn(self, heard: str, personality: str, on_text=None) -> dict:
        import anthropic

        system = [
            {"type": "text", "text": SYSTEM_PROMPT.replace("{faces}", ", ".join(EXPRESSIONS))},
            {"type": "text", "text": self._context(personality)},
        ]
        messages = list(self.history) + [{"role": "user", "content": heard}]
        tools = [t for t in self.toolbox.api_list() if t["name"] not in CLAUDE_SKIP_TOOLS]
        used: list[str] = []
        text_parts: list[str] = []

        for _ in range(MAX_TOOL_ROUNDS):
            resp = self._create(system, messages, tools, on_text)
            if resp.stop_reason == "refusal":
                return {"text": "Hmm, I'd better not answer that one.", "tools": used}
            # Keep text from every round: words before a tool call are part of the reply.
            text_parts += [b.text for b in resp.content if getattr(b, "type", "") == "text" and b.text.strip()]
            calls = [b for b in resp.content if getattr(b, "type", "") == "tool_use"]
            if resp.stop_reason != "tool_use" or not calls:
                break
            # Append the assistant turn unchanged (thinking/progress blocks included).
            messages.append({"role": "assistant", "content": resp.content})
            results = []
            for call in calls:
                out, is_err = self.toolbox.run(call.name, call.input)
                shown = out if isinstance(out, str) else "(photo)"
                used.append(f"{call.name}({_short(call.input)}) -> {shown}")
                results.append({"type": "tool_result", "tool_use_id": call.id,
                                "content": out, "is_error": is_err})
            messages.append({"role": "user", "content": results})
            # Fast path: every tool worked and the confirmation was already
            # written alongside the calls, so skip the follow-up model call.
            # Only for action tools: an info tool's result still has to be said.
            round_text = [b for b in resp.content if getattr(b, "type", "") == "text" and b.text.strip()]
            if (round_text and not any(r["is_error"] for r in results)
                    and not any(c.name in INFO_TOOLS for c in calls)):
                break
            if any(c.name in INFO_TOOLS for c in calls):
                text_parts = []  # "let me check" filler is replaced by the real answer

        raw = " ".join(text_parts)
        expression, gesture = -1, None
        for kind, value in TAG_RE.findall(raw):
            value = value.lower()
            if kind.lower() == "face" and value in EXPRESSIONS and expression < 0:
                expression = EXPRESSIONS.index(value)
            elif kind.lower() == "gesture" and value in GESTURES and gesture is None:
                gesture = value
        tags = [f"face={EXPRESSIONS[expression]}"] if expression >= 0 else []
        tags += [f"gesture={gesture}"] if gesture else []
        return {"text": _speakable(TAG_RE.sub("", raw)), "tools": tags + used,
                "expression": expression, "gesture": gesture}

    def _create(self, system, messages, tools, on_text=None):
        resp = self._create_raw(system, messages, tools, on_text)
        try:
            usage.record(getattr(resp, "model", None) or self.settings["model"], resp.usage)
        except Exception as e:  # noqa: BLE001 - accounting must never break a turn
            print(f"[usage] not recorded: {e}")
        return resp

    def _create_raw(self, system, messages, tools, on_text=None):
        """One model call. With on_text, the reply is streamed and each text
        delta is passed to on_text as it arrives (for sentence-by-sentence speech)."""
        import anthropic

        model = self.settings["model"]
        common = dict(model=model, max_tokens=1024, system=system, messages=messages,
                      tools=tools, cache_control={"type": "ephemeral"},
                      output_config={"effort": self.settings["effort"]})
        if model == "claude-sonnet-5-5":
            # Lowest-latency thinking setting on Sonnet 5.5, plus server-side
            # refusal fallback. Retried plainly if the API rejects either.
            fast = dict(common, thinking={"type": "between_tools"},
                        betas=["server-side-fallback-2026-07-01"], fallbacks="default")
            try:
                return self._call(self.client.beta.messages, fast, on_text)
            except anthropic.BadRequestError as e:
                print(f"[brain] fast path rejected ({e}); retrying plain")
        return self._call(self.client.messages, common, on_text)

    @staticmethod
    def _call(api, params: dict, on_text):
        if on_text is None:
            return api.create(**params)
        with api.stream(**params) as stream:
            for event in stream:
                if (event.type == "content_block_delta"
                        and getattr(event.delta, "type", "") == "text_delta"):
                    on_text(event.delta.text)
            return stream.get_final_message()

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
    text = re.sub(r"[*_`#>\[\]]+", "", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def bubble_text(text: str, limit: int = 90) -> str:
    """First sentence (or a clipped prefix) for the bot's speech bubble."""
    m = re.match(r"(.+?[.!?])(\s|$)", text)
    first = m.group(1) if m else text
    if len(first) <= limit:
        return first
    return first[: limit - 3].rsplit(" ", 1)[0] + "..."
