"""vizself: vizBot's own code and hardware as an MCP server (stdio).

vizBot reaches it through vizbrain's connectors.json. Settings come from the environment:
VIZSELF_REPO (default ~/projects/vizpow), VIZSELF_REF (default: the newest firmware tag),
VIZSELF_DOCS (a folder with self-map.md and hardware.md, overriding the ones in the repo).
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from .index import Index
from .search import tokens
from .source import Repo

LIMIT = 5500        # one result; vizbrain's default cap is 6000 characters
SNIPPET = 18        # code lines shown per search hit
MAX_READ = 150

INSTRUCTIONS = (
    "vizBot's own source code, design docs and hardware, at the release the bots run. Use it when asked how "
    "vizBot works, what it is made of, why it does something, or what changed. Start with search_code or "
    "self_map, then read_code for detail. Results are for you to read; answer out loud in plain words, "
    "never read code aloud.")


class Tools:
    def __init__(self, index: Index):
        self.ix = index

    def search_code(self, query: str, limit: int = 5) -> str:
        """Search vizBot's own code, docs and self-map by meaning and by keyword ("how do you see", "head touch pad", or exact names like voiceAskListen). Returns file, line range, what the file does, and the matching code. `limit` is 1 to 8."""
        hits = self.ix.search(query, max(1, min(limit, 8)))
        if not hits:
            return f"Nothing in vizBot's code matches {query!r}. Try other words or a name from the code."
        out = [f"vizBot code ({self.ix.versions()}), best matches for {query!r}:"]
        for n, c in enumerate(hits, 1):
            if c.kind == "code":
                what = self.ix.summary(c.path) + self.ix.stale_note(c.path)
                start, lines = _best_window(c.text.splitlines(), query)
                body = "\n".join(f"{c.start + start + i:5}  {l}" for i, l in enumerate(lines))
                out.append(f"\n{n}. {c.path}, lines {c.start}-{c.end}. {what}\n{body}")
            else:
                label = "self-map" if c.kind == "map" else c.path
                out.append(f"\n{n}. From {label}, {c.title}:\n{_clip(c.text, 900)}")
        return _clip("\n".join(out), LIMIT)

    def read_code(self, path: str, start: int = 1, lines: int = 80) -> str:
        """Read part of one of vizBot's source files with line numbers. `path` can be a full path or a file name (voice_client.h). `lines` is at most 150."""
        path = self.ix.resolve(path)
        all_lines = self.ix.files[path].splitlines()
        start = max(1, min(start, len(all_lines)))
        end = min(len(all_lines), start + max(1, min(lines, MAX_READ)) - 1)
        body = "\n".join(f"{i:5}  {all_lines[i - 1]}" for i in range(start, end + 1))
        return _clip(f"{path}, lines {start}-{end} of {len(all_lines)}:\n{body}", LIMIT)

    def self_map(self, topic: str = "") -> str:
        """vizBot's map of itself. No topic: what vizBot is, its boards, and how body and brain talk. With a file name (party_blob.h) or a topic (servos, voice): what those parts do and which files hold them."""
        if not topic.strip():
            parts = [f"vizBot ({self.ix.versions()})."] + [f"{t}:\n{b}" for t, b in self.ix.overview]
            return _clip("\n\n".join(parts), LIMIT)
        try:
            path = self.ix.resolve(topic)
            return _clip(self.ix.summaries.get(path, f"{path} has no self-map entry yet.")
                         + self.ix.stale_note(path), LIMIT)
        except ToolError:
            pass
        hits = self.ix.search(topic, 3, kinds=("map",))
        if not hits:
            return f"The self-map says nothing about {topic!r}. Try search_code."
        return _clip("\n\n".join(c.text + self.ix.stale_note(c.title.strip("` ")) for c in hits), LIMIT)

    def hardware(self, board: str = "stackchan") -> str:
        """vizBot's hardware for one board: chip, screen, servos, camera, mic, speaker, LEDs, pins and known quirks. `board` is stackchan (default), cores3, 1.69, 1.3, faces or matrix."""
        title, body = self.ix.board(board)
        return _clip(body, LIMIT)

    def recent_changes(self, limit: int = 10, path: str = "") -> str:
        """What changed lately in vizBot's code, newest first, as dated commit messages. Optional `path` (a file name) narrows it to one file. `limit` is 1 to 25."""
        if path:
            path = self.ix.resolve(path)
        lines = self.ix.repo.log(max(1, min(limit, 25)), path)
        if not lines:
            return f"No changes found{' to ' + path if path else ''}."
        head = f"Latest changes{' to ' + path if path else ''} up to {self.ix.repo.ref}:"
        return _clip("\n".join([head] + lines), LIMIT)

    def open_work(self) -> str:
        """Open GitHub issues and pull requests for vizBot: what Kevin is working on or has planned next."""
        gh = shutil.which("gh") or next((p for p in ("/opt/homebrew/bin/gh", "/usr/local/bin/gh")
                                         if Path(p).exists()), None)
        if not gh:
            raise ToolError("GitHub isn't reachable from here (no gh command).")
        repo = _github_repo(self.ix.repo)
        out = []
        for kind in ("issue", "pr"):
            try:
                r = subprocess.run([gh, kind, "list", "--repo", repo, "--state", "open", "--limit", "15",
                                    "--json", "number,title,updatedAt"], capture_output=True, text=True, timeout=8)
            except subprocess.TimeoutExpired:
                raise ToolError("GitHub didn't answer in time.") from None
            if r.returncode != 0:
                raise ToolError(f"GitHub said no: {r.stderr.strip()[:200]}")
            items = json.loads(r.stdout or "[]")
            label = "Open issues" if kind == "issue" else "Open pull requests"
            out.append(f"{label}: " + ("; ".join(f"#{i['number']} {i['title']} (updated {i['updatedAt'][:10]})"
                                                  for i in items) or "none") + ".")
        return _clip("\n".join(out), LIMIT)


