# Settings and upkeep

How to change what vizBot does, keep him up to date, and remove him.

## The commands

Open Terminal and type any of these.

| Command | What it does |
|---|---|
| `vizbrain doctor` | Checks everything and prints a report. Add `--copy` to put it on the clipboard. |
| `vizbrain setup` | Asks your name and city again, and re-downloads anything missing. |
| `vizbrain set-key` | Saves a new Claude API key. |
| `vizbrain restart` | Restarts the brain. Do this after changing a setting. |
| `vizbrain stop` | Stops the brain until you next log in. |
| `vizbrain install-service` | Starts the brain and makes it start at login. |
| `vizbrain update` | Installs the newest version. |
| `vizbrain uninstall` | Removes vizbrain. See [Uninstall](#uninstall). |
| `vizbrain --version` | Shows the installed version. |

## Change your name or city

Run `vizbrain setup` and answer the questions again. Press Return to keep an answer as it is.

## Change other settings

Other settings live in one file:

```
~/Library/Application Support/vizbrain/settings.json
```

The file only holds what you've changed. If it doesn't exist yet, create it. To open the folder, run:

```bash
open ~/Library/Application\ Support/vizbrain
```

A complete example:

```json
{
  "user_name": "Dana",
  "daily_cap_usd": 1.0,
  "web_search": false
}
```

Then run `vizbrain restart`. A typo makes the whole file unreadable and he goes back to the defaults; `vizbrain doctor` tells you if that happened.

### Settings you might change

| Setting | What it does | Default | Example |
|---|---|---|---|
| `user_name` | What he calls you. | The first name on your Mac account | `"Dana"` |
| `location` | Where he is, for weather and local answers. Set it with `vizbrain setup`. | Your Mac's timezone only | (set by setup) |
| `daily_cap_usd` | Most he can spend on Claude in a day, in dollars. `0` means no limit. | `2.0` | `1.0` |
| `monthly_cap_usd` | Most he can spend in a calendar month. `0` means no limit. | `20.0` | `10.0` |
| `web_search` | Whether he can look things up online. | `true` | `false` |
| `tts_voice` | His voice. Empty means his usual one. | `""` | `"kokoro:af_heart@1.0"` or a Mac voice such as `"Samantha"` |
| `wled_allow` | Which WLED lights he may control, by name. `null` means all he finds. | `null` | `["Desk Lamp", "Shelf"]` |
| `bot_host` | The bot's address, if he can't be found automatically. | `""` (find him) | `"192.168.1.80"` |
| `debug_log` | Write what was heard and said into the log file. | `false` | `true` |

The spending limits protect Kevin's account. Please don't raise them without asking him.

### Settings to leave alone

| Setting | What it is |
|---|---|
| `port` | The port vizbrain listens on (4050). The bot expects it. |
| `model`, `event_model` | Which Claude models he uses. |
| `effort` | How hard Claude thinks before answering. Higher is slower. |
| `history_turns` | How many recent exchanges he keeps in mind (10). |
| `session_idle_s` | How long a quiet gap ends a conversation (300 seconds). |
| `stt_model` | The speech recognition model. |
| `tts_engine`, `tts_rate` | Voice engine, and speed for Mac voices only. |
| `web_search_max_uses` | Searches per answer (2). |
| `wake_enabled`, `wake_threshold` | For "Hey vizBot", which isn't included in this version. |
| `vizmac_url`, `workspace_id` | Used in Kevin's lab only. |

### Settings that live on the bot

Open vizBot's own web page for these:

- **Personality:** chill, hyper or grumpy.
- **Voice on or off:** the Voice switch in the Brain card.
- **Screen, effects and brightness.**

## His memory and routines

Two files sit next to `settings.json`:

- `memory.json`: the facts he was asked to remember.
- `routines.json`: the things he does on a schedule.

You can edit or delete either one by hand. He picks up changes without a restart.

## Update

```bash
vizbrain update
```

This installs the newest version and restarts him. Your settings, his memory and the key are kept. If it says your install link isn't active, email Kevin for a new one.

A new vizbrain sometimes needs newer bot firmware. `vizbrain doctor` says so if it does. The bot's update steps are in the [README](../README.md#step-1-update-the-bot).

## Uninstall

To remove the program but keep your settings and his memory:

```bash
vizbrain uninstall
```

To remove everything, including the key, settings, memory, logs and downloaded models:

```bash
vizbrain uninstall --purge
```

It lists what will be deleted and asks you to type `yes` first.

Two things are left behind either way:

- **uv**, the small tool the installer used. Remove it with `rm ~/.local/bin/uv ~/.local/bin/uvx`.
- **The bot still points at this Mac.** That's harmless. He'll show "Can't reach my brain" when tapped until another brain is installed.

## Where everything is

| What | Where |
|---|---|
| Settings, memory, routines, spend totals | `~/Library/Application Support/vizbrain/` |
| His voice model | `~/Library/Application Support/vizbrain/kokoro/` |
| Speech recognition model | `~/.cache/huggingface/hub/` |
| Log | `~/Library/Logs/vizbrain/vizbrain.log` |
| The Claude API key | Keychain Access, under the name `vizbrain` |
| Start-at-login entry | `~/Library/LaunchAgents/com.kpow.vizbrain.plist` |
| The vizbrain app (opens his page; macOS grants it network access) | `~/Applications/vizbrain.app` |
| The program | `~/.local/share/uv/tools/vizbrain/` |
