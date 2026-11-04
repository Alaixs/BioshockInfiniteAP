"""Ids are stored in every generated seed and in players' saves: an id never changes or moves to
another name. ids.json records them. New items and locations are added to it, never edited: set
BSAP_UPDATE_IDS=1 and run the tests once (python apworld/run_tests.py <Archipelago checkout>).
"""

import json
import os
import pathlib
import unittest

from ..data import item_name_to_id, location_name_to_id

SNAPSHOT = pathlib.Path(__file__).with_name("ids.json")


class TestIds(unittest.TestCase):
    def test_ids_are_unique_and_valid(self) -> None:
        for kind, table in (("item", item_name_to_id), ("location", location_name_to_id)):
            with self.subTest(kind):
                self.assertEqual(len(set(table.values())), len(table), f"two {kind}s share an id")
                for name, ident in table.items():
                    self.assertTrue(0 < ident < 2**53, f"{kind} {name!r}: id {ident} out of range")

    def test_recorded_ids_never_change(self) -> None:
        current = {"items": item_name_to_id, "locations": location_name_to_id}
        recorded = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
        if os.environ.get("BSAP_UPDATE_IDS"):
            for kind in current:
                recorded[kind] = {**current[kind], **recorded[kind]}  # only adds the new ones
            SNAPSHOT.write_text(json.dumps(recorded, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        for kind in ("items", "locations"):
            with self.subTest(kind):
                for name, ident in recorded[kind].items():
                    self.assertEqual(current[kind].get(name), ident, f"{name!r} must keep id {ident}")
                new = sorted(name for name in current[kind] if name not in recorded[kind])
                self.assertFalse(new, f"new {kind} missing from ids.json (BSAP_UPDATE_IDS=1): {new}")
