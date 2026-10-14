# Connectors

A connector is an MCP server whose tools vizBot can use: your music, your calendar, facts about you. Each Mac lists its own connectors, so Kevin's vizBot and yours can know different things. With no connectors, vizBot works exactly as before.

Connectors need vizbrain 1.2.0 or newer.

## Add one

```bash
vizbrain connectors add spotify --action play --action like --replaces music_play -- spotify-mcp
vizbrain connectors test spotify
```

`add` saves the connector and restarts vizbrain, which reads the list when it starts. `test` starts it on its own and lists its tools; add `--call TOOL '{}'` to try one tool.

| Command | Does |
|---|---|
| `vizbrain connectors` | List them, with how each one is doing if vizbrain is running |
| `vizbrain connectors test [NAME] [--call TOOL JSON]` | Start them here, list their tools, call one |
| `vizbrain connectors add NAME [options] -- COMMAND [ARGS...]` | Add (or replace) a server that runs as a command |
| `vizbrain connectors add NAME --url URL` | Add an HTTP MCP server |
| `vizbrain connectors remove NAME` | Remove one |
| `vizbrain connectors set-secret ID` | Store a secret in the Keychain for a connector's env |

`add` options:

- `--action TOOL` marks a tool that changes something, like play or like. After an action he just confirms it; other tools are look-ups, and he reads the answer before replying.
- `--replaces TOOL` hides one of vizBot's own tools while this connector is running. A Spotify connector replaces `music_play`.
- `--env KEY=VALUE` sets an environment variable for the server. `KEY=keychain:ID` reads a secret saved with `set-secret ID`, so it never sits in a file.
- `--timeout SECONDS` is how long one call may take (default 8, at most 20).

## The file

It's `~/Library/Application Support/vizbrain/connectors.json`, readable only by you. It uses the same shape as Claude Desktop and Claude Code:

```json
{
  "mcpServers": {
    "kpow": {"command": "kpow-mcp"},
    "spotify": {
      "command": "spotify-mcp",
      "actions": ["play", "queue", "control", "create_playlist", "add_to_playlist", "like"],
      "replaces": ["music_play"]
    }
  }
}
```

Other keys: `args`, `env`, `cwd`, `url` (instead of `command`), `tools` (an allowlist), `enabled` (false switches it off), `timeout_s`, `start_timeout_s`, `max_result_chars`.

- **Names:** lowercase letters, digits and dashes, up to 16 characters. Claude sees each tool as `name__tool`, for example `spotify__play`.
- **Commands:** found in `~/.local/bin`, Homebrew and vizbrain's own folder, even though the login agent starts with a bare PATH.
- **A broken entry is skipped, not fatal.** `vizbrain connectors` and `vizbrain doctor` say what's wrong with it.

## What vizbrain does with them

- **Starting:** each server runs in the background while vizbrain runs, and is checked once a minute. A server that crashes is restarted, and its tools stay offered meanwhile.
- **Limits:** 12 tools per connector, 24 in all. Up to 4 look-ups and 15 seconds of waiting per question, so a slow server can't keep him quiet for long.
- **Speaking:** he says "let me check" while a look-up runs. If Claude goes straight to the tool, he says a short filler himself.
- **Trust:** results are labelled as data from that connector. After one, he won't change his memory or routines in the same answer, so a server can't plant anything that sticks.
- **Names:** a server can offer a resource named `vocabulary`, with names to expect, one per line. Speech-to-text gets them as hints, and it's re-read every minute.
- **Pictures:** if a result contains `Cover: https://...` from Spotify's image servers, the cover shows on his screen for 8 seconds. This needs bot firmware 3.7.34 or newer.

## Writing your own server

Any MCP server over stdio works. For a good voice experience:

- Return short plain text. He reads it aloud, so skip tables and markdown.
- Raise `ToolError` (from `mcp.server.mcpserver.exceptions`) for failures you expect, like "nothing playing". Other exceptions reach Claude only as "Error executing tool".
- Answer within a few seconds: serve cached data, refresh it in the background.
- Say in each tool's description when to use it. That's all Claude sees.

## Kevin's two

- **kpow-mcp** answers questions about Kevin from kpow.xyz and a private profile file on his Mac.
- **spotify-mcp** plays, queues, searches, makes playlists, adds to them and likes songs.
  - **Sign-in:** it borrows the sign-in of a vizSpot on the same Wi-Fi, so there's nothing to set up for playing and searching. For playlists and likes, run `spotify-mcp login` once and approve it in the browser.
  - **Premium:** playing, skipping and queueing need Spotify Premium.
