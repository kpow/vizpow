# vizbrain

vizBot is the body: a face, a head that moves, a microphone and a speaker. vizbrain is his brain: a small program on your Mac that hears what you say to him, thinks with Claude, and answers out loud in his own voice.

This page takes you from a bot on your desk to your first conversation. Plan on about 20 minutes, most of it waiting on downloads.

## Before you start

You need all five:

- **A Mac with Apple silicon** (M1 or newer) running macOS 14 or newer, with about 4 GB of free space.
- **A Stackchan vizBot**, switched on and joined to your home Wi-Fi. ([How to join him to Wi-Fi](../README.md#connecting).)
- **The same Wi-Fi for both.** The Mac and the bot must be on the same network, and not on a guest network.
- **Your install link.** Email Kevin for it. The link is yours, so don't pass it on.
- **A Claude API key.** Kevin sends this too. You'll paste it once during install.

## Step 1: Update the bot

vizbrain needs bot firmware **3.7.31 or newer**.

1. Open vizBot's web page. On your home Wi-Fi it is at `http://vizbot-xxxx.local`, where `xxxx` is the code in his name (vizBot-XXXX).
2. Find the **Firmware** line. If it shows 3.7.31 or newer, skip to Step 2.
3. Download the newest **Stackchan** file from [kpow.xyz/vizbot](https://kpow.xyz/vizbot).
4. On his web page, press **Update** and choose that file.
5. When it finishes, switch him fully off and on: hold the bottom button for about 6 seconds, then turn him back on. Skip this and his head won't move.

## Step 2: Install the brain

1. Open **Terminal** (press Cmd+Space, type `Terminal`, press Return).
2. Paste this line, with your own link in place of `<your install link>`, and press Return:

```bash
bash -c "$(curl -sSL <your install link>)"
```

The installer downloads about 1.2 GB in total, so the time depends on your connection. It never asks for your Mac password.

If it says the link isn't active, email Kevin for a new one.

## Step 3: Answer three things

The installer stops and asks:

1. **What should vizBot call you?** Press Return to accept the name it suggests.
2. **What city are you in?** This is for the weather and local answers. Type the city and pick it from the list.
3. **The Claude API key.** Paste the key Kevin sent and press Return. Nothing appears as you paste; that's normal.

The key is stored in your Mac's Keychain. Don't post it or share it: anyone who has it can spend Kevin's money.

Near the end, macOS asks whether **vizbrain** may find devices on your local network. Choose **Allow**. Without it the brain can't reach the bot.

## Step 4: Read the checkup

The installer finishes by checking everything and printing a report. Every line should start with `[ OK ]`. A line that starts with `[FAIL]` has a `Fix:` line under it that says what to do.

It then opens vizBot's page on your Mac, [http://localhost:4050](http://localhost:4050). The top line should read "Claude: online" and show the bot's address.

## Step 5: Say hello

1. Tap the **front** of his head.
2. Wait for "Listening..." on his screen.
3. Say something. Try one of these:
   - "What's the weather like?"
   - "Remember that my dog's name is Biscuit."
   - "Look up and make your ring purple."
4. To stop him, tap the front of his head again.

He starts answering a second or two after you stop talking.

vizbrain now starts by itself every time you log in to your Mac. There is nothing to launch.

## If something's off

Run the checkup again any time. Open Terminal and type:

```bash
vizbrain doctor
```

Then look up what he said or did in [Troubleshooting](docs/troubleshooting.md).

## More

- [Things to say](docs/things-to-say.md): everything he can do, with an example for each.
- [Troubleshooting](docs/troubleshooting.md): find the symptom, get the fix.
- [Settings and upkeep](docs/settings.md): change his voice or location, update, uninstall.
- [Privacy and cost](docs/privacy-and-cost.md): what leaves your house, and who pays.
- [Developing](DEVELOPING.md): how it works inside, for people changing the code.

## Versions

| vizbrain | Needs bot firmware |
|---|---|
| 1.1.1 | 3.7.31 or newer |
| 1.1.0 | 3.7.31 or newer |
