"""Everything vizself knows, built once at start: code pieces, docs, the self-map and the
hardware page, with a keyword index over all of it and, once the embedding model has run,
a meaning index too. Search fuses the two rankings."""

from __future__ import annotations

import difflib
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from mcp.server.mcpserver.exceptions import ToolError

from .chunks import Chunk, chunk_file, md_sections
from .search import BM25, tokens
from .source import CODE_GLOBS, DOC_GLOBS, SELF_DOCS, Repo

_VERSION = re.compile(r"\bv\d+\.\d+\.\d+\b")
_FW_VERSION = re.compile(r'FIRMWARE_VERSION\s+"([^"]+)"')
_BRAIN_VERSION = re.compile(r'__version__\s*=\s*"([^"]+)"')
PER_FILE = 2     # search results from one file, at most
# Plans and reviews describe intent, often outdated; the code and the self-map are what he is.
WEIGHT = {"code": 1.0, "map": 1.0, "doc": 0.85}   # fused ranks are flat: 0.85 drops a doc ~10 places
QUESTIONS = "Questions: "   # self-map line: what people ask that this file answers
RRF_K = 60       # reciprocal rank fusion: a piece scores 1 / (RRF_K + rank) in each ranking
DENSE_TOP = 40   # meaning-search ranks that count


