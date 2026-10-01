# vizbrain

The brain for vizBot. A small Python service on the Mac that listens to what you say to the Stackchan, thinks with Claude, answers in a spoken voice, and drives the lab (WLED lights, vizMac keyboard). Plan and interfaces: [`docs/PLAN-vizbot-brain.md`](../docs/PLAN-vizbot-brain.md).

## Run it

```bash
cd ~/projects/vizpow/vizbrain
python3 -m venv .venv && .venv/bin/pip install -e ".[mcp]"
.venv/bin/vizbrain serve
```

Open http://localhost:4050 to type to vizBot. Pat the **middle** of his head to talk (firmware v3.5.x and later).

The first start downloads the whisper model (~930 MB) from Hugging Face.

## Turn on Claude

Without an API key, vizbrain runs a tiny rule-based stand-in: it can tell the time, set light colors and say hello, so the voice pipeline is testable. To use Claude:

```bash
.venv/bin/vizbrain set-key
```

That stores the key in the macOS Keychain (service `vizbrain`). Then restart vizbrain. `ANTHROPIC_API_KEY` in the environment also works. The key is never written to a file.

## Run it at login (launchd)

```bash
mkdir -p ~/Library/Logs/vizbrain
cp launchd/com.kpow.vizbrain.plist ~/Library/LaunchAgents/
launchctl load ~/Library/LaunchAgents/com.kpow.vizbrain.plist
```

Logs: `~/Library/Logs/vizbrain/vizbrain.log`.

## Lab tools in Claude Code (MCP)

```bash
claude mcp add vizlab -- ~/projects/vizpow/vizbrain/.venv/bin/vizbrain mcp
```

Tools: `list_lab_devices`, `wled_set`, `keyboard_effect`, `keyboard_flash`, `bot_expression`, `bot_head`, `bot_gesture`, `bot_base_leds`, `bot_say`. Nothing can power off, reflash or re-network the bot.

## How it fits together

| Piece | What it does |
|---|---|
| `server.py` | HTTP API on :4050 (`/v1/voice`, `/v1/audio/<id>`, `/v1/text`, `/v1/health`, `/v1/log`), Bonjour `_vizbrain._tcp` |
| `brain.py` | Claude conversation (Sonnet 5.5, tool loop), or the offline stand-in |
| `speech.py` | whisper (mlx) to hear, macOS `say` to speak (24 kHz PCM), one voice per personality |
| `tools.py` | The tool registry shared by the brain and MCP |
| `bot.py`, `lab.py` | Clients for the vizBot, WLED and vizMac APIs |
| `mcp_server.py` | `vizlab` MCP server over stdio |

Settings: `~/Library/Application Support/vizbrain/settings.json` (only changed values are stored). Useful keys: `bot_host` (pin a bot IP), `model`, `wled_allow` (list of light names, or null for all), `tts_rate`.

## Debugging

- Brain: `curl localhost:4050/v1/health`, `curl localhost:4050/v1/log`
- Bot: `curl http://<bot>/brain/status` (add `?trace=1` for per-chunk mic levels), `curl -o last.wav http://<bot>/brain/lastwav` for the last raw recording, `curl http://<bot>/brain/listen` to start listening without touching him
