import unittest

from mcp.server.mcpserver.exceptions import ToolError

from vizself.chunks import MAX_LINES, chunk_file
from vizself.index import Index
from vizself.search import tokens
from vizself.source import Repo

from .helpers import make_repo


class RepoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = make_repo()

    def test_default_ref_is_the_newest_firmware_tag_not_vizbrain_or_head(self):
        self.assertEqual(Repo(self.root).ref, "v1.1.0")

    def test_reads_git_objects_not_the_working_tree(self):
        files = Repo(self.root).files(("vizbot/*.h",))
        self.assertIn('"1.1.0"', files["vizbot/config.h"])

    def test_only_the_listed_folders(self):
        files = Repo(self.root).files(("vizbot/*.h", "vizbrain/vizbrain/*.py", "docs/*.md"))
        self.assertEqual(sorted(files), ["docs/plan-eyes.md", "vizbot/bot_eyes.h", "vizbot/config.h",
                                         "vizbrain/vizbrain/__init__.py"])


class IndexTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ix = Index.build(Repo(make_repo()))

    def test_self_map_splits_into_file_summaries_and_overview(self):
        self.assertIn("vizbot/bot_eyes.h", self.ix.summaries)
        self.assertEqual([t for t, _ in self.ix.overview], ["Overview: what vizBot is"])
        self.assertEqual(sorted(self.ix.boards), ["Stackchan", "Waveshare ESP32-S3-Touch-LCD-1.69 (pendant)"])

    def test_files_changed_since_the_self_map_release_are_stale(self):
        self.assertEqual(self.ix.map_ref, "v1.0.0")
        self.assertEqual(self.ix.stale, {"vizbot/bot_eyes.h", "vizbot/config.h"})
        self.assertIn("trust the code", self.ix.stale_note("vizbot/bot_eyes.h"))

    def test_questions_line_feeds_search_but_not_the_summary(self):
        self.assertEqual(self.ix.questions("vizbot/bot_eyes.h"), "how do your eyes work? do you ever wink?")
        self.assertNotIn("wink", self.ix.summary("vizbot/bot_eyes.h", sentences=5))
        self.assertIn("wink", self.ix._embeddable(self.ix.search("blink", 1)[0]))

    def test_versions_come_from_the_code(self):
        self.assertTrue(self.ix.versions().startswith("firmware 1.1.0, vizbrain 0.2.0, from v1.1.0"))

    def test_search_finds_code_by_a_spoken_word(self):
        hits = self.ix.search("how do you blink", 3)
        self.assertEqual(hits[0].path, "vizbot/bot_eyes.h")
        self.assertEqual(hits[0].kind, "code")

    def test_search_skips_a_file_summary_when_its_code_already_matched(self):
        hits = self.ix.search("blink", 8)
        self.assertNotIn("vizbot/bot_eyes.h", [c.title for c in hits if c.kind == "map"])

    def test_empty_question_is_a_tool_error(self):
        with self.assertRaises(ToolError):
            self.ix.search("how do you", 3)

    def test_resolve_by_file_name_and_close_names(self):
        self.assertEqual(self.ix.resolve("bot_eyes.h"), "vizbot/bot_eyes.h")
        with self.assertRaisesRegex(ToolError, "Close: bot_eyes.h"):
            self.ix.resolve("bot_eye.h")

    def test_board_by_short_name(self):
        self.assertEqual(self.ix.board("1.69")[0], "Waveshare ESP32-S3-Touch-LCD-1.69 (pendant)")


class ChunkTest(unittest.TestCase):
    def test_tokens_split_code_names_and_drop_filler(self):
        self.assertEqual(tokens("how does scCameraMotionTick work"),
                         ["sccameramotiontick", "sc", "camera", "motion", "tick"])
        self.assertEqual(tokens("blinking servo_pins"), ["blink", "servo", "pin"])

    def test_cpp_pieces_cover_the_file_and_stay_small(self):
        text = "\n\n".join(f"void f{i}() {{\n" + "  x();\n" * 30 + "}" for i in range(6))
        pieces = chunk_file("vizbot/a.h", text)
        self.assertTrue(all(c.end - c.start + 1 <= MAX_LINES for c in pieces))
        self.assertEqual(pieces[0].start, 1)
        self.assertEqual(pieces[-1].end, len(text.splitlines()))
        self.assertTrue(all(c.text.lstrip().startswith("void f") for c in pieces))

    def test_braces_in_strings_and_comments_dont_confuse_depth(self):
        text = 'void a() {\n  s = "}";  // }\n  /* } */\n\n  more();\n}\n\nvoid b() {\n}\n'
        starts = [c.start for c in chunk_file("vizbot/a.h", text)]
        self.assertEqual(starts, [1])   # one small piece: b() merges, a()'s blank line isn't a cut

    def test_markdown_splits_on_level_two_headings(self):
        pieces = chunk_file("docs/x.md", "# T\n\nintro\n\n## One\n\na\n\n## Two\n\nb\n")
        self.assertEqual([c.title for c in pieces], ["(top)", "One", "Two"])


if __name__ == "__main__":
    unittest.main()
