# Developing vizbrain

How vizbrain works inside, how to run it from source, and how to release it. The pages for people who just use it start at the [README](README.md).

## Layout

| Piece | What it does |
|---|---|
| `vizbrain/server.py` | HTTP API on :4050, who may call which route, turns, events, the routine scheduler, pairing and the once-a-minute announce to the bot, Bonjour `_vizbrain._tcp` |
| `vizbrain/brain.py` | The Claude conversation (system prompt, tool loop, prompt caching), spoken failure messages, the no-key stand-in |
| `vizbrain/speech.py` | whisper (mlx) to hear, Kokoro to speak (24 kHz PCM), macOS `say` as the fallback voice |
| `vizbrain/tools.py` | The tool registry shared by the brain and MCP; hides tools whose gear isn't present |
| `vizbrain/bot.py`, `lab.py` | Clients for the vizBot, WLED and vizMac APIs; bot discovery; `MIN_FIRMWARE` |
| `vizbrain/memory.py` | Remembered facts and routines (JSON next to settings.json, reloaded when edited) |
| `vizbrain/usage.py` | Daily Claude spend (`usage.json`) and the spend caps |
| `vizbrain/config.py` | Settings, the Keychain key, the pairing token |
| `vizbrain/install.py` | `setup`, the login agent, `update`, `uninstall` |
| `vizbrain/doctor.py` | `vizbrain doctor` |
| `vizbrain/wake.py` | "Hey vizBot": UDP receiver, openWakeWord model, the recording studio page |
| `vizbrain/mcp_server.py` | `vizlab` MCP server over stdio |
| `install.sh` | The installer friends run from their install link |
| `constraints.txt` | The exact dependency versions the installer pins to |
| `training/wakeword.py` | Trains the wake-word model (own venv, needs PyTorch) |
| `tests/` | `unittest` suite, no network, no bot |

## Run from source

```bash
cd vizbrain
python3.14 -m venv .venv && .venv/bin/pip install -c constraints.txt -e ".[mcp]"
.venv/bin/vizbrain setup        # name, city, models
.venv/bin/vizbrain set-key
.venv/bin/vizbrain serve
```

Needs Apple silicon, macOS 14 or newer, and Python 3.11 or newer (3.14 is what `constraints.txt` was made with).

Only one copy can serve port 4050. If the login agent is running, `vizbrain stop` first. A second `serve` prints who holds the port and exits.

### Tests

```bash
cd vizbrain && PYTHONPATH=. .venv/bin/python -m unittest discover -s tests
```

The suite stands in for the bot, launchd, Claude and the network, so it is safe to run next to a live brain.

## HTTP and UDP interface

| Route | Who may call it | What it does |
|---|---|---|
| `POST /v1/voice` | The paired bot | 16 kHz mono s16 WAV in, reply JSON out (`heard`, `text`, `bubble`, `expression`, `audio_id`) |
| `GET /v1/audio/<id>` | The paired bot | The reply clip, raw s16le mono PCM at 24 kHz, chunked while it is still being spoken |
| `POST /v1/event` | The paired bot | `{"type": "arrival" \| "lean_in"}` from the bot's senses |
| `POST /v1/text` | This Mac only | `{"text": ...}` typed turn; the bot speaks the reply |
| `POST /v1/say` | This Mac only | `{"text": ..., "voice"?: ...}` speak exact text, no model call |
| `GET /v1/health` | This Mac only | Status: version, key state, ears, bot, firmware, spend, last error |
| `GET /v1/log` | This Mac only | The last 50 turns (in memory) |
| `GET /` | This Mac only | The typing page |
| UDP `:4051` | The bot the brain already drives | The bot's idle mic stream for the wake word |

An error reply to the bot can carry `"say"`: firmware 3.7.31 and newer shows it as his speech bubble. The bot's side is in [`vizbot/README.md`](../vizbot/README.md) (`/brain/*`).

## Who may call what

`server.refusal()` decides, before any body is read.

- **Local routes** are served to loopback only. They also check `Host` (DNS rebinding), `Origin` (another site's page), and need `Content-Type: application/json` on POST, which forces a CORS preflight that the server never answers.
- **Bot routes** refuse anything carrying `Origin`, and once a bot is paired they need the pairing token in `X-Viz-Token`.

### Pairing

- `config.pairing()` creates `pairing.json` (`{"token": "<id>.<secret>", "paired": false}`, mode 600) on first use.
- `App.announce()` runs every minute. It gives the bot this Mac's address, the token (`/brain/config?token=`), and the wake setting, writing only what differs. The bot reports the public half of the token as `pairId` in `/brain/status`.
- Once the bot holds the token, `paired` turns true and the bot routes require it from then on.
- A call carrying the token moves the brain to the caller's address (the bot's IP changed). A call without it is only taken when no bot is known.
- To pair from scratch, delete `pairing.json` and restart.

Known limits, on purpose for a small trusted group:

- The token goes to the first Stackchan discovery finds (trust on first use). There is no "is this your bot?" step.
- The bot's own web routes have no auth. Anyone on the LAN can re-point it, take a photo or fetch `/brain/lastwav`.
- Bot-to-brain traffic is plain HTTP on the LAN.
- With firmware older than 3.7.31 the bot can't hold a token, so the bot routes stay open to the LAN until a newer bot pairs.

## Local Network permission

macOS blocks a login agent from devices on the LAN until the user allows it (Apple TN3179); calls fail with `[Errno 65] No route to host`. Terminal, SSH and launchd daemons are exempt, which is why everything works from Terminal and fails in the background.