def _best_window(lines: list[str], query: str) -> tuple[int, list[str]]:
    """The SNIPPET-line stretch of a piece with the most query words in it."""
    if len(lines) <= SNIPPET:
        return 0, lines
    want = set(tokens(query))
    hits = [len(want & set(tokens(l))) for l in lines]
    best = max(range(len(lines) - SNIPPET + 1), key=lambda s: (sum(hits[s:s + SNIPPET]), -s))
    return best, lines[best:best + SNIPPET]


def _github_repo(repo: Repo) -> str:
    url = repo.git("remote", "get-url", "origin").strip()
    m = re.search(r"github\.com[:/](.+?)(?:\.git)?$", url)
    if not m:
        raise ToolError("This checkout has no GitHub remote.")
    return m.group(1)


def _clip(text: str, limit: int) -> str:
    return text if len(text) <= limit else text[:limit - 20].rstrip() + "\n[... cut short]"


def build(index: Index) -> MCPServer:
    server = MCPServer("vizself", instructions=INSTRUCTIONS)
    tools = Tools(index)
    for fn in (tools.search_code, tools.read_code, tools.self_map, tools.hardware, tools.recent_changes,
               tools.open_work):
        server.add_tool(fn, structured_output=False)
    return server


def default_index() -> Index:
    repo = Repo(Path(os.environ.get("VIZSELF_REPO", "~/projects/vizpow")), os.environ.get("VIZSELF_REF") or None)
    docs = os.environ.get("VIZSELF_DOCS")
    return Index.build(repo, Path(docs).expanduser() if docs else None)


def run() -> None:
    """`vizself` serves MCP over stdio; `vizself fetch-model` downloads the embedding model once;
    `vizself index` embeds the current release ahead of time."""
    from .embed import Embedder

    command = sys.argv[1] if len(sys.argv) > 1 else "serve"
    if command == "fetch-model":
        print("model ready" if Embedder.load(download=True) else "download failed")
        return
    index = default_index()
    embedder = Embedder.load()
    if command == "index":
        started = time.monotonic()
        if embedder:
            index.add_vectors(embedder)
        print(f"{len(index.chunks)} pieces, {index.versions()}, "
              f"{'embedded' if embedder else 'keyword only'} in {time.monotonic() - started:.1f} s")
        return
    if embedder:
        # Keyword search answers at once; meaning search joins when the vectors are ready
        # (instant from the cache, about a minute on the first start after a release).
        threading.Thread(target=index.add_vectors, args=(embedder,), daemon=True).start()
    build(index).run("stdio")
