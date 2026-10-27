"""Search by meaning: a small local embedding model (bge-small, ONNX on the CPU), so
"how do you see" finds the camera code. Vectors are cached per release, so only the
first start after a new release pays for them."""

from __future__ import annotations

import hashlib
import os
import sys
from pathlib import Path

import numpy as np

MODEL = "BAAI/bge-small-en-v1.5"
FILES = ("onnx/model.onnx", "tokenizer.json")
QUERY_PREFIX = "Represent this sentence for searching relevant passages: "
MAX_TOKENS = 512
BATCH = 16
KEEP_CACHES = 3


class Embedder:
    def __init__(self, model_path: str, tokenizer_path: str):
        import onnxruntime as ort
        from tokenizers import Tokenizer

        self.session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
        self.tok = Tokenizer.from_file(tokenizer_path)
        self.tok.enable_truncation(MAX_TOKENS)

    @classmethod
    def load(cls, download: bool = False) -> "Embedder | None":
        """The model from the Hugging Face cache; None (keyword search only) when it isn't there."""
        from huggingface_hub import hf_hub_download

        try:
            paths = [hf_hub_download(MODEL, f, local_files_only=not download) for f in FILES]
        except Exception as e:   # not downloaded, or offline
            print(f"[vizself] no embedding model ({type(e).__name__}); keyword search only. "
                  f"Run `vizself fetch-model` once to add it.", file=sys.stderr)
            return None
        return cls(*paths)

    def encode(self, texts: list[str], query: bool = False) -> np.ndarray:
        """Unit vectors, one row per text."""
        if query:
            texts = [QUERY_PREFIX + t for t in texts]
        order = sorted(range(len(texts)), key=lambda i: len(texts[i]))   # similar lengths pad less
        out = np.zeros((len(texts), 384), dtype=np.float32)
        for b in range(0, len(order), BATCH):
            idx = order[b:b + BATCH]
            self.tok.enable_padding()
            enc = self.tok.encode_batch([texts[i] for i in idx])
            ids = np.array([e.ids for e in enc], dtype=np.int64)
            mask = np.array([e.attention_mask for e in enc], dtype=np.int64)
            hidden = self.session.run(None, {"input_ids": ids, "attention_mask": mask,
                                             "token_type_ids": np.zeros_like(ids)})[0]
            cls = hidden[:, 0, :]   # bge pools on the [CLS] token
            out[idx] = cls / np.linalg.norm(cls, axis=1, keepdims=True)
        return out


def cache_dir() -> Path:
    env = os.environ.get("VIZSELF_CACHE")
    return Path(env).expanduser() if env else Path.home() / "Library/Application Support/vizself/cache"


def cached_vectors(embedder: Embedder, texts: list[str]) -> np.ndarray:
    """Vectors for these texts, from the cache when the exact same texts were embedded before."""
    digest = hashlib.sha256("\0".join([MODEL] + texts).encode()).hexdigest()[:16]
    path = cache_dir() / f"{digest}.npy"
    if path.exists():
        vecs = np.load(path)
        if len(vecs) == len(texts):
            return vecs
    vecs = embedder.encode(texts)
    path.parent.mkdir(parents=True, exist_ok=True)
    np.save(path, vecs)
    for old in sorted(path.parent.glob("*.npy"), key=lambda p: p.stat().st_mtime)[:-KEEP_CACHES]:
        old.unlink(missing_ok=True)
    return vecs
