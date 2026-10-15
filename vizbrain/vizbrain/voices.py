"""The voices vizBot can speak with, for the picker on the bot's page.

Every English Kokoro voice, a few blends, and any the owner saves in
settings.json as "custom_voices" (a list of {id, label, group, spec}).
A spec is what `tts_voice` holds: "kokoro:<voice>[@speed]", or a blend
"kokoro:<voice>*<weight>+<voice>*<weight>[@speed]" (see speech.parse_kokoro).
"""

from __future__ import annotations

from . import speech

AMERICAN, BRITISH, MIXES = "American", "British", "Mixes"

_VOICES = [
    # American female
    ("af_heart", "Heart (warm)", AMERICAN), ("af_bella", "Bella (bright)", AMERICAN),
    ("af_nicole", "Nicole (soft, close)", AMERICAN), ("af_sarah", "Sarah (clear)", AMERICAN),
    ("af_sky", "Sky (light)", AMERICAN), ("af_nova", "Nova (calm)", AMERICAN),
    ("af_alloy", "Alloy (even)", AMERICAN), ("af_aoede", "Aoede (gentle)", AMERICAN),
    ("af_jessica", "Jessica (casual)", AMERICAN), ("af_kore", "Kore (steady)", AMERICAN),
    ("af_river", "River (relaxed)", AMERICAN),
    # American male
    ("am_puck", "Puck (playful)", AMERICAN), ("am_michael", "Michael (friendly)", AMERICAN),
    ("am_adam", "Adam (plain)", AMERICAN), ("am_echo", "Echo (smooth)", AMERICAN),
    ("am_eric", "Eric (upbeat)", AMERICAN), ("am_fenrir", "Fenrir (gruff)", AMERICAN),
    ("am_liam", "Liam (young)", AMERICAN), ("am_onyx", "Onyx (deep)", AMERICAN),
    ("am_santa", "Santa (jolly)", AMERICAN),
    # British
    ("bf_emma", "Emma (British)", BRITISH), ("bf_isabella", "Isabella (British)", BRITISH),
    ("bf_alice", "Alice (British)", BRITISH), ("bf_lily", "Lily (British)", BRITISH),
    ("bm_george", "George (British, deep)", BRITISH), ("bm_fable", "Fable (British, storyteller)", BRITISH),
    ("bm_lewis", "Lewis (British)", BRITISH), ("bm_daniel", "Daniel (British, crisp)", BRITISH),
]

CATALOG: list[dict] = [
    {"id": "puck", "label": "Puck, quick (the default)", "group": AMERICAN, "spec": speech.KOKORO_VOICE},
    *[{"id": v, "label": label, "group": group, "spec": f"kokoro:{v}"} for v, label, group in _VOICES],
    {"id": "gentleman", "label": "Old British gentleman", "group": MIXES, "spec": "kokoro:bm_george*0.6+bm_lewis*0.4@0.9"},
    {"id": "storyteller", "label": "Storyteller (slow Fable)", "group": MIXES, "spec": "kokoro:bm_fable@0.95"},
    {"id": "transatlantic", "label": "Transatlantic (Michael and George)", "group": MIXES, "spec": "kokoro:am_michael*0.5+bm_george*0.5"},
    {"id": "dreamy", "label": "Dreamy (Nicole and Sky)", "group": MIXES, "spec": "kokoro:af_nicole*0.5+af_sky*0.5@0.95"},
    {"id": "bouncy", "label": "Bouncy (fast Puck and Eric)", "group": MIXES, "spec": "kokoro:am_puck*0.6+am_eric*0.4@1.15"},
]

SAMPLE = "Hi! This is how I'll sound from now on."


def catalog(settings: dict) -> list[dict]:
    """The built-in voices plus the owner's custom_voices (well-formed ones only)."""
    out = [dict(v) for v in CATALOG]
    ids = {v["id"] for v in out}
    for v in settings.get("custom_voices") or []:
        if (isinstance(v, dict) and isinstance(v.get("id"), str) and v["id"] not in ids
                and isinstance(v.get("spec"), str) and valid(v["spec"])):
            out.append({"id": v["id"], "label": str(v.get("label") or v["id"]),
                        "group": str(v.get("group") or "Custom"), "spec": v["spec"]})
            ids.add(v["id"])
    return out


def find(settings: dict, voice_id: str) -> dict | None:
    return next((v for v in catalog(settings) if v["id"] == voice_id), None)


def current(settings: dict) -> dict:
    """The catalog entry for the voice in use, or a stand-in for a hand-written spec."""
    spec = speech.voice_for(settings)
    for v in catalog(settings):
        if v["spec"] == spec:
            return v
    return {"id": "custom", "label": "Custom (settings.json)", "group": "Custom", "spec": spec}


def valid(spec: str) -> bool:
    """A spec synthesize() can use: a Kokoro spec that parses, or a macOS voice name."""
    if not spec.startswith("kokoro:"):
        return bool(spec.strip())
    try:
        speech.parse_kokoro(spec)
        return True
    except ValueError:
        return False


def listing(settings: dict) -> dict:
    """What GET /v1/voices returns: the choices (no specs) and the current one."""
    now = current(settings)
    return {"current": now["id"], "current_label": now["label"],
            "voices": [{"id": v["id"], "label": v["label"], "group": v["group"]} for v in catalog(settings)]}
