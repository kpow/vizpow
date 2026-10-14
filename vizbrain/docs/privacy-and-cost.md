# Privacy and cost

What leaves your house when you talk to vizBot, what stays, and who pays. Worth sharing with anyone who lives with him.

## The short version

- **Your voice stays in your home.** Speech is turned into text on your Mac. The audio is never sent to Claude.
- **The text of what you say goes to Claude,** along with what he remembers about you, so he can answer.
- **His camera takes a photo when you ask him to look, and when someone walks in.** Those photos go to Claude.
- **Kevin pays for it.** Each answer costs him about 1 to 3 cents.

## Stays on your Mac

- **The sound of your voice.** The bot sends the recording to your Mac over your Wi-Fi. Your Mac turns it into text and discards the audio.
- **His voice.** It is made on your Mac.
- **His memory and routines.** They are files on your Mac that you can open, edit or delete. See [Settings](settings.md#his-memory-and-routines).
- **The log.** It records when things happened and how long they took. It does not record what was said, unless you turn on `debug_log`.
- **The last 50 exchanges,** shown on his page at `http://localhost:4050`. They are held in memory and gone when vizbrain restarts. That page only opens on the Mac itself, not from other devices on your Wi-Fi.

## Goes to Anthropic (Claude)

Every time he answers, your Mac sends Claude:

- The text of what he heard.
- The last 10 or so exchanges of the current conversation.
- Everything he has been asked to remember, and his list of routines.
- The local time, the weather, and the names of any lights he controls.
- Your first name.

And sometimes:

- **A photo,** when you ask him what he sees.
- **A photo when someone walks in.** After the room has been still for 3 minutes, movement makes him take one to check a person is really there before he says hello. He takes up to three, a few seconds apart. There is no shutter sound for these.
- **Your web search questions,** with your city so the results are local.

Anthropic handles what it receives under its own privacy policy. The requests are made with Kevin's API key, so they are made under his account.

## Goes elsewhere

- **Open-Meteo** (a weather service) gets your latitude and longitude when he checks the weather, at most every 15 minutes. It doesn't get your name.
- **Hugging Face and GitHub** are contacted during install to download the program and the speech recognition and voice models.
- **kpow.xyz** (Kevin's site) is contacted when you install or update, to fetch the installer from your link.

## The bot checks in with Kevin's server

This is part of the bot's firmware, not the brain, and it happens with or without vizbrain. Every vizBot checks in with **vizCloud**, a server Kevin runs to send bots new personalities and content.

- **The bot sends:** its hardware address and type, its firmware version, its address on your home network, the face and personality it's showing, its Wi-Fi signal strength and how long it has been on.
- **Kevin's server can tell the bot to:** change face or personality, show a speech bubble, play a sound, change brightness, background or volume, sleep, or restart.
- **It cannot** use the camera or the microphone, and it never receives what you say.

There is no switch for this yet. If you want it off, tell Kevin.

## On your Wi-Fi

- The bot and your Mac talk over your home network without encryption. Anyone already on your Wi-Fi with the right tools could listen in.
- The brain only accepts voice from the bot it is paired with.
- **The bot's own web page has no password.** Anyone on your Wi-Fi can open it, change his settings, take a photo with his camera, or download his last recording. Only put him on a network you trust.

## How to turn things off

| To stop this | Do this |
|---|---|
| Walk-in photos and greetings, for a while | Hold your hand on his head for 2 seconds (chill mode). |
| Web searches | Set `"web_search": false` in [settings](settings.md#change-other-settings). |
| Everything, for now | Turn off **Voice** in the Brain card on his web page, or run `vizbrain stop`. |
| A remembered fact | "Forget about ...", or edit `memory.json`. |

There is no setting yet to turn walk-in photos off for good. If you want that, tell Kevin.

## The key

- The Claude API key is Kevin's. It is kept in your Mac's Keychain, not in a file.
- Kevin can see how much has been used and what it cost.
- Don't share the key or your install link. Anyone with the key can spend Kevin's money.

## Cost

You pay nothing. Kevin's account is charged for each answer.

| What | Rough cost to Kevin |
|---|---|
| One answer | 1 to 3 cents |
| A web search | About 1 cent more |
| A walk-in check | Under 1 cent |
| A day of normal chatting | About 15 cents |

These come from Kevin's own use over two weeks: 282 requests cost $3.22. A typical day was 15 cents and his busiest was 71 cents.

Two limits stop surprises: he stops answering at **$2 in a day** or **$20 in a month** and says so out loud. The running total shows at the top of `http://localhost:4050`. It is an estimate made on your Mac from list prices, so Kevin's actual bill can differ slightly.
