# vizbrain runbook (for Kevin)

Step-by-step instructions for everything you do as the owner: turning on install links, inviting a friend, testing, taking access away, and releasing updates. Friends never need this page; theirs starts at the [README](README.md).

Nothing secret belongs in this file. This repo is public. Codes and the API key live only in DigitalOcean, your Keychain, and your messages to friends.

## How it fits together

```
friend's Terminal ── bash -c "$(curl -sSL https://kpow.xyz/vb/<code>)"
        │
        ▼
kpow.xyz  (kpow_v3: server/routes/vizbrain-routes.ts)
        │  is <code> listed in VIZBRAIN_INVITES?  no → "link isn't active"
        ▼  yes
vizbrain/install.sh from this repo's main branch, with the link written in
        │
        ▼
installs vizbrain from the git tag vizbrain-vX.Y.Z, asks name, city and API key,
starts it at login, runs vizbrain doctor
```

- **Install links** are `https://kpow.xyz/vb/<code>`. The codes live in one DigitalOcean setting, `VIZBRAIN_INVITES`.
- **The API key** is yours, and every friend uses the same one. You send it to them yourself.
- **Docs are public** on GitHub and tell friends to email you for a link.

## 1. Turn on install links (one time)

Do this once. After it, adding a friend is section 2.

**1a. Make a code.** In Terminal:

```bash
python3 -c "import secrets; print(secrets.token_urlsafe(18))"
```

It prints something like `x7Rk2-...`, 24 characters. That is the code.

**1b. Add it to DigitalOcean.**