A bare Python agent is filed under "Python", and with several Pythons installed that switch can be on and still not apply. So `install-service` writes a small `~/Applications/vizbrain.app` (bundle id `com.kpow.vizbrain`, with `NSLocalNetworkUsageDescription` and `NSBonjourServices`), signs it ad hoc, registers it, and names it in the agent's `AssociatedBundleIdentifiers`. macOS then asks once for "vizbrain" and keeps the answer. Opening the app shows the typing page. `doctor` warns when the agent isn't linked to the app, and fails when the background copy can't reach a bot that Terminal can.

## Turn lifecycle

1. The bot records until you stop talking (12 s at most) and POSTs the WAV.
2. whisper transcribes it. The user's name and any gear present are passed as hints.
3. `Brain.turn` streams Claude's reply. Faces and gestures ride in the text as `[face:x]` and `[gesture:x]` tags, so they cost no tool round trip.
4. Each finished sentence is synthesized and appended to one live clip. `/v1/voice` returns as soon as the first sentence is ready and the bot starts playing while the rest arrives.
5. Tool calls run between model rounds (6 at most). An action tool whose confirmation was already spoken ends the turn without another model call.

Prompt caching: the system prompt and tools are cached, and so is the conversation so far. Everything that changes per turn (time, weather, memory, notes) rides in a trailing system message so it never breaks the cached prefix.

Spend: `usage.record()` adds each response to `usage.json`. Past `daily_cap_usd` or `monthly_cap_usd` a turn is answered with a fixed sentence by local speech, and events and routines go quiet.

## Gear and tools

`Toolbox.api_list()` leaves out tools whose gear isn't there: `wled_set` without WLED devices, the keyboard and now-playing tools without vizMac, the Spotify tools without the Spotify app. The system prompt tells him to say he doesn't have it hooked up.

The MCP server exposes 9 of the tools to Claude Code:

```bash
claude mcp add vizlab -- "$(which vizbrain)" mcp
```

It needs the `mcp` extra, which the friend installer leaves out.

## Wake word

Not shipped to friends. The model lives outside the package, in `~/Library/Application Support/vizbrain/wakeword/models/` (`melspectrogram.onnx`, `embedding_model.onnx`, `hey_vizbot.onnx`). With `wake_enabled` true and the model present the brain listens on UDP :4051 and tells the bot to stream; otherwise it tells the bot not to.

Design and training notes: [`docs/plan-wake-word.md`](../docs/plan-wake-word.md). The current model was trained with one person's real clips. Note the trainer writes `wakeword/hey_vizbot.onnx` and the brain loads `wakeword/models/hey_vizbot.onnx`; copy it across after training.

## Logs and debugging

- `vizbrain doctor` first.
- Brain: `curl localhost:4050/v1/health`, `curl localhost:4050/v1/log`.
- Log: `~/Library/Logs/vizbrain/vizbrain.log` under the login agent, stdout otherwise. Lines are timestamped; the file rolls to `.log.1` at 5 MB.
- `"debug_log": true` in settings.json puts the words of each turn and every wake burst in the log.
- Bot: `curl http://<bot>/brain/status` (add `?trace=1` for per-chunk mic levels), `curl -o last.wav http://<bot>/brain/lastwav`, `curl http://<bot>/brain/listen` to start listening without touching him.

## How it gets to friends

Nothing is linked from the site and no GitHub release is published (the kpow.xyz downloads page lists every release in this repo).

1. A friend gets a link, `https://kpow.xyz/vb/<code>`, and pastes `bash -c "$(curl -sSL <link>)"`.
2. The kpow.xyz route (`server/routes/vizbrain-routes.ts` in the kpow_v3 repo) checks the code against the `VIZBRAIN_INVITES` environment variable. For a valid code it serves `vizbrain/install.sh` from this repo's `main` branch with the link written into it. For any other code it serves a one-line script that says the link isn't active.
3. `install.sh` installs `uv`, then vizbrain from the git tag `vizbrain-v$VERSION` at the versions in `constraints.txt`, then runs `setup`, `set-key`, `install-service` and `doctor`.
4. The link is saved in `~/Library/Application Support/vizbrain/install-link`, and `vizbrain update` goes back to it.

Codes are at least 16 letters, digits, dashes or underscores. Make one with:

```bash
python3 -c "import secrets; print(secrets.token_urlsafe(18))"
```

Add it to `VIZBRAIN_INVITES` (comma-separated) on the kpow.xyz server. Remove it to end that link. Never commit a code or the API key: both repos are public.

The source is public, so the link is an invitation, not a lock. What a stranger can't get is the API key.

## Release checklist

1. Bump `__version__` in `vizbrain/__init__.py` and `VERSION` in `install.sh` to the same number.
2. If the bot protocol changed, bump `MIN_FIRMWARE` in `bot.py` and the Versions table in the README.
3. If dependencies changed, regenerate `constraints.txt` from an environment you have actually run: `pip freeze --exclude-editable`.
4. Run the tests.
5. Merge to `main`, then tag and push: `git tag vizbrain-vX.Y.Z && git push origin vizbrain-vX.Y.Z`. The installer on `main` points at that tag, so push the tag before telling anyone to update.
6. Test the real thing in a fresh macOS user account with a real install link.

To try an unreleased build without a tag, point the installer at a local wheel:

```bash
uv build --wheel
VIZBRAIN_SOURCE="$PWD/dist/vizbrain-X.Y.Z-py3-none-any.whl" VIZBRAIN_CONSTRAINTS="$PWD/constraints.txt" bash install.sh
```

That runs the whole installer, including `install-service`, on the Mac you run it on.

## History

The original plan and build log are in [`docs/PLAN-vizbot-brain.md`](../docs/PLAN-vizbot-brain.md). It describes the 1.0 design; where it and this page disagree, this page is current.
