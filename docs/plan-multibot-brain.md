# Multi-bot vizbrain: every bot, its own name, its own wake word

**Status: Draft for Kevin's approval, 2026-11-27**

## 1. Summary

Today vizbrain follows one bot. skunky's "Hey vizBot" was heard on the Mac and then dropped, because mac was the adopted bot (`server.py:205`). This plan turns vizbrain into a small household hub:
- every paired bot can wake it and talk;
- each bot can get its own name ("Hey Skunky");
- a training service makes that wake word, on the Mac or hosted.

**Stage A1 fixes today's bug with one small Mac change and no firmware change.** Everything after it is small, separate PRs. vizbrain keeps working with no internet. vizCloud is reused for fleet admin and, at the very end, hosted training.

## 2. Outcomes

| # | Outcome | How the design delivers it | Stages |
|---|---|---|---|
| 1 | Every bot on the LAN can talk to vizbrain, several at once | Roster of paired bots; wake accepted from any of them; then per-bot sessions with one turn per bot and at most 2 at once | A1, C2 |
| 2 | A bot trained to its own name ("Hey Skunky") | Several wake models per bot; a bot's own model runs only on its own mic stream | D1, D2 |
| 3 | A service to train a bot's wake word, local or hosted | `vizbrain wake train` on the Mac; hosted = vizCloud stores jobs + models, Kevin's Mac does the compute | D2-D4, E1-E3 |
| 4 | All the configuration for this | `bots.json` per bot, new `settings.json` keys, 2 new NVS keys, loopback routes + CLI | B3, C3, D1 |
| a | Two bots hear the same wake word | SNR (from the BEAT noise floor) first, then score, in a 350 ms window; only the winner wakes | C1 |
| b | Concurrent turns, not one global busy lock | One turn per bot, global cap `max_parallel_turns: 2`, Mac speaker used by one bot at a time | C2 |
| c | Per-bot pairing tokens + revoke | `bots.json` keyed by uid; per-bot token; token changes on the bot need proof; `vizbrain bots revoke` | B2, B3, B4 |
| d | Per-bot identity: name, voice, persona, speaker, memory | Name from the bot, persona from vizCloud, voice/speaker/persona note/memory scope per bot in vizbrain | C3 |
| e | A roster: online, firmware, wake model | `vizbrain bots`, `GET /v1/bots`, `/v1/health.bots[]`; vizCloud admin shows the same from bot sync | A1, B3, D1 |

Also covered: friends' privacy (no raw audio leaves the house), migration of single-bot installs (B3), false-wake retraining (D4).

## 3. Architecture

```
  StackChan bots (only VOICE_ENABLED build)          vizCloud (optional, internet)
  +-----------+  +-----------+                       +-----------------------------+
  |   mac     |  |  skunky   |  ...                  | bots: identity, persona,    |
  | uid=MAC   |  | uid=MAC   |                       |   fw, v2 sync state         |
  | brainTok  |  | brainTok  |  -- v2 sync --------> | brains + wake_jobs (E1)     |
  +-----------+  +-----------+                       +--------------^--------------+
     |  ^   UDP :4051 VZW1 (BEATs + bursts)                         | jobs/models
     |  |   HTTP :4050 /v1/* (X-Viz-Token = that bot's token)       | (E2, E3)
     v  |   HTTP :80 /brain/* (brain sends X-Viz-Token)             |
  +-------------------------------- vizbrain (Mac, LAN, works offline) -----------+
  |  Roster / bots.json (uid -> token, host, name, fw, settings)                   |
  |      |                                                                         |
  |  WakeReceiver (per host) -> FeaturePipeline (shared) -> classifiers per bot    |
  |      |   hey_vizbot (shared)  +  hey_skunky (skunky only)                      |
  |      v                                                                         |
  |  Arbiter: shared word -> 350 ms window, SNR then score; echo guard             |
  |      |  winner: GET /brain/wake                                                |
  |      v                                                                         |
  |  BotSession per uid: conversation, follow-ups, busy, speaking_until            |
  |      |  global cap 2 turns, Mac speaker exclusive, memory shared or own        |
  |      v                                                                         |
  |  Claude + tools (bot passed explicitly)   TTS/STT locks (shared)               |
  |                                                                                |
  |  Training: studio (per bot) -> wakeword.py --phrase -> gate -> model store     |
  |            local `nice` process, or hosted via vizCloud worker                 |
  +--------------------------------------------------------------------------------+
```

