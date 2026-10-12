"""The Tower tab's "tower" (tower.py, notes/tower-mode.md): its story and deck
text forms and back, the checks tower_config.c makes, the tower-only
duelists, the manifest round trip and its mod API, and the tab.

    python -m unittest discover -s tools/pc/fm_editor/tests -t tools/pc
"""
import json
import unittest

try:
    import tkinter as tk
except ImportError:     # a Python built without Tk
    tk = None

from fm_editor import compat, manifest, roster, tower, validate
from fm_editor.model import Project
from fm_editor.tests.test_data import fixture

TOWER = {
    "name": "The Test Tower", "label": "TEST TOWER", "unlock": "after_ending", "on_lose": "restart",
    "intro": ["Three floors.", {"text": "Win them all.", "portrait": 0}],
    "ending": "The top.",
    "floors": [
        {"duelist": "Simon Muran", "checkpoint": True, "life_points": {"opponent": 100},
         "reward": {"cards": ["Mystic Elf"], "starchips": 25}, "before": "Show me.", "after": "Well fought."},
        {"duelist": "m:ghost", "field": "Yami", "no_fusions": True, "life_points": {"player": 100},
         "deck": {"Mystic Elf": 20, "Kuriboh": 20},
         "before": ["Nobody fuses here.", {"text": "Not even me.", "speaker": "m:ghost"}]},
        {"duelist": 7, "no_equips": True, "reward": {"unlock_duelist": "Nitemare"}},
    ],
}
MOD = {"id": "m", "name": "M", "duelists": [{"id": "ghost", "copy": "Simon Muran", "name": "Ghost Simon",
                                            "tower_only": True}],
       "tower": TOWER}


def project() -> Project:
    p = Project(fixture().game())
    manifest.apply(p, json.loads(json.dumps(MOD)))
    return p


def problems(p, value):
    return tower.check(value, lambda name: roster.named(p, name), lambda name: p.resolve(name))


