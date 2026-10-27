"""The vizpow repo at one git ref: file list, file text, history. Read from git objects,
never the working tree, so a half-finished branch can't leak into what vizBot says."""

from __future__ import annotations

import subprocess
from pathlib import Path

from mcp.server.mcpserver.exceptions import ToolError

# What vizBot is made of. Everything else in the repo (proofs of concept, other
# boards' side projects, build output) only gets a mention in the self-map.
CODE_GLOBS = ("vizbot/*.h", "vizbot/*.ino", "vizbot/platformio.ini", "vizbot/*.csv",
              "vizbrain/vizbrain/*.py")
DOC_GLOBS = ("docs/*.md", "vizbot/README.md", "vizbrain/README.md")
SELF_DOCS = "docs/vizself"


class Repo:
    def __init__(self, path: Path, ref: str | None = None):
        self.path = Path(path).expanduser()
        if not (self.path / ".git").exists():
            raise ValueError(f"{self.path} is not a git checkout")
        self.ref = ref or self.latest_release()
        self.sha = self.git("rev-parse", f"{self.ref}^{{commit}}").strip()

    def git(self, *args: str, timeout: float = 20) -> str:
        r = subprocess.run(["git", "-C", str(self.path), *args], capture_output=True, text=True,
                           timeout=timeout)
        if r.returncode != 0:
            raise ToolError(f"git {args[0]} failed: {r.stderr.strip()[:200]}")
        return r.stdout

    def latest_release(self) -> str:
        """The newest firmware tag (v3.9.1 style, not vizbrain-v1.3.1): the code the bots run."""
        tags = self.git("tag", "--list", "v[0-9]*", "--sort=-v:refname").split()
        return next((t for t in tags if "-" not in t), "HEAD")

    def files(self, globs: tuple[str, ...]) -> dict[str, str]:
        """{path: text} for every file at the ref matching one of the globs."""
        listed = self.git("ls-tree", "-r", self.sha, "--", *_dirs(globs)).splitlines()
        blobs = {}
        for line in listed:
            meta, path = line.split("\t", 1)
            if meta.split()[1] == "blob" and any(Path(path).match(g) and path.count("/") == g.count("/")
                                                 for g in globs):
                blobs[meta.split()[2]] = path
        return {blobs[sha]: text for sha, text in self._cat(list(blobs)).items()}

    def _cat(self, shas: list[str]) -> dict[str, str]:
        if not shas:
            return {}
        r = subprocess.run(["git", "-C", str(self.path), "cat-file", "--batch"], input=("\n".join(shas) + "\n").encode(),
                           capture_output=True, timeout=30)
        out, pos, texts = r.stdout, 0, {}
        for _ in shas:
            nl = out.index(b"\n", pos)
            sha, _kind, size = out[pos:nl].decode().split()
            body = out[nl + 1:nl + 1 + int(size)]
            texts[sha] = body.decode("utf-8", "replace")
            pos = nl + 1 + int(size) + 1
        return texts

    def show(self, path: str) -> str | None:
        r = subprocess.run(["git", "-C", str(self.path), "show", f"{self.sha}:{path}"],
                           capture_output=True, timeout=10)
        return r.stdout.decode("utf-8", "replace") if r.returncode == 0 else None

    def changed_since(self, ref: str) -> set[str]:
        """Files that differ between `ref` and this ref ("" if `ref` is unknown here)."""
        try:
            return set(self.git("diff", "--name-only", ref, self.sha).split())
        except ToolError:
            return set()

    def log(self, limit: int, path: str = "") -> list[str]:
        args = ["log", self.sha, f"-n{limit}", "--no-merges", "--date=short", "--format=%ad %s"]
        return self.git(*args, *(["--", path] if path else [])).splitlines()


def _dirs(globs: tuple[str, ...]) -> list[str]:
    return sorted({str(Path(g).parent) for g in globs})