## 4. Interface contract (the one agreed version)

### Ownership: one writable copy of each

| Thing | Owner | Others read it via |
|---|---|---|
| name (`deviceName`), uid (`WiFi.macAddress()`) | bot | `/state`, `/brain/status`, v2 sync |
| persona (Chill/Hyper/Grumpy), fleet approval | vizCloud | v2 sync config to the bot; brain reads `/state` |
| pairing tokens, voice, speaker, persona note, memory scope, wake models, threshold, conversation | vizbrain | `/brain/config` (wakeWord, display only) |
| hosted training jobs + model store | vizCloud (stage E1 only) | brain pulls |

### Bot -> brain (vizbrain :4050)
- Unchanged: `/v1/voice`, `/v1/event`, `/v1/audio/<id>`, `/v1/listen/<id>`, `GET/POST /v1/voices`, `/v1/hush`. Headers stay `X-Bot-Id` (ignored) and `X-Viz-Token`. The token identifies the caller.
- No `/v1/hello`. No `X-Bot-Uid/Name/Fw`. No `X-Viz-Wake`, no `wake_outcome` event.
- `403 {"code":"not_paired","error":"not paired with this brain","say":...}` for an unknown or revoked token. Firmware matches `code`, or the released `error` text.
- `503 {"code":"busy","error":"brain busy","say":...}` only when the global turn cap is full.

### Brain -> bot (bot :80; `X-Viz-Token: <that bot's token>` on every call once the bot holds one)
- `GET /brain/status` stays open. Keeps every field it has today, **including top-level `pairId`** (the released brain reads it). Adds:
  - `uid`: `WiFi.macAddress()` (`AA:BB:CC:DD:EE:FF`); the brain lowercases and strips colons to compare
  - `botId` (cloud, may be `""`), `name`, `fw`, `board:"stackchan"`, `caps:{mic,speaker,camera,servos}`
  - `pair:{strict, refused, linkUp, authAgoS}`
  - `wake.phrase`
  - `diag:{udpFailStep, udpErrno, httpFailStep, httpErr, failsWhileCloudHeld}`
- `GET /brain/config?host=&token=&enabled=&wake=&forget=1&strict=0|1&wakeWord=<=31`
  - `token`: accepted only into an empty slot, or with proof of the current token. Else `403 {"error":"token mismatch"}`.
  - `forget=1` with proof: clears token, host and strict in one step. Without proof (bot page): `202 {"ok":true,"confirm":"hold"}`, then a head hold within 30 s confirms.
  - Strict on: `host`, `wake=1`, `enabled=1`, `wakeWord`, `token`, `strict` need proof. `enabled=0` and `wake=0` stay open.
  - Strict off: `host` and the switches behave as today (the released brain pushes `host` without a header after a Mac IP change).
  - Released firmware ignores unknown args, so new params are always safe to send.
- `GET /brain/wake`, `GET /brain/play`: 403 without proof when strict; otherwise 200 or 409 as today. `/brain/play` never adopts its caller while a token is held.
- `GET /brain/lastwav`: always needs proof. `/brain/listen`: open (page button and interaction suite).
- **Link rule:** the bot's link is UP when it had authenticated contact in the last 10 min (a call carrying its token, or a 2xx on its own brain call).
- **Audio gate:** a burst may open only if `pairToken[0] && !pairRefused && (!brainStrict || linkUp)`. Otherwise BEATs only.

