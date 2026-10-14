# Troubleshooting

Find what he said or did, then do the fix.

## Start here

Open Terminal and run the checkup:

```bash
vizbrain doctor
```

It checks the Mac, the key, the downloads, the bot and the connection between them. Any line marked `[FAIL]` has a `Fix:` line under it. Most problems end there.

Two fixes solve a lot:

- **Restart the brain:** `vizbrain restart`
- **Restart the bot:** hold the bottom button about 6 seconds to switch him off, then turn him back on.

## He doesn't answer

| What you see | Why | Fix |
|---|---|---|
| Tapping does nothing: no "Listening..." | You tapped the back or the middle. Or voice is switched off on the bot. | Tap the **front** of his head. If that doesn't work, open his web page and turn on **Voice** in the Brain card. |
| "Can't find my brain. Is vizbrain running on the Mac?" | vizbrain isn't running, or the bot has never been told where it is. | Run `vizbrain doctor`. If it says vizbrain isn't running, run `vizbrain install-service`. |
| The checkup says "Terminal can reach vizBot, but the running vizbrain can't" | macOS is blocking vizbrain from your local network. | Open **System Settings › Privacy & Security › Local Network** and turn on **vizbrain**, then run `vizbrain restart`. No vizbrain in that list? Run `vizbrain install-service`; it asks again. |
| "Can't reach my brain. Is the Mac awake and on this Wi-Fi?" | The Mac is asleep, off, or on a different network. Or the Mac's address changed. | Wake the Mac and check it's on the same Wi-Fi. He finds the new address within a minute; `vizbrain restart` makes it immediate. |
| "I'm not paired with this Mac yet. Try me again in a minute." | vizbrain was reinstalled, or just started. | Wait a minute and try again. If it stays, run `vizbrain restart`. |
| "I'm still getting my ears ready. Try me again in a minute." | vizbrain just started and is loading speech recognition. | Wait a minute. Right after installing, it may still be downloading. |
| "My hearing isn't working. On the Mac, run vizbrain doctor." | The speech recognition download is missing or damaged. | Run `vizbrain setup`, then `vizbrain restart`. |
| "Still thinking about the last one." | He handles one thing at a time. | Wait a few seconds and tap again. |
| "I didn't hear anything." | Nothing was said within a few seconds of "Listening...", or it was too quiet. | Wait for "Listening...", then speak at a normal volume within arm's reach. |
| "My brain had a problem. Check the vizbrain page on the Mac." | Something unexpected failed. | Run `vizbrain doctor`. If it finds nothing, see [Ask Kevin](#ask-kevin). |

## He answers, but something is wrong

| What he says or does | Why | Fix |
|---|---|---|
| "I don't have a Claude key yet..." | No key is saved on this Mac. | Run `vizbrain set-key` and paste the key Kevin sent. |
| "My Claude key needs a workspace ID." | The key Kevin sent is a personal key, and no workspace ID was entered. | Run `vizbrain set-key`, paste the key, then paste the workspace ID Kevin sent (it starts with `wrkspc_`). |
| "My Claude key isn't working. On the Mac, run vizbrain set-key." | The key was mistyped or has been switched off. | Run `vizbrain set-key` and paste it again. If it still fails, email Kevin for a new key. |
| "My Claude account is out of credit." | Kevin's account needs topping up. | Email Kevin. |
| "I've hit today's spending limit. I'll be back tomorrow." | He reached the daily limit. | Wait until tomorrow. The limit is in [Settings](settings.md). |
| "I'm being asked to slow down. Try me again in a minute." | Too many requests in a short time. | Wait a minute. |
| "Claude is busy right now. Try me again in a moment." | Claude's servers are overloaded. | Try again shortly. Nothing is wrong on your side. |
| "The Mac can't reach Claude right now." | The Mac has no internet. | Check the Mac's connection. If a VPN is on, try with it off. |
| His voice is a flat Mac voice | His own voice isn't downloaded. | Run `vizbrain setup`, then `vizbrain restart`. |
| The weather is for the wrong city, or he says no location is set | His location isn't set, or was set on the bot only. | Run `vizbrain setup` and answer the city question. |
| "I don't have that hooked up" when you ask for lights | He hasn't found any WLED lights. He looks again every 5 minutes. | Check the light is on the same Wi-Fi and shows in the WLED app, then wait 5 minutes or run `vizbrain restart`. |
| He can't find one light by name | He uses the name set in the WLED app. | Say that name, or rename the light in the WLED app. |
| Keyboard or "what's playing" requests fail | Those need vizMac, which only runs in Kevin's lab. | Nothing to fix. |
| "Play some jazz" does nothing | The Spotify desktop app isn't installed, or macOS blocked vizbrain from controlling it. | Install and open Spotify once. If macOS asks whether vizbrain may control Spotify, allow it. |
| He never greets you | Greeting needs 3 still minutes first, then waits 10 minutes between greetings. Chill mode turns it off. | Leave the room for a few minutes and come back. |
| His head doesn't move after an update | The bot needs a full power cycle after new firmware. | Hold the bottom button about 6 seconds to switch him off, then turn him on. |
| "Hey vizBot" does nothing | The wake word isn't included in this version. | Tap the front of his head. |
| He asks a question but doesn't listen for the answer | Bot firmware older than 3.7.33, or he already listened 3 times in a row. | Update his firmware, or tap to answer. |
| "The ... connector isn't running right now" or "didn't answer" | A connector's server stopped or is slow. vizbrain restarts it by itself. | Run `vizbrain connectors` to see it, and `vizbrain connectors test NAME` to start it and see the error. |
| A connector's tools never show up | It isn't in the list, or its entry is broken. | Run `vizbrain connectors`; it names the problem. `vizbrain doctor` also checks connectors. |
| Spotify: "needs Spotify's ... permission" | Playlists and likes need a one-time sign-in on the Mac. | Run `spotify-mcp login` and approve it in the browser. |
| Spotify: "No Spotify app is open to play on" | Nothing is open to play on. | Open Spotify on the Mac or your phone. |
| No album cover on his screen | Bot firmware older than 3.7.34. | Update his firmware. |

## Installing

| What the installer says | Fix |
|---|---|
| "This vizbrain install link isn't active." | Email Kevin for a new link. |
| "vizbrain needs a Mac with Apple silicon" | It won't run on an Intel Mac. |
| "This Terminal is running under Rosetta" | In Finder, select Terminal, choose File > Get Info, untick "Open using Rosetta", then open a new Terminal window. |
| "vizbrain needs macOS 14 (Sonoma) or newer" | Update macOS, then run your install line again. |
| "vizbrain needs about 4 GB of free space" | Free up space, then run your install line again. |
| "A download failed" | Check the internet connection, then run `vizbrain setup`. Finished downloads are kept. |
| "No vizBot found" | Check he's switched on and on the same Wi-Fi as the Mac, not a guest network. vizbrain keeps looking, so you can carry on. |
| "his firmware is ... and this brain needs 3.7.31 or newer" | Update the bot. See Step 1 in the [README](../README.md#step-1-update-the-bot). |
| `vizbrain: command not found` | Open a new Terminal window. The command is added for new windows. |

## Changing things

| What happened | Why | Fix |
|---|---|---|
| I changed a setting and nothing happened | Settings are read when vizbrain starts. | Run `vizbrain restart`. |
| I edited `settings.json` and now my settings are ignored | A typo makes the whole file unreadable. | Run `vizbrain doctor`; it names the problem. Fix the typo or delete the file. |
| "vizbrain is already running" | You started a second copy. The first one is still working. | Nothing to fix. To restart it: `vizbrain restart`. |

## Ask Kevin

If the fixes above don't do it:

1. Run this. It puts the report on your clipboard:

```bash
vizbrain doctor --copy
```

2. Paste it into an email to Kevin, and say what you asked vizBot and what he did.

The report contains no key and nothing you said to him. If Kevin asks for the log file, it is at `~/Library/Logs/vizbrain/vizbrain.log`. It holds timings and counts, not your words, unless you turned on `debug_log`.