@dataclass
class Index:
    repo: Repo
    files: dict[str, str]                 # code path -> text
    chunks: list[Chunk]
    summaries: dict[str, str]             # code path -> its self-map section
    overview: list[tuple[str, str]]       # self-map sections that aren't about one file
    boards: dict[str, str]                # hardware.md section per board
    map_ref: str = ""                     # the release the self-map was written for
    stale: set[str] = field(default_factory=set)
    bm25: BM25 | None = None
    embedder: Any = None
    vectors: Any = None                   # one unit row per chunk, once embedded

    @classmethod
    def build(cls, repo: Repo, docs_dir: Path | None = None) -> "Index":
        files = repo.files(CODE_GLOBS)
        chunks = [c for path, text in sorted(files.items()) for c in chunk_file(path, text)]
        for path, text in sorted(repo.files(DOC_GLOBS).items()):
            chunks += chunk_file(path, text)
        self_map, hardware = _self_docs(repo, docs_dir)
        summaries, overview = {}, []
        for title, s, e, body in md_sections(self_map):
            path = title.strip("` ")
            if path in files:
                summaries[path] = body
            elif title != "(top)":
                overview.append((title, body))
            chunks.append(Chunk(f"{SELF_DOCS}/self-map.md", s, e, body, "map", title))
        boards = {}
        for title, s, e, body in md_sections(hardware):
            if title != "(top)":
                boards[title] = body
            chunks.append(Chunk(f"{SELF_DOCS}/hardware.md", s, e, body, "map", title))
        found = _VERSION.search("\n".join(self_map.splitlines()[:6]))
        index = cls(repo, files, chunks, summaries, overview, boards, found.group(0) if found else "")
        if index.map_ref and index.map_ref != repo.ref:
            index.stale = repo.changed_since(index.map_ref) & set(files)
        index.bm25 = BM25([index._searchable(c) for c in chunks])
        return index

    def add_vectors(self, embedder) -> None:
        """Embed every chunk (or load them from the cache); search uses them from then on."""
        from .embed import cached_vectors

        vectors = cached_vectors(embedder, [self._embeddable(c) for c in self.chunks])
        self.embedder = embedder
        self.vectors = vectors

    def questions(self, path: str) -> str:
        """The spoken questions the self-map says this file answers ("how do you see?")."""
        lines = self.summaries.get(path, "").splitlines()
        return next((l.strip()[len(QUESTIONS):] for l in lines if l.strip().startswith(QUESTIONS)), "")

    def _embeddable(self, c: Chunk) -> str:
        context = f"{self.summary(c.path)}\n{self.questions(c.path)}" if c.kind == "code" else ""
        return f"{c.path} {c.title}\n{context}\n{c.text}"

    def _searchable(self, c: Chunk) -> str:
        context = self.summaries.get(c.path, "") if c.kind == "code" else ""
        return f"{c.path} {c.title}\n{context}\n{c.text}"

    # ---- facts -------------------------------------------------------------------------

    def versions(self) -> str:
        fw = _FW_VERSION.search(self.files.get("vizbot/config.h", ""))
        brain = _BRAIN_VERSION.search(self.files.get("vizbrain/vizbrain/__init__.py", ""))
        return (f"firmware {fw.group(1) if fw else 'unknown'}, vizbrain {brain.group(1) if brain else 'unknown'}, "
                f"from {self.repo.ref} (commit {self.repo.sha[:7]})")

    def stale_note(self, path: str) -> str:
        if path not in self.stale:
            return ""
        return f" (Summary written for {self.map_ref}; this file changed since, so trust the code.)"

    def summary(self, path: str, sentences: int = 2) -> str:
        body = self.summaries.get(path, "")
        text = " ".join(l for l in body.splitlines()[1:]
                        if l.strip() and not l.lstrip().startswith(("-", QUESTIONS)))
        parts = re.split(r"(?<=[.!?])\s+", text.strip())
        return " ".join(parts[:sentences])

    # ---- lookups -----------------------------------------------------------------------

    def search(self, query: str, limit: int, kinds: tuple[str, ...] = ("code", "map", "doc")) -> list[Chunk]:
        if not tokens(query):
            raise ToolError("Say what to look for: a feature, a part, or a name from the code.")
        fused: dict[int, float] = {}
        keyword = sorted(((s, i) for i, s in enumerate(self.bm25.scores(query)) if s > 0), reverse=True)
        for rank, (_, i) in enumerate(keyword):
            fused[i] = 1 / (RRF_K + rank)
        vectors = self.vectors
        if vectors is not None:
            sims = vectors @ self.embedder.encode([query], query=True)[0]
            for rank, i in enumerate(sims.argsort()[::-1][:DENSE_TOP]):
                fused[int(i)] = fused.get(int(i), 0.0) + 1 / (RRF_K + rank)
        scored = sorted(((v * WEIGHT[self.chunks[i].kind], i) for i, v in fused.items()
                         if self.chunks[i].kind in kinds), reverse=True)
        out, per_file = [], {}
        for _, i in scored:
            c = self.chunks[i]
            if per_file.get(c.path, 0) >= PER_FILE:
                continue
            per_file[c.path] = per_file.get(c.path, 0) + 1
            out.append(c)
            if len(out) >= 2 * limit:
                break
        # A code hit carries its file's self-map summary, so that summary on its own is a repeat.
        coded = {c.path for c in out if c.kind == "code"}
        return [c for c in out if not (c.kind == "map" and c.title.strip("` ") in coded)][:limit]

    def resolve(self, path: str) -> str:
        """A code path from what the model asked for: full path, file name, or close to one."""
        path = path.strip().strip("`").lstrip("./")
        if path in self.files:
            return path
        by_name = [p for p in self.files if Path(p).name == Path(path).name]
        if len(by_name) == 1:
            return by_name[0]
        close = difflib.get_close_matches(Path(path).name, [Path(p).name for p in self.files], n=4, cutoff=0.5)
        hint = f" Close: {', '.join(close)}." if close else ""
        raise ToolError(f"No file called {path} in vizBot's code.{hint}")

    def board(self, name: str) -> tuple[str, str]:
        key = name.lower().replace(" ", "")
        for title, body in self.boards.items():
            if key and key in title.lower().replace(" ", ""):
                return title, body
        raise ToolError(f"No board matching {name!r}. Boards: {', '.join(self.boards)}.")


def _self_docs(repo: Repo, docs_dir: Path | None) -> tuple[str, str]:
    """self-map.md and hardware.md: from docs_dir when given, else from the repo at the ref."""
    out = []
    for name in ("self-map.md", "hardware.md"):
        text = None
        if docs_dir and (docs_dir / name).exists():
            text = (docs_dir / name).read_text()
        out.append(text if text is not None else (repo.show(f"{SELF_DOCS}/{name}") or ""))
    return out[0], out[1]