### UDP :4051
- `VZW1 | seq u32 | flags u8 | pad[3]`, 12 bytes, unchanged. Flags: START 0x01, END 0x02, BEAT 0x04 (BEAT payload: noise u16). Pad bytes stay zero and are ignored.
- UDP carries no authority. Source IP maps to a bot through announce. Packets from unrostered IPs never reach the detector.
- A packet from a paired IP not announced to in the last 60 s triggers an immediate announce (max once per 10 s per IP). Unknown IPs get a `/state` + `/brain/status` probe, max once a minute.
- Not built: VZW2, pad-byte noise or take ids, CAPTURE flag, mDNS TXT.

### vizbrain `settings.json` (new keys)

| Key | Default | Meaning |
|---|---|---|
| `max_parallel_turns` | 2 | global cap on overlapping turns |
| `auto_pair` | true | mint a token only for StackChans reporting an empty `pairId` |
| `strict_bots` | false | announce sends `strict=1` |
| `wake_arbitration_ms` | 350 | shared-word window |
| `shared_wake_model` | `"hey_vizbot"` | the house-wide word |
| `wake_keep_false` | false | keep false-wake clips for retraining |
| `cloud_url` | `""` | hosted training; brain token in Keychain `vizcloud-brain` |

`tts_voice`, `speaker`, `wake_threshold` become per-bot defaults. `bot_host` = "always include this address", and picks who keeps the shared token in migration.

### `bots.json` (0600, plaintext tokens like `pairing.json` today)
```
{"version":1, "primary":"<uid>", "bots":{"<uid>":{
  "token","name","host","firmware","cloud_bot_id","paired_at","revoked",
  "settings":{"tts_voice","speaker","persona_note","memory":"shared|own",
              "wake_models":[...],"wake_threshold","shared_wake":true}}}}
```
- Key = normalized uid. Released firmware without `uid`: `host:<hostname>`, re-keyed once uid appears.
- Incoming tokens: pairId prefix -> record -> `hmac.compare_digest`. Tokens never logged.

### Local routes (loopback only, existing Host/Origin/JSON checks)
- `GET /v1/bots`, `POST /v1/bots/<uid>`, `POST /v1/bots/<uid>/revoke`, `POST /v1/bots/<uid>/claim`
- `/v1/text`, `/v1/say` take `"bot"`; `/v1/health` adds `bots[]`, keeps `bot`
- `GET /studio?bot=`, `POST /v1/studio/{record,skip,back}`, `GET /v1/studio/state`
- `POST /v1/wake/train {bot,phrase,where,use_recordings}`, `GET /v1/wake/jobs`
- CLI: `vizbrain bots [set|primary|claim|revoke|unstrict]`, `vizbrain wake [models|assign|studio|setup-trainer|train|retrain|worker]`

### NVS (namespace `vizbot`, firmware-owned)
- Kept: `brainHost`, `brainTok`, `voiceOn`, `wakeOn`, `devName`
- New: `brainStrict` (bool), `wakeWord` (str <=31, display only)
- uid is derived, never stored.

### vizCloud
- Bot sync state, additive: `wakePhrase` (<=31), `brainPaired` (bool). No new config keys. v1 untouched.
- Hosted training only (E1): migration `015_brains_and_wake_jobs.sql`; `brains` reuses the v2 token pattern (32 random bytes, sha256 stored, `approved|pending|disabled`, never 401); `wake_jobs` holds phrase, status, report, optional embeddings, onnx + sha256. Brain API `X-Brain-Token`, worker API `X-Worker-Token`.

## 5. Guard rails (every later extension stays inside these)

**Ownership and offline**
1. Exactly one writable copy of each thing (table above).
2. Every turn, wake and pairing works with no internet. An unreachable cloud never blocks a turn.
3. vizCloud: v1 frozen, v2 additive, never 401 a legitimate bot, size caps kept, migrations append-only, one Node service + Postgres, no deploy or prod DB access without Kevin's OK.