class TowerTest(unittest.TestCase):
    def test_story_text(self):
        value = ["Plain line.", {"text": "Said.", "speaker": "Seto"}, {"text": "Faced.", "portrait": 8},
                 {"text": "Nobody's.", "speaker": "none"}, {"text": "By number.", "speaker": 7}]
        text = tower.story_text(value)
        self.assertEqual(text, "Plain line.\n@Seto: Said.\n#8: Faced.\n@none: Nobody's.\n@7: By number.")
        self.assertEqual(tower.story_value(text), value)
        self.assertEqual(tower.story_value("One line."), "One line.")
        self.assertIsNone(tower.story_value("\n  \n"))
        self.assertEqual(tower.story_text("Alone"), "Alone")
        # A colon in plain words is words: only "@who:" and "#number:" speak.
        self.assertEqual(tower.story_value("Note: it is late.\n#x: no"), ["Note: it is late.", "#x: no"])

    def test_deck_text(self):
        self.assertEqual(tower.deck_text({"Mystic Elf": 20, "Kuriboh": 1}), "Mystic Elf x20\nKuriboh")
        self.assertEqual(tower.deck_value("Mystic Elf x20\nKuriboh\n5 x3\nKuriboh"),
                         {"Mystic Elf": 20, "Kuriboh": 2, 5: 3})
        self.assertEqual(tower.deck_text([1, 1, 2]), "1 x2\n2")
        self.assertIsNone(tower.deck_value(""))
        self.assertEqual(tower.deck_count({"a": 20, "b": 20}), 40)
        self.assertEqual(tower.names_value("Kuriboh, 5"), ["Kuriboh", 5])
        self.assertEqual(tower.names_value("Kuriboh"), "Kuriboh")
        self.assertEqual(tower.names_text(["Kuriboh", 5]), "Kuriboh, 5")

    def test_terrain(self):
        self.assertEqual([tower.terrain_of(t) for t in ("none", "Forest", "yami", "Sea", "4", 6, None)],
                         [0, 1, 6, 5, 4, 6, 0])
        self.assertIsNone(tower.terrain_of("Lava"))

    def test_check_good(self):
        p = project()
        self.assertEqual(problems(p, TOWER), [])
        self.assertEqual(problems(p, None), [])

    def test_check_mistakes(self):
        """What tower_config.c notes, the same way: an error where the game
        leaves a part out, a warning where it reads it otherwise."""
        p = project()
        self.assertEqual([lvl for lvl, _, _ in problems(p, [1])], ["error"])
        self.assertIn(("error", "tower.floors"), {(lvl, w) for lvl, w, _ in problems(p, {"name": "x"})})
        bad = {"unlock": "after-ending", "on_lose": "retry", "colour": 1, "music": 99999,
               "label": "A LABEL FAR LONGER THAN THIRTY ONE LETTERS",
               "floors": [{"duelist": "Nobody"},
                          {"duelist": "Seto", "field": "Lava", "no_fusions": "true", "life_points": 0,
                           "deck": {"Mystic Elf": 39}, "reward": {"cards": ["Nothing"], "starchips": -5, "gold": 1},
                           "before": [{"text": "Hi.", "portrait": 30}, 5], "checkpoint": 1, "boss": True}]}
        found = {(lvl, w) for lvl, w, _ in problems(p, bad)}
        for expected in (("error", "tower.unlock"), ("warning", "tower.on_lose"), ("warning", "tower"),
                         ("warning", "tower.music"), ("warning", "tower.label"), ("error", "tower.floors[0]"),
                         ("warning", "tower.floors[1].field"), ("warning", "tower.floors[1].no_fusions"),
                         ("warning", "tower.floors[1].life_points"), ("error", "tower.floors[1].deck"),
                         ("warning", "tower.floors[1].reward"), ("warning", "tower.floors[1].reward.starchips"),
                         ("warning", "tower.floors[1].before[0]"), ("error", "tower.floors[1].before[1]"),
                         ("warning", "tower.floors[1].checkpoint"), ("warning", "tower.floors[1]")):
            self.assertIn(expected, found)
        self.assertIn(("error", "tower.floors"),
                      {(lvl, w) for lvl, w, _ in problems(p, {"floors": [{"duelist": "Nobody"}]})})
        unlock = {(lvl, w) for lvl, w, _ in problems(p, {"unlock": {"wins": "many", "beat": "Nobody"},
                                                         "floors": [{"duelist": 1}]})}
        self.assertEqual(unlock, {("error", "tower.unlock.wins"), ("warning", "tower.unlock.beat")})

    def test_same_as_the_game(self):
        """The keys tower_config.c knows are the editor's."""
        import re
        from pathlib import Path
        source = (Path(__file__).resolve().parents[4] / "src/pc/tower/tower_config.c").read_text(encoding="utf-8")
        arrays = [tuple(re.findall(r'"(\w+)"', body)) for body in re.findall(r"known\[\] = \{(.*?)\};", source, re.S)]
        for keys in (tower.TOWER_KEYS, tower.FLOOR_KEYS, tower.REWARD_KEYS, tower.UNLOCK_KEYS, tower.LINE_KEYS):
            self.assertIn(tuple(keys), arrays)

    def test_tower_only(self):
        p = project()
        self.assertEqual(tower.tower_only(p), ["ghost"])
        self.assertTrue(tower.set_tower_only(p, "ghost", False))
        self.assertEqual(tower.tower_only(p), [])
        self.assertFalse(tower.set_tower_only(p, "ghost", False))
        self.assertTrue(tower.set_tower_only(p, "ghost", True))
        self.assertFalse(tower.set_tower_only(p, "nobody", True))

    def test_manifest_and_validate(self):
        p = project()
        built = manifest.build(p)
        self.assertEqual(built["tower"], TOWER)
        self.assertEqual(built["duelists"][0]["tower_only"], True)
        self.assertEqual(compat.required(built)[0], 13)
        self.assertEqual([i for i in validate.validate(p) if i.area == "Tower"], [])
        p.other["tower"] = {"floors": [{"duelist": "Nobody"}]}
        issues = [(i.level, i.where) for i in validate.validate(p) if i.area == "Tower"]
        self.assertIn(("error", "tower.floors"), issues)
        self.assertFalse([i for i in validate.validate(p) if i.area == "Mod info" and i.where == "tower"])


class TowerTabTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if tk is None:
            raise unittest.SkipTest("this Python has no Tk")
        try:
            cls.root = tk.Tk()
        except tk.TclError as problem:
            raise unittest.SkipTest(f"no display for Tk: {problem}")
        cls.root.withdraw()

    @classmethod
    def tearDownClass(cls):
        cls.root.destroy()

    def make(self, p):
        from tkinter import ttk
        from fm_editor.tower_tab import TowerTab

        class App:
            def __init__(self):
                self.project = p
                self.changes = 0

            def changed(self):
                self.changes += 1

        app = App()
        tab = TowerTab(ttk.Notebook(self.root), app)
        tab.refresh()
        return app, tab

    def test_tab_shows_and_keeps(self):
        p = project()
        app, tab = self.make(p)
        self.assertTrue(tab.has.get())
        self.assertEqual(tab.vars["name"].get(), "The Test Tower")
        self.assertEqual(tab.vars["unlock"].get(), "after_ending")
        self.assertEqual(tab.vars["on_lose"].get(), "restart")
        self.assertEqual(len(tab.list.get_children()), 3)
        self.assertEqual(tab.vars["duelist"].get(), "Simon Muran")
        self.assertTrue(tab.vars["checkpoint"].get())
        self.assertEqual(tab.vars["lp.opponent"].get(), "100")
        self.assertEqual(tab.vars["reward.cards"].get(), "Mystic Elf")
        self.assertEqual(tab._get_text("intro"), "Three floors.\n#0: Win them all.")
        self.assertEqual(tab.only.curselection(), (0,))
        # Committed untouched (as every tab switch does): as written, unchanged.
        self.assertTrue(tab.commit())
        self.assertEqual(p.other["tower"], TOWER)
        self.assertEqual(app.changes, 0)
        # The second floor in the form, and back.
        tab.list.selection_set("1")
        tab.pick()
        self.assertEqual((tab.vars["field"].get(), tab.vars["no_fusions"].get(), tab.vars["lp.player"].get()),
                         ("Dark", True, "100"))
        self.assertEqual(tab._get_text("deck"), "Mystic Elf x20\nKuriboh x20")
        self.assertTrue(tab.commit())
        self.assertEqual(p.other["tower"], TOWER)

    def test_tab_edits(self):
        p = project()
        app, tab = self.make(p)
        tab.list.selection_set("2")
        tab.pick()
        tab.vars["no_equips"].set(False)
        tab.vars["lp.player"].set("4000")
        tab.vars["lp.opponent"].set("4000")
        tab.vars["field"].set("Forest")
        tab._set_text("after", "@Seto: You win.\nThe end.")
        self.assertTrue(tab.commit())
        floor = p.other["tower"]["floors"][2]
        self.assertEqual(floor, {"duelist": 7, "field": "Forest", "life_points": 4000,
                                 "reward": {"unlock_duelist": "Nitemare"},
                                 "after": [{"text": "You win.", "speaker": "Seto"}, "The end."]})
        self.assertGreater(app.changes, 0)
        # A floor added, moved up, removed.
        tab.add()
        self.assertEqual(len(p.other["tower"]["floors"]), 4)
        tab.vars["duelist"].set("Heishin")
        tab.move(-1)
        self.assertEqual(p.other["tower"]["floors"][2]["duelist"], "Heishin")
        tab.remove()
        self.assertEqual([f["duelist"] for f in p.other["tower"]["floors"]], ["Simon Muran", "m:ghost", 7])
        # The tower's own duelists, and the conditions.
        tab.only.selection_clear(0, "end")
        tab.vars["unlock"].set("conditions")
        tab.vars["unlock.beat"].set("Seto")
        tab.vars["unlock.after_ending"].set(True)
        self.assertTrue(tab.commit())
        self.assertNotIn("tower_only", roster.find(p, "ghost").extra)
        self.assertEqual(p.other["tower"]["unlock"], {"after_ending": True, "beat": "Seto"})
        # Off: no tower in the mod.
        tab.has.set(False)
        tab.toggle()
        self.assertNotIn("tower", p.other)
        self.assertNotIn("tower", manifest.build(p))

    def test_tab_new_tower(self):
        p = Project(fixture().game())
        app, tab = self.make(p)
        self.assertFalse(tab.has.get())
        tab.has.set(True)
        tab.toggle()
        self.assertEqual(p.other["tower"], {"floors": [{"duelist": 1}]})
        tab.vars["name"].set("Mine")
        tab.vars["music"].set("0x2C0")
        self.assertTrue(tab.commit())
        self.assertEqual(p.other["tower"], {"name": "Mine", "music": 0x2C0, "floors": [{"duelist": 1}]})
        self.assertEqual(compat.required(manifest.build(p))[0], 13)


if __name__ == "__main__":
    unittest.main()
