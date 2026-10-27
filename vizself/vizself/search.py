"""Keyword search (BM25) that understands code names: `scCameraMotionTick` is also
"camera", "motion" and "tick", so a spoken question can find it."""

from __future__ import annotations

import math
import re
from collections import Counter

_WORD = re.compile(r"[A-Za-z][A-Za-z0-9]*|\d+")
_CAMEL = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|\d+")

# Words a spoken question carries that say nothing about what to find.
STOP = frozenset("""a an and are about at be by can did do does doing for from get got had has have how
i if in into is it its me my of on or out so tell that the their them then there these they this to
up was we were what when where which who why will with work works working you your yourself""".split())

K1, B = 1.4, 0.6


def tokens(text: str) -> list[str]:
    """Words, plus the parts of camelCase and snake_case names. Lowercase, no stop words."""
    out = []
    for word in _WORD.findall(text.replace("_", " ")):
        low = word.lower()
        parts = [p.lower() for p in _CAMEL.findall(word)]
        for t in ([low] + parts if len(parts) > 1 else [low]):
            if t not in STOP and len(t) > 1:
                out.append(_stem(t))
    return out


def _stem(t: str) -> str:
    """Just enough stemming for speech: "blinks"/"blinking" -> "blink"."""
    for suffix in ("ing", "ers", "er", "es", "ed", "s"):
        if t.endswith(suffix) and len(t) - len(suffix) >= 3:
            return t[:-len(suffix)]
    return t


class BM25:
    def __init__(self, docs: list[str]):
        self.tfs = [Counter(tokens(d)) for d in docs]
        self.lens = [sum(tf.values()) for tf in self.tfs]
        self.avg = (sum(self.lens) / len(self.lens)) if self.lens else 1.0
        df = Counter(t for tf in self.tfs for t in tf)
        n = len(docs)
        self.idf = {t: math.log(1 + (n - c + 0.5) / (c + 0.5)) for t, c in df.items()}

    def scores(self, query: str) -> list[float]:
        q = set(tokens(query))
        out = []
        for tf, n in zip(self.tfs, self.lens):
            s = 0.0
            for t in q:
                f = tf.get(t)
                if f:
                    s += self.idf[t] * f * (K1 + 1) / (f + K1 * (1 - B + B * n / self.avg))
            out.append(s)
        return out