**Privacy and security**
4. No raw audio leaves the house. Training uploads are phrase text by default, embeddings only on explicit opt-in, never WAVs.
5. Room audio goes only to a brain the bot holds a token for. Once strict, every change to host/token/wake/enabled needs proof.
6. A brain never overwrites another brain's token automatically. Claiming a paired bot is an explicit local action. Revoke survives rediscovery.
7. UDP carries no authority: it may only start a wake on a paired bot.
8. Tokens are per bot, never logged, compared in constant time, stored 0600 or in Keychain. Local admin routes are loopback-only.

**Firmware**
9. Active Object rules: handlers only set requests or flags; no new blocking calls in AudioAO; any new TX or mic load publishes `SIG_NET_TX_START/END` or a named AoSig.
10. TX budget: every audio packet goes through `wakeSend`/`wakeTxRoom`; no new continuous traffic; header stays VZW1 12 bytes.
11. Voice stays StackChan-only. The 1.69 and vizMini appear only in the vizCloud roster.

**Compatibility and behaviour**
12. New firmware works with the released vizbrain, and the new vizbrain serves released firmware. A one-bot install (Kate) behaves the same, byte for byte, including the system prompt. Rolling vizbrain back keeps the first bot working.
13. One turn per bot, a global turn cap, Mac speaker one bot at a time, shared-word wakes suppressed while any bot is speaking.
14. A wake model is assigned only after passing the gate (false accepts, recall, cross-phrase rejection). Recipe in code, provenance manifest per model.

**Process**
15. One branch + one PR per stage, bench dot-suffix version. Firmware: `tools/soak/interactions.py` from a cold boot on mac + a check against the released vizbrain. vizbrain: unit tests + a live mac + skunky run, then the Mac goes back to the released build. No AI attribution anywhere.

## 6. Staged rollout

Proof rules:
- **firmware** = interaction suite from a cold boot on mac + wake-and-turn against the released vizbrain
- **mac** = unit tests + a live two-bot run (mac + skunky), then back to the released build

### Phase A: both bots can wake (fixes today's bug)

| # | Owner | Ships | Proof | Size |
|---|---|---|---|---|
| A1 | mac | **Roster + wake from any paired bot.** Roster = hosts proven to hold our token (a token-carrying call, or `/brain/status.pairId == ours` from a `discover_stackchans()` sweep). `on_wake` accepts any rostered host. **Echo guard:** drop all wakes while `busy`, and until the reply clip ends + 1 s. Announce loops over rostered bots (host and wake only); token pushed only to an empty `pairId`. `503` gains `say`. `adopt_bot` stays (turns are still serialized, so tools follow the caller). | Unit: wake from a non-current host fires; a wake during the other bot's reply is dropped; a bot with a foreign `pairId` gets no token. Live: "Hey vizBot" at skunky while mac was last used, skunky answers; then mac answers. | ~4 h |
| A2 | mac | Resilience: `Handler.timeout = 20`, per-host `peak`, per-host stream stats in `/v1/health` (packets, seq gaps, BEAT age, noise, wakes). | Unit: a half-sent POST is freed after 20 s; a 60 s reply stream still completes. Live: health shows both bots. | ~2 h |

### Phase B: safe per-bot pairing