1. Go to [cloud.digitalocean.com](https://cloud.digitalocean.com) and sign in.
2. Click **Apps** in the left sidebar, then the app that runs kpow.xyz.
3. Click the **Settings** tab.
4. Find **App-Level Environment Variables** and click **Edit**.
5. Click **+ Add Variable**.
   - **Key:** `VIZBRAIN_INVITES`
   - **Value:** the code from 1a
   - Tick **Encrypt**.
6. Click **Save**. DigitalOcean redeploys the site, which takes a few minutes. The **Activity** tab shows it finishing.

Don't see **App-Level Environment Variables**? Open the web service under **Components** instead; it has its own **Environment Variables** section that works the same way.

**1c. Check it works.** Open these two addresses in a browser, with your code in the first:

- `https://kpow.xyz/vb/<your code>` shows plain text starting with `#!/bin/bash`. That's the installer. It works.
- `https://kpow.xyz/vb/wrong-code-wrong-code` shows "This vizbrain install link isn't active. Email Kevin for a new one."

If the first one shows the normal kpow.xyz page instead, the redeploy hasn't finished. Wait a minute and reload.

## 2. Invite a friend

1. **Check they can run it.** They need a Mac with Apple silicon (M1 or newer) on macOS 14 or newer, and a Stackchan vizBot.
2. **Give them a code.** Either reuse one you already made, or make a new one (1a) and add it to `VIZBRAIN_INVITES` after a comma: `firstcode,secondcode`. One code per friend means you can turn off one person without touching the other. Saving redeploys the site.
3. **Send two separate messages.** Text or Signal is better than email for the key.

Message 1, the link and the guide:

> Here's your vizBot brain. Start here: https://github.com/kpow/vizpow/blob/main/vizbrain/README.md
>
> When it says to paste the install line, use this:
>
> `bash -c "$(curl -sSL https://kpow.xyz/vb/<their code>)"`
>
> I'll send the Claude key separately. Keep both to yourself.

Message 2, the key: your Anthropic API key, on its own. If it's a personal key (it starts `sk-ant-usr`), add the workspace ID on its own line too; the installer asks for it right after the key. A normal API key made in the Console (`sk-ant-api…`) needs no workspace ID, which makes it the simpler one to hand out.

To copy the key from your own Mac without showing it: `security find-generic-password -s vizbrain -a anthropic -w | pbcopy`. Your workspace ID is `workspace_id` in `~/Library/Application Support/vizbrain/settings.json`.

4. **Make sure their bot has firmware 3.7.31 or newer.** The README walks them through updating it.
5. **When they get stuck,** ask them to run `vizbrain doctor --copy` and paste the result to you. Their [troubleshooting page](docs/troubleshooting.md) covers most things first.

## 3. Test a release the way a friend would

Do this after setting up links (section 1), and again before telling friends about a new version. It takes about 20 minutes.

Your own vizbrain uses port 4050, and only one brain can have it. So switch yours off for the test and back on after.

**3a. Make a test account** (while logged in as yourself):

1. Open **System Settings › Users & Groups**.
2. Click **Add User…** and enter your Mac password.
3. Choose **Standard**, name it `vizbrain test`, give it any password, and click **Create User**.

**3b. Hand the bot over to the test account:**

1. Copy the API key somewhere you can reach from the other account, like a note on your phone. You'll paste it during the test.
2. Stop your own vizbrain:

```bash
launchctl bootout gui/$(id -u)/com.kpow.vizbrain
```

3. **Log out:** Apple menu › Log Out. Don't use "switch user"; your account would keep running.
4. Log in as **vizbrain test**. Skip every setup screen you can.

**3c. Install as a friend would.**

1. Open Safari and go to the README: https://github.com/kpow/vizpow/blob/main/vizbrain/README.md. Follow it exactly as written, including the parts that feel obvious. Note anything confusing.
2. At Step 2, open Terminal and paste the install line with a code from `VIZBRAIN_INVITES`.
3. Answer the questions and paste the key when asked.
4. If macOS asks about finding devices on the local network, click **Allow**. Note that it asked.
5. Read the checkup at the end. Every line should start with `[ OK ]` or `[WARN]`.
6. Tap the front of the bot's head and talk to him.

Things to write down: total time, any step where you had to guess, any `[FAIL]`, and anything the bot said that wasn't an answer.

**3d. Put things back.**

1. Log out of **vizbrain test**, and log in as yourself.
2. Start your vizbrain again:

```bash
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.kpow.vizbrain.plist
```

3. Delete the test account: **System Settings › Users & Groups**, click the ⓘ next to **vizbrain test**, then **Delete User…** and choose to delete its home folder.
4. Your bot reconnects to your brain by itself within a minute. Nothing else to do.

## 4. Take access away

| You want to | Do this |
|---|---|
| Stop new installs from one link | Remove that code from `VIZBRAIN_INVITES` and save. Their copy keeps working, but `vizbrain update` stops. |
| Stop someone using Claude at all | Make a new API key in the [Claude Console](https://platform.claude.com/settings/keys) and delete the old one. Send the new key to everyone else; they run `vizbrain set-key`. Everyone shares one key, so this is the only way. |
| Stop everything | Delete the key in the Claude Console. Every brain falls back to its few built-in replies. |

## 5. Keep an eye on cost

- **Real spend:** the [Claude Console](https://platform.claude.com) usage page. Every friend uses your key, so it is all in one place.
- **Per-install limits:** each brain stops at $2 a day and $20 a month and says so out loud. Friends can change this in their settings file, so set a monthly spend limit in the Console as the real backstop.
- **Each brain's own estimate** is at the top of `http://localhost:4050` on its Mac.

## 6. Release a vizbrain update

Full checklist: [DEVELOPING.md › Release checklist](DEVELOPING.md#release-checklist). In short:

1. Bump the version in `vizbrain/vizbrain/__init__.py` and `VERSION=` in `vizbrain/install.sh` to the same number.
2. Merge to `main`.
3. Tag and push: `git tag vizbrain-vX.Y.Z && git push origin vizbrain-vX.Y.Z`. Until the tag exists, new installs fail, because `install.sh` on `main` already points at it.
4. Test it (section 3).
5. Tell friends to run `vizbrain update`.

If the update needs newer bot firmware, release the firmware first (section 7) and bump `MIN_FIRMWARE` in `vizbrain/vizbrain/bot.py`.

## 7. Release bot firmware

Same as always: merge, build the four boards with `~/.platformio/penv/bin/pio`, `gh release create`, then the kpow_v3 guide PR. One extra check before publishing: every `-factory.bin` must start with `e903024f`:

```bash
for f in builds/*-factory.bin; do echo "$(xxd -p -l 4 $f)  $f"; done
```

`e903004f` means the old header bug is back; don't publish that file.

## 8. Move your own Mac to the new vizbrain

Your own vizbrain still runs the 1.0 code from your main checkout. When you want 1.1.0:

1. In `~/projects/vizpow`, throw away the uncommitted `vizbot/name_firmware.py` edit; that fix is already on `main`. Look at the `vizbot/config.h` edit before deciding about it.
2. `git switch main && git pull`
3. `cd vizbrain && .venv/bin/pip install -e .`
4. `launchctl kickstart -k gui/$(id -u)/com.kpow.vizbrain`
5. `.venv/bin/vizbrain setup` to give him your city, then `.venv/bin/vizbrain doctor`.

Two changes you'll notice: the log no longer shows what was said (set `"debug_log": true` in settings.json to get it back), and web search uses the city from setup.

## 9. If a friend says he's too quiet

The Stackchan can't do Bluetooth audio (the ESP32-S3 has no Classic Bluetooth), so the louder option runs through the Mac. On the bot's page, **Brain (voice)** › **Talk through the Mac's speaker** sets the vizbrain `speaker` setting to `"mac"` (`"bot"` is the default). vizbrain then plays each reply on the Mac's sound output, such as a Bluetooth speaker paired to the Mac, and the bot mouths it with his own speaker muted. Needs vizbrain 1.3.0 and bot firmware 3.7.37. If the switch says "Mac speaker not yet", the message names what's missing. Details for friends: [Settings › Make him louder](docs/settings.md#make-him-louder).

## Where everything lives

| What | Where |
|---|---|
| The install-link route | kpow_v3, `server/routes/vizbrain-routes.ts` |
| Invite codes | DigitalOcean › your app › Settings › `VIZBRAIN_INVITES` |
| The installer | this repo, `vizbrain/install.sh` on `main` |
| The version friends get | the git tag `vizbrain-vX.Y.Z` |
| Friend docs | `vizbrain/README.md` and `vizbrain/docs/` |
| How it works inside | [DEVELOPING.md](DEVELOPING.md) |
