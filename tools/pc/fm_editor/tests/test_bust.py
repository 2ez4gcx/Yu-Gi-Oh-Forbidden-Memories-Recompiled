"""A duelist's bust (bust.py, notes/more-duelists.md "bust"): its pictures
read and written with the roster, the checks busts.c makes, its mod API,
the game's numbering and preview, the tower stories naming an expression,
and the Duelists tab's Bust section and the Tower tab's picker.

    python -m unittest discover -s tools/pc/fm_editor/tests -t tools/pc
"""
import json
import re
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from fm_editor import bust as bustm, compat, manifest, pngio, roster, tower, validate
from fm_editor.model import Project
from fm_editor.tests.test_data import fixture
from fm_editor.tests.test_gui import GuiCase

W, H = 64, 96


def figure(mark, mouth=False, shut=False, w=W, h=H) -> bytes:
    """A figure on a clear ground: the test's own picture, by its colors."""
    out = bytearray()
    for y in range(h):
        for x in range(w):
            if shut and 20 <= y < 24 and 20 <= x < 44:
                out += bytes((10, 10, 10, 255))
            elif mouth and 34 <= y < 40 and 26 <= x < 38:
                out += bytes((250, 250, 250, 255))
            elif 4 <= y < 12 and 24 <= x < 40:
                out += bytes(mark) + b"\xff"
            elif (x - w // 2) ** 2 + (y - 26) ** 2 < 18 ** 2 or (y >= 46 and abs(x - w // 2) < 14 + (y - 46) // 3):
                out += bytes((200, 160, 120, 255))
            else:
                out += b"\x00\x00\x00\x00"
    return pngio.encode(pngio.Image(w, h, bytes(out)))


CALM, ANGRY, SAD = (40, 200, 60), (220, 40, 40), (40, 90, 230)


def three() -> bustm.Bust:
    b = bustm.short(figure(CALM), "calm")
    b.expressions[0].talk = [figure(CALM, mouth=True)]
    b.expressions[0].talk_paths = [None]
    b.expressions[0].blink = figure(CALM, shut=True)
    bustm.add(b, "angry", figure(ANGRY))
    bustm.add(b, "sad", figure(SAD))
    return b


class BustTest(unittest.TestCase):
    def setUp(self):
        self.p = Project(fixture().game())
        self.p.info.id = "m"
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name) / "m"

    def files(self):
        return sorted(f.relative_to(self.folder).as_posix() for f in self.folder.rglob("*") if f.is_file())

    def test_round_trip(self):
        """An added duelist's bust and a disc duelist's with replace_bust,
        saved as their pictures under busts/<id>/, opened again the same,
        saved again byte for byte; renamed and dropped, the old files go."""
        p = self.p
        ghost = roster.add_copy(p, 1, "Ghost")
        ghost.bust = three()
        roster.give_bust(p, 7, figure(SAD), "smug")
        roster.set_replace_bust(p, 7, True)
        manifest.save_mod(p, self.folder)
        self.assertEqual([f for f in self.files() if f.startswith("busts/")],
                         ["busts/ghost/angry.png", "busts/ghost/calm-blink.png", "busts/ghost/calm-talk1.png",
                          "busts/ghost/calm.png", "busts/ghost/sad.png", "busts/seto/smug.png"])
        written = json.loads((self.folder / "duelists/ghost.json").read_text())
        self.assertEqual(written["bust"], {"default": "calm", "expressions": [
            {"name": "calm", "image": "busts/ghost/calm.png", "talk": ["busts/ghost/calm-talk1.png"],
             "blink": "busts/ghost/calm-blink.png"},
            {"name": "angry", "image": "busts/ghost/angry.png"},
            {"name": "sad", "image": "busts/ghost/sad.png"}]})
        seto = json.loads((self.folder / "duelists/seto.json").read_text())
        self.assertEqual(seto, {"replace": "Seto", "bust": {"default": "smug", "expressions": [
            {"name": "smug", "image": "busts/seto/smug.png"}]}, "replace_bust": True})
        again, messages = manifest.open_mod(p.retail, self.folder)
        self.assertEqual(messages, [])
        mine = roster.find(again, "ghost")
        self.assertEqual([e.name for e in mine.bust.expressions], ["calm", "angry", "sad"])
        self.assertEqual(mine.bust.expressions[0].talk, ghost.bust.expressions[0].talk)
        self.assertEqual(mine.bust.expressions[0].blink, ghost.bust.expressions[0].blink)
        self.assertTrue(roster.replacement(again, 7).replace_bust)
        before = {f: (self.folder / f).read_bytes() for f in self.files()}
        manifest.save_mod(again, self.folder)
        self.assertEqual({f: (self.folder / f).read_bytes() for f in self.files()}, before)
        # Renamed: its files are named for it; the old ones go.
        bustm.rename(mine.bust, bustm.find(mine.bust, "calm"), "quiet")
        roster.drop_bust(again, 7)
        manifest.save_mod(again, self.folder)
        busts = [f for f in self.files() if f.startswith("busts/")]
        self.assertEqual(busts, ["busts/ghost/angry.png", "busts/ghost/quiet-blink.png", "busts/ghost/quiet-talk1.png",
                                 "busts/ghost/quiet.png", "busts/ghost/sad.png"])
        self.assertNotIn("duelists/seto.json", self.files())        # nothing left of the replacement
        self.assertEqual(json.loads((self.folder / "duelists/ghost.json").read_text())["bust"]["default"], "quiet")

    def test_spelling_kept(self):
        """A bust written by hand keeps its paths and its short form."""
        self.folder.mkdir(parents=True)
        (self.folder / "art").mkdir()
        (self.folder / "art/g.png").write_bytes(figure(CALM))
        (self.folder / "art/g-open.png").write_bytes(figure(CALM, mouth=True))
        (self.folder / "mod.json").write_text(json.dumps({"id": "m", "name": "M", "duelists": [
            {"id": "a", "copy": 1, "bust": "art/g.png"},
            {"id": "b", "copy": 2, "bust": {"default": "x", "expressions": [
                {"name": "x", "image": "art/g.png", "talk": "art/g-open.png"}]}},
            {"id": "c", "copy": 3, "bust": {"default": "y", "expressions": [{"name": "y", "image": "art/none.png"}]}}]}))
        p, messages = manifest.open_mod(self.p.retail, self.folder)
        self.assertEqual(messages, ["duelist c bust expressions[0]: no file art/none.png; kept as written"])
        manifest.save_mod(p, self.folder)
        listed = {e["id"]: e for e in json.loads((self.folder / "mod.json").read_text())["duelists"]}
        self.assertEqual(listed["a"]["bust"], "art/g.png")
        self.assertEqual(listed["b"]["bust"]["expressions"][0]["talk"], "art/g-open.png")
        self.assertEqual(listed["c"]["bust"], {"default": "y", "expressions": [{"name": "y", "image": "art/none.png"}]})
        self.assertTrue((self.folder / "art/g-open.png").is_file())

    def test_checks(self):
        p = self.p
        ghost = roster.add_copy(p, 1, "Ghost")
        b = ghost.bust = three()
        issues = lambda: [(i.level, i.message) for i in validate.validate(p) if i.area == "Duelists"]  # noqa: E731
        self.assertEqual(issues(), [])
        b.expressions[1].talk.append(figure(ANGRY, w=W + 8))
        b.expressions[1].talk_paths.append(None)
        for name in ("e4", "e5"):
            bustm.add(b, name, figure(SAD))
        b.default = "nobody"
        found = issues()
        self.assertIn(("warning", "its bust's default is none of its expressions; the first, \"calm\", is taken"), found)
        self.assertIn(("warning", f"expression \"angry\": talking frame 1 is {W + 8} x {H}, the default's {W} x {H}; "
                                  "the game stretches it to that"), found)
        self.assertIn(("warning", "5 expressions: the game changes 4 of one record in place; the fifth on come in "
                                  "as another character, sliding"), found)
        # replace_bust: a disc duelist's, which the campaign shows as a character.
        roster.give_bust(p, 4, figure(CALM))
        roster.set_replace_bust(p, 4, True)
        self.assertIn(("warning", "the campaign shows Villager 1 as no character: replace_bust changes nothing "
                                  "there"), issues())
        with self.assertRaises(ValueError):
            roster.set_replace_bust(p, ghost, True)
        # Names the editor makes.
        self.assertEqual(bustm.check_name(b, ""), "an expression has a name")
        self.assertIn("already", bustm.check_name(b, "ANGRY"))
        self.assertIn("at most 31", bustm.check_name(b, "x" * 32))
        with self.assertRaises(ValueError):
            bustm.add(b, "sad", figure(SAD))

    def test_numbers_and_characters(self):
        """The game numbers the default 0, then the list's order; the disc
        duelists' campaign characters are tower_config.c's."""
        b = three()
        b.default = "angry"
        self.assertEqual([e.name for e in bustm.order(b)], ["angry", "calm", "sad"])
        self.assertEqual(bustm.number(b, bustm.find(b, "sad")), 2)
        source = (Path(__file__).resolve().parents[4] / "src/pc/tower/tower_config.c").read_text(encoding="utf-8")
        table = re.search(r"duelist_characters\[39\] = \{(.*?)\};", source, re.S).group(1)
        table = re.sub(r"/\*.*?\*/", "", table, flags=re.S)
        self.assertEqual(tuple(int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", table)),
                         bustm.DUELIST_CHARACTERS)
        self.assertEqual(bustm.sharing(7), [7, 32, 36])
        self.assertEqual(bustm.campaign_character(23), 0)
        header = (Path(__file__).resolve().parents[4] / "src/pc/free_duel/busts.h").read_text(encoding="utf-8")
        for name, value in (("BUST_EXPRESSIONS", bustm.EXPRESSIONS), ("BUST_TALK_FRAMES", bustm.TALK_FRAMES),
                            ("BUST_IN_PLACE", bustm.IN_PLACE), ("BUST_SIDE", bustm.SIDE)):
            self.assertRegex(header, rf"#define {name} {value}\b")

    def test_preview_where_the_game_stands_it(self):
        """Its middle over the side's place, its bottom on the box (under
        178 rows), and the frames drawn over the picture."""
        b = three()
        calm = bustm.find(b, "calm")
        screen = bustm.preview(bustm.frame(calm), "left")
        self.assertEqual(screen.size, (320, 240))
        left, top = 0x68 - W // 2, 0xB2 - H
        self.assertEqual(screen.pixel(left + 30, top + 6)[:3], CALM)
        talking = bustm.preview(bustm.frame(calm, ("talk", 0)), "right")
        self.assertEqual(talking.pixel(0xD8 - W // 2 + 30, top + 36)[:3], (250, 250, 250))
        shut = bustm.frame(calm, "blink")
        self.assertEqual(shut.pixel(30, 21)[:3], (10, 10, 10))
        big = bustm.fitted(pngio.Image(512, 300, b"\x10\x20\x30\x80" * (512 * 300)))
        self.assertEqual(big.size, (256, 150))
        self.assertEqual(big.pixel(0, 0)[3], 255)

    def test_mod_api(self):
        """A bust, replace_bust and a story's expression by name need mod API
        17; so does a bust in a duelist's own file."""
        needs = lambda value, project=None: compat.required(value, project)[0]  # noqa: E731
        self.assertEqual(needs({"duelists": [{"id": "g", "copy": 1, "bust": "b.png"}]}), 17)
        self.assertEqual(needs({"duelists": [{"replace": 7, "replace_bust": True}]}), 17)
        self.assertEqual(needs({"tower": {"floors": [{"duelist": 1, "before": {"characters": {
            "left": {"duelist": "g", "expression": "angry"}}, "lines": ["Hi."]}}]}}), 17)
        self.assertEqual(needs({"tower": {"floors": [{"duelist": 1, "before": {"characters": {
            "left": {"duelist": "g", "expression": 2}}, "lines": ["Hi."]}}]}}), 14)
        p = self.p
        roster.add_copy(p, 1, "Ghost").bust = three()
        self.assertEqual(needs(manifest.build(p), p), 17)
        self.assertEqual(compat.HOST_API, 17)

    def test_tower_names_an_expression(self):
        """[left: Ghost expression angry] and back; the checks know the bust."""
        for text, value in (("Ghost expression angry", {"duelist": "Ghost", "expression": "angry"}),
                            ('Ghost expression "very angry"', {"duelist": "Ghost", "expression": "very angry"}),
                            ("opponent expression sad", {"duelist": "opponent", "expression": "sad"}),
                            ("4 expression 2", {"character": 4, "expression": 2})):
            self.assertEqual(tower._cast_value(text), value)
            self.assertEqual(tower._cast_text(value), text)
        p = self.p
        ghost = roster.add_copy(p, 1, "Ghost")
        ghost.bust = three()
        story = {"characters": {"left": {"duelist": "Ghost", "expression": "glad"},
                                "right": {"duelist": "Seto", "expression": "calm"}}, "lines": ["Hi."]}
        found = tower.check({"intro": story, "floors": [{"duelist": 1}]}, lambda n: roster.named(p, n),
                            lambda n: p.resolve(n), lambda n: roster.bust_of(p, roster.named(p, n)))
        # Each says what the game shows instead (tower_config.c bust_expression).
        self.assertIn(("warning", "tower.intro.characters.left.expression",
                       "Ghost's bust has no expression \"glad\"; its default, \"calm\", is shown instead"), found)
        self.assertIn(("warning", "tower.intro.characters.right.expression",
                       "Seto has no bust of its own (the Duelists tab's Bust); left out, and the campaign's "
                       "character is shown as it first comes in (expression 0)"), found)
        story["characters"] = {"left": {"duelist": "Ghost", "expression": "angry"}}
        self.assertEqual(tower.check({"intro": story, "floors": [{"duelist": 1}]}, lambda n: roster.named(p, n),
                                     lambda n: p.resolve(n), lambda n: roster.bust_of(p, roster.named(p, n))), [])


class BustTabTest(GuiCase):
    def test_the_bust_section(self):
        """Add an expression, rename it, its frames, the default, the order,
        the campaign option (asked first) and the preview; the Tower tab's
        picker writes the duelist and its expression."""
        app, tab = self.app, self.app.duelists
        p = app.project
        app.notebook.select(tab)
        app.update()
        view = tab.bust_view
        tab.sections.select(view.frame)
        tab.goto((7, None))
        folder = Path(self.tmp.name) / "pictures"
        folder.mkdir(exist_ok=True)
        for name, data in (("calm", figure(CALM)), ("angry", figure(ANGRY)), ("open", figure(CALM, mouth=True)),
                           ("shut", figure(CALM, shut=True))):
            (folder / f"{name}.png").write_bytes(data)
        self.assertIsNone(view.add(str(folder / "calm.png"), "calm"))
        b = roster.bust_of(p, 7)
        self.assertEqual([e.name for e in b.expressions], ["calm"])
        self.assertEqual(roster.replacement(p, 7).bust, b)
        self.assertIn("bust", tab.list.set("7", "state"))
        self.assertIn("already", view.add(str(folder / "angry.png"), "calm"))      # a name used: refused
        view.add(str(folder / "angry.png"), "angry")
        self.assertEqual(len(view.list.get_children()), 2)
        view.list.selection_set(view.list.get_children()[0])
        view.add_talk(str(folder / "open.png"))
        view.choose_blink(str(folder / "shut.png"))
        e = bustm.find(b, "calm")
        self.assertEqual(len(e.talk), 1)
        self.assertIsNotNone(e.blink)
        self.assertEqual(view.list.set(str(id(e)), "frames"), "talk 1, blink")
        view.which.set("talk0")
        view.draw()
        self.assertIn("320×240", view.caption.cget("text"))
        view.rename("quiet")
        self.assertEqual(b.default, "quiet")
        view.list.selection_set(str(id(bustm.find(b, "angry"))))
        view.make_default()
        self.assertEqual([x.name for x in bustm.order(b)], ["angry", "quiet"])
        # The campaign option: asked first, said beside it.
        with mock.patch("tkinter.messagebox.askyesno", return_value=False):
            view.replace.set(True)
            view.toggle_replace()
        self.assertFalse(roster.replacement(p, 7).replace_bust)
        with mock.patch("tkinter.messagebox.askyesno", return_value=True):
            view.replace.set(True)
            view.toggle_replace()
        self.assertTrue(roster.replacement(p, 7).replace_bust)
        self.assertIn("CHANGES THE CAMPAIGN", view.warning.cget("text"))
        self.assertIn("Seto 2nd, Seto 3rd", view.warning.cget("text"))
        # The Tower tab's picker.
        towers = app.tower
        app.notebook.select(towers)
        towers.has.set(True)
        towers.toggle()
        app.update()
        towers._bust_duelists()
        self.assertIn("Seto", towers.stage_duelists.cget("values"))
        towers.stage_duelist.set("Seto")
        towers._bust_expressions()
        self.assertEqual(list(towers.stage_bust_expressions.cget("values")), ["angry", "quiet"])
        towers.stage_bust_expression.set("quiet")
        towers.last_story = "intro"
        towers._bust()
        self.assertIn("[left: Seto expression quiet]", towers.texts["intro"].get("1.0", "end"))
        # Gone: the replacement with it.
        app.notebook.select(tab)
        tab.goto((7, None))
        view.drop(ask=False)
        self.assertIsNone(roster.replacement(p, 7))