| # | Owner | Ships | Proof | Size |
|---|---|---|---|---|
| B1 | firmware | `/brain/status` adds `uid, botId, name, fw, board, caps, diag` (UDP fail step + errno, HTTP fail step + esp_err, cloud-held flag). | `curl /brain/status` on both; uid matches vizCloud admin MAC. Cold-boot skunky 3x, read `diag`. | ~3 h |
| B2 | firmware | Token protection: `collectHeaders("X-Viz-Token")`, proof or empty slot for `token`, no adoption in `/brain/play`, `lastwav` locked, `pairRefused` on a not-paired 403, hold-to-Forget. | `curl ?token=x` = 403, with proof = 200. Forget + hold, released brain re-pairs in 60 s. Forget without hold keeps the token. Released brain pairs a fresh bot. | ~1 day |
| B3 | mac | `bots.json` by uid, per-bot tokens, migration, `X-Viz-Token` on `Bot._get`, BEAT-triggered announce, `bots claim/revoke/unstrict`, `code` in 403/503, `strict_bots` off. | Unit: a copy of a real `pairing.json` + two fake bots on `8514b0dc` = 2 records; the old token still serves the first. A foreign pairId is never pushed. Revoke = 403 `not_paired`. Live on released fw and on B2 fw: both re-key silently. Roll back: mac still works. | ~1 day |
| B4 | firmware | Strict mode + audio gate + link rule. Tested against a B3 dev brain. | With `strict=1`: `curl ?host=10.0.0.99` no token = 403, `/brain/wake` no token = 403. Cold boot: BEATs only, first burst within 10 s. Revoke: only BEATs. `bots unstrict` then released brain works. | ~1 day |
| B5 | firmware | Boot fix for skunky's 67 UDP fails + stalled POST, chosen from B1 data only. Candidate: `VOICE_CONNECT_MS` for open, then `esp_http_client_set_timeout_ms` for I/O. | 5 cold boots of skunky: 0 UDP fails before the first burst, no stalled POST. | set by B1 data |

### Phase C: talk at once, each bot itself

| # | Owner | Ships | Proof | Size |
|---|---|---|---|---|
| C1 | mac + tools | Arbitration: window when another rostered idle bot sharing the model sent any packet in the last 6 s; SNR first, then score. Echo guard narrows to shared-word wakes from other bots. Adds `t_voice_wake_two` to the suite. | Unit: higher SNR wins at 120 ms apart; B's first packet 200 ms late still competes; one bot alone fires with no delay. Live: between the bots, exactly one beeps. | ~½ day |
| C2 | mac | `BotSession` per uid, overlapping turns, cap 2, Mac speaker exclusive, bot passed explicitly to tools/pictures/code mode/`personality()`, `adopt_bot` removed, cooldowns per (uid, kind), routines tagged, inferred false wake (wake 200 + no `/v1/voice` within 20 s). | Unit: two concurrent `/v1/voice` both answer, a third gets 503 + `say`, histories stay apart. Live: talk to both within 5 s. | ~1 day |
| C3 | mac | Per-bot name in prompt, voice, speaker, persona note, memory scope. | Unit: one-bot prompt bytes equal today's. Live: a different voice on skunky only. | ~½ day |

### Phase D: own names

| # | Owner | Ships | Proof | Size |
|---|---|---|---|---|
| D1 | mac | Shared feature pipeline, classifiers per bot, model manifest, `wake assign`. Ships with `hey_vizbot` only. | Assign a candidate model to skunky only; CPU within the benchmark (~1.1 ms per step per bot). | ~½ day |
| D2 | mac | Training pipeline: `--phrase --job`, recipe in code (weight ramp to 1500, 32 hidden, 10% positives, 8k steps), near-misses from the phrase, other household phrases as negatives, acceptance gate. `setup-trainer`, `train` as a separate `nice` process. | Retrain "Hey vizBot", compare with the doc. Train "Hey Skunky". A day's soak with the TV on. | ~1 day + ~35 min/job |
| D3 | mac + firmware | Studio inside the service, per bot, loopback page. Firmware: `wakeWord` on the Brain card ("Say 'Hey Skunky'", plain text "Open vizbrain on the Mac to record"), and `wakePhrase` + `brainPaired` in v2 sync state. | 40 clips from skunky while vizbrain runs, none from mac. Wake word shows after reboot. | ~½ day + ~2 h |
| D4 | mac | False-wake capture (opt-in, max 50 clips, purge after 30 days) + `wake retrain`. | Unit: no `/v1/voice` within 20 s saves the burst; cap and purge work. | ~½ day |

### Phase E: hosted training (optional)

