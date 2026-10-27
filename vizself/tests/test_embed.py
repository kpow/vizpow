import os
import tempfile
import unittest
from unittest import mock

import numpy as np

from vizself import embed
from vizself.index import Index
from vizself.source import Repo

from .helpers import make_repo

# A stand-in model: texts about seeing and texts about blinking point different ways.
AXES = (("see", "gaze", "eye", "blink"), ("version", "pin", "servo"))


class FakeEmbedder:
    def __init__(self):
        self.calls = 0

    def encode(self, texts, query=False):
        self.calls += 1
        rows = [[1.0 + sum(w in t.lower() for w in words) * 5 for words in AXES] for t in texts]
        v = np.array(rows, dtype=np.float32)
        return v / np.linalg.norm(v, axis=1, keepdims=True)


class EmbedTest(unittest.TestCase):
    def setUp(self):
        self.cache = tempfile.mkdtemp()
        patcher = mock.patch.dict(os.environ, {"VIZSELF_CACHE": self.cache})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.ix = Index.build(Repo(make_repo()))

    def test_meaning_finds_code_that_shares_no_keyword(self):
        self.assertEqual(self.ix.search("gaze around", 3), [])   # no keyword hits at all
        self.ix.add_vectors(FakeEmbedder())
        hits = self.ix.search("gaze around", 3)
        self.assertEqual(hits[0].path, "vizbot/bot_eyes.h")

    def test_vectors_are_cached_for_the_same_texts(self):
        first, second = FakeEmbedder(), FakeEmbedder()
        self.ix.add_vectors(first)
        Index.build(Repo(self.ix.repo.path)).add_vectors(second)
        self.assertEqual((first.calls, second.calls), (1, 0))
        self.assertEqual(len(os.listdir(self.cache)), 1)

    def test_no_model_means_keyword_search_only(self):
        with mock.patch("huggingface_hub.hf_hub_download", side_effect=OSError("offline")):
            self.assertIsNone(embed.Embedder.load())


if __name__ == "__main__":
    unittest.main()
