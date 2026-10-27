# vizself

vizBot's own code and hardware as an MCP server, so vizBot can talk about how it works.
It reads the vizpow repo straight from git at the newest firmware tag (`v3.9.1` style), so it
describes the code the bots run, never a half-finished branch. Nothing is hosted; it runs over stdio.

## Install and run

```bash
uv tool install --editable ~/projects/vizpow/vizself
vizself fetch-model   # once: the embedding model, BAAI/bge-small-en-v1.5 (~130 MB, Hugging Face cache)
vizself index         # optional: embed the current release now (~1 minute) instead of on first start
```

Add it to vizbrain's `~/Library/Application Support/vizbrain/connectors.json`:

```json
"vizself": {"command": "vizself", "timeout_s": 10}
```

Settings (environment): `VIZSELF_REPO` (default `~/projects/vizpow`), `VIZSELF_REF` (default: the
newest `vX.Y.Z` tag), `VIZSELF_DOCS` (a folder with `self-map.md` and `hardware.md`, overriding
`docs/vizself/` at the ref).

## Tools

| Tool | Answers |
|---|---|
| `search_code(query, limit?)` | How does the camera take a photo? Where is `voiceAskListen`? |
| `read_code(path, start?, lines?)` | Show me lines 430-480 of voice_client.h. |
| `self_map(topic?)` | What are you made of? What does party_blob.h do? |
| `hardware(board?)` | What servos do you have? Which pins? |
| `recent_changes(limit?, path?)` | What changed in the last release? |
| `open_work()` | What's Kevin working on next? (open issues and PRs, via `gh`) |

Search fuses two rankings (reciprocal rank fusion) over code pieces, docs and the self-map:

- keyword (BM25), splitting code names (`scCameraMotionTick` is also "camera motion tick");
- meaning, from a local embedding model on the CPU. Vectors are cached in
  `~/Library/Application Support/vizself/cache` per release, so only the first start after a
  release embeds (about a minute, in the background: keyword search answers meanwhile).

Each code piece is searched together with its file's self-map summary, so plain words find code.
Plans and reviews in `docs/` rank a little lower than code. Without the model, search is
keyword only. Files that changed since the self-map was written are marked, so the model
trusts the code over an old summary.

## Tests

```bash
cd vizself && uv run python -m unittest
```
