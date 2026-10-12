"""A mod's "attribute" on a card (manifest.py _attribute_choice): the six
retail names always work; an id or name past them only takes if this mod's
own "attributes" section declares it, same as the engine's Attributes_Value
(src/pc/cards/attributes.c) requires. An id manifest.py let through without
that declaration used to write a card the engine would refuse at load, which
then fell back to reading past the retail attribute strip in the card-detail
renderer (src/game/func_80028B08.c) -- a stray texel where an icon belongs.

    python -m unittest discover -s tools/pc/fm_editor/tests -t tools/pc
"""
import unittest

from fm_editor import manifest
from fm_editor.model import Project
from fm_editor.tests.test_data import fixture

BASE = 1   # "Blue Dragon" in the synthetic fixture, a monster


def project() -> Project:
    return Project(fixture().game())


class CardAttributeTest(unittest.TestCase):
    def test_retail_name_needs_no_declaration(self):
        p = project()
        before = p.cards[BASE].attribute
        messages = manifest.apply(p, {"id": "m", "cards": [{"replace": BASE, "attribute": "Light"}]})
        self.assertEqual(p.cards[BASE].attribute, 0)
        self.assertNotEqual(before, 0)
        self.assertFalse(messages)

    def test_undeclared_numeric_id_is_left_out(self):
        p = project()
        before = p.cards[BASE].attribute
        messages = manifest.apply(p, {"id": "m", "cards": [{"replace": BASE, "attribute": 8}]})
        self.assertEqual(p.cards[BASE].attribute, before)
        self.assertTrue(any("attribute" in msg for msg in messages), messages)

    def test_undeclared_digit_string_is_left_out(self):
        p = project()
        before = p.cards[BASE].attribute
        messages = manifest.apply(p, {"id": "m", "cards": [{"replace": BASE, "attribute": "8"}]})
        self.assertEqual(p.cards[BASE].attribute, before)
        self.assertTrue(messages)

    def test_id_declared_for_a_different_id_is_left_out(self):
        p = project()
        before = p.cards[BASE].attribute
        manifest_json = {"id": "m", "attributes": {"attributes": [{"id": 9, "name": "Void"}]},
                         "cards": [{"replace": BASE, "attribute": 8}]}
        messages = manifest.apply(p, manifest_json)
        self.assertEqual(p.cards[BASE].attribute, before)
        self.assertTrue(messages)

    def test_declared_numeric_id_applies(self):
        p = project()
        manifest_json = {"id": "m", "attributes": {"attributes": [{"id": 8, "name": "Metal"}]},
                         "cards": [{"replace": BASE, "attribute": 8}]}
        messages = manifest.apply(p, manifest_json)
        self.assertEqual(p.cards[BASE].attribute, 8)
        self.assertFalse(messages)

    def test_declared_name_applies(self):
        p = project()
        manifest_json = {"id": "m", "attributes": {"attributes": [{"id": 8, "name": "Metal"}]},
                         "cards": [{"replace": BASE, "attribute": "Metal"}]}
        messages = manifest.apply(p, manifest_json)
        self.assertEqual(p.cards[BASE].attribute, 8)
        self.assertFalse(messages)

    def test_reserved_ids_never_apply_even_if_declared(self):
        p = project()
        before = p.cards[BASE].attribute
        manifest_json = {"id": "m", "attributes": {"attributes": [{"id": 6, "name": "Spell"}]},
                         "cards": [{"replace": BASE, "attribute": 6}]}
        messages = manifest.apply(p, manifest_json)
        self.assertEqual(p.cards[BASE].attribute, before)
        self.assertTrue(messages)


if __name__ == "__main__":
    unittest.main()
