"""Code mode: the show while vizBot looks through his own code.

A connector marked "code_mode" in connectors.json (the vizself connector) gets
the "Ink Reader" animation on the bot (GET /bot/code; older firmware answers 404) and a
corny line every ~8 s while it digs, so a long look-up is fun to watch instead
of a silent wait. The lines go into the turn's live clip (the bot answers 409
to /brain/play during a voice turn), which also keeps the bot's 45 s read
timeout from ending the turn. Spec: docs/vizself/code-quips.md and
docs/vizself/code-mode-motion-spec.md.
"""

from __future__ import annotations

import random
import threading
import time

from .bot import EXPRESSIONS
from .speech import PLAY_RATE

OPENER = (
    "Ooh, a question about me. Let me look at my own code.",
    "Hang on, I'm opening my own brain.",
    "One sec, reading my own source code. Very weird feeling.",
    "Let me check my insides.",
    "Okay, scrolling through myself.",
)
WAITING = (
    "I have a lot of header files. Like, a lot.",
    "I'm not lost. I'm exploring.",
    "Who wrote this? Oh. Kevin did.",
    "Searching. Searching. Still very handsome though.",
    "My code is like ferrofluid. Spiky, but in a good way.",
    "Grepping as fast as my little servos allow.",
    "If you hear clicking, that's just me thinking really hard.",
    "I found a comment that says to do, later. That was a while ago.",
    "Reading about myself is basically a selfie, but longer.",
    "I'm ninety percent sure this is the right file.",
    "So many semicolons. Is that normal?",
    "Why do programmers prefer dark mode? Because light attracts bugs.",
    "I'd tell you a UDP joke, but you might not get it.",
    "I asked my camera for help. It just stared at me.",
    "There are ten kinds of robots. Ones that get binary, and ones that don't.",
    "My servos say hi. They're not involved, they just wanted to say hi.",
    "This function is longer than my attention span.",
    "Debugging myself is like looking for my glasses while wearing them.",
    "I'd tell you a joke about my firmware, but it's still compiling.",
    "I was going to read the docs, but the code was right there.",
    "Fun fact: I am mostly header files and vibes.",
    "Hold on, there's a lot of me to get through.",
    "My pins are all accounted for. Mostly.",
    "I think my eyes are written in math. Neat.",
    "Don't worry, I'm reading the comments too. Some of them are spicy.",
)
LONG_WAIT = (
    "Okay, this is a deep one. Still digging.",
    "Still here. Still reading. Still adorable.",
    "This rabbit hole has rabbit holes.",
    "Almost there, I can feel it in my tentacles.",
    "You could get a snack. I'll wait. Well, you'll wait.",
    "I'm learning so much about myself right now.",
)
FOUND_IT = (
    "Got it!",
    "Found it. Okay, here's the deal.",
    "Aha, there it is.",
    "Okay, I know how I work now.",
    "Found the spot.",
)
# The face for a quip: mostly happy. Mischief and sassy get a body pump on the bot instead of spikes.
QUIP_FACES = ("happy",) * 4 + ("winking", "winking", "excited", "excited", "mischief", "proud", "sassy")

EVERY_S = 8.0       # a line every 8 s,
JITTER_S = 1.5      # give or take, so it never ticks like a metronome
LONG_AFTER_S = 30.0  # then the long-wait lines


class Bag:
    """Shuffle bag: nothing repeats until the whole pool has been used."""

    def __init__(self, pool, rng: random.Random):
        self.pool, self.rng, self.left = list(pool), rng, []

    def draw(self) -> str:
        if not self.left:
            self.left = self.pool[:]
            self.rng.shuffle(self.left)
        return self.left.pop()


def _pcm_ms(pcm_bytes: int) -> int:
    return int(pcm_bytes / 2 * 1000 / PLAY_RATE)


class CodeShow:
    """One turn's code-mode show. say(text, after) speaks into the turn's live
    clip and calls after(pcm_bytes) once that line is in the clip; get_bot()
    is the bot or None. Every bot call runs on its own thread and failures are
    ignored: old firmware answers 404, and the show is never worth a turn."""

    def __init__(self, get_bot, say, rng: random.Random | None = None, clock=time.monotonic,
                 every_s: float = EVERY_S, jitter_s: float = JITTER_S):
        self.get_bot, self.say, self.clock = get_bot, say, clock
        self.rng = rng or random.Random()
        self.every_s, self.jitter_s = every_s, jitter_s
        self.waiting, self.long = Bag(WAITING, self.rng), Bag(LONG_WAIT, self.rng)
        self.digging = False
        self.started = 0.0
        self.quips: list[str] = []
        self._wake = threading.Event()
        self._thread: threading.Thread | None = None

    # ---- the bot ----------------------------------------------------------

    def _bot(self, **params) -> None:
        def call():
            bot = self.get_bot()
            if bot is None:
                return
            try:
                bot.code(**params)
            except Exception:  # noqa: BLE001 - the animation is decoration
                pass
        threading.Thread(target=call, daemon=True).start()

    # ---- the turn --------------------------------------------------------

    def start(self, opener: bool) -> None:
        """A code look-up is starting: the animation, an opening line (unless
        something was already said), then a quip every ~8 s until found() or end()."""
        if self.digging:
            return
        self.digging = True
        if not self.started:
            self.started = self.clock()
        self._bot(v=1, why="vizself")
        if opener:
            self.say(self.rng.choice(OPENER), None)
        self._wake.clear()
        self._thread = threading.Thread(target=self._quips, daemon=True)
        self._thread.start()

    def _quips(self) -> None:
        while True:
            wait = self.every_s + self.rng.uniform(-self.jitter_s, self.jitter_s)
            if self._wake.wait(max(0.25 * self.every_s, wait)) or not self.digging:
                return
            long = self.clock() - self.started > LONG_AFTER_S
            line = (self.long if long else self.waiting).draw()
            face = EXPRESSIONS.index(self.rng.choice(QUIP_FACES))
            self.quips.append(line)
            # The beat's signal goes once the line is in the clip, so it lands just before the audio
            self.say(line, lambda pcm_bytes, face=face: self._bot(quip=_pcm_ms(pcm_bytes), expr=face))

    def _stop(self) -> None:
        self.digging = False
        self._wake.set()

    def found(self) -> None:
        """The answer is starting: a "found it" line, the hero, then the answer."""
        if not self.digging:
            return
        self._stop()
        self._bot(found=1)
        self.say(self.rng.choice(FOUND_IT), None)

    def end(self) -> None:
        """The turn is over. Still digging means no answer came: shrug out."""
        if self.digging:
            self._stop()
            self._bot(v=0)