| # | Owner | Ships | Proof | Size |
|---|---|---|---|---|
| E1 | vizCloud | Admin shows `wakePhrase`/`brainPaired`; `brains` + `wake_jobs` tables, brain API, worker API, Brains page. | `npm run lint && typecheck && test` on PGlite; Bot API tests unchanged; admin e2e. | ~1-2 days |
| E2 | mac | Cloud client: `wake train --hosted`, download, sha256 check, local assign. | Against local vizCloud, then a friend-style install. | ~½ day |
| E3 | mac | `vizbrain wake worker` on Kevin's Mac. | Local end to end. Production deploy only with Kevin's OK. | ~½ day |

**Rollback note (RUNBOOK, from B3):** the old vizbrain serves only the bot that kept the shared token. A re-keyed bot on released firmware is silently retaken by the old announce; on B2+ firmware it needs Forget (hold the head). Any strict bot needs `vizbrain bots unstrict` before rollback.

## 7. Decisions for Kevin

| # | Question | Team recommendation |
|---|---|---|
| 1 | Keep "Hey vizBot" as a shared word next to each bot's own name? | **Yes**, on by default. Arbitration + echo guard stop double answers. |
| 2 | Overlapping turns, or a queue? | **Overlap**: one turn per bot, max 2 at once, Mac speaker one bot at a time. A queue can push bot 2 near its 45 s HTTP limit. |
| 3 | Memory shared or per bot? | **Shared facts** (`memory.json`), **per-bot history**, with a per-bot `memory:"own"` switch. |
| 4 | Pairing security: Forget from the bot page needs a head hold, a brain that lost `pairing.json` can't silently re-pair, strict mode stays off until a later release turns it on? | **Yes to all three.** Turn strict on after a week of mac + skunky on B4 firmware + B3. |
| 5 | Hosted training: where does compute run, what leaves a friend's house? | vizCloud stores jobs and models; **compute on Kevin's Mac** as a worker (~30 min/job, no GPU bill). Default upload = phrase text only. Opt-in = voice embeddings, never audio, said plainly in `privacy-and-cost.md`. |

## 8. Disagreements and the architect's call

| Topic | Positions | Call |
|---|---|---|
| Scope of stage 1 | Mac: remove `adopt_bot` and pass the bot explicitly in 1a. Architect review: same. | **Smaller A1.** With one global busy lock, turns are serialized and `adopt_bot` already points tools at the caller. Explicit passing moves to C2, where overlap needs it. Adds an echo guard, which neither draft had in stage 1: without it, mac's reply saying "vizBot" would wake skunky. |
| `host` open before strict | Review: gate it once a token is held. Firmware: keep it open until strict. | **Firmware's way.** Checked: a pinned `brainHost` skips mDNS (`voice_client.h:406`), and the released announce pushes `host` with no header (`server.py:481`). Gating it would strand friends' bots after a Mac IP change. Strict closes the gap. |
| False-wake window | Review: 10 s. Mac: 20 s. | **20 s.** `VOICE_WAIT_SPEECH_MS` 6000 + `VOICE_MAX_REC_MS` 12000 = 18 s before a POST. |
| Arbitration window | Review: another bot's burst in the last 1 s. Mac: any packet in the last 6 s. | **Mac's.** It covers a pre-roll held 200 ms by `wakeTxRoom`, and costs 350 ms only in homes with 2+ bots online. |
| `/brain/status` shape | Mac asked `pair:{state}`, `link:{}`, `lastSendErrno`. Firmware: `pair:{strict,refused,linkUp,authAgoS}`, `diag:{}`. | **Firmware's shape** (it owns the route), and top-level `pairId` stays for the released brain. |
| mDNS TXT | Mac: "may add". Firmware: dropped. | **Not built.** `/state` discovery works. |
| vizCloud state fields | Mac E1 needs bots to send `wakePhrase`/`brainPaired`. Firmware: "no change for vizCloud". | **Firmware sends them in D3**, additive v2 state, read-only. |

Next: pick answers for the 5 decisions in section 7. A1 can start either way.
