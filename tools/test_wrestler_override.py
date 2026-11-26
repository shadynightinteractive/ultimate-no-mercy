#!/usr/bin/env python3
"""Tests of wrestler_override.py (the override file's rules O1-O7, 2026-11-17).   python tools/test_wrestler_override.py"""
import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wrestler_override as WO  # noqa: E402

BASE = {"id": "0x038C", "name": "ELIZABETH", "short_name": "LIZ", "nick": "THE LADY", "height": 7, "weight": 20,
        "body": {"nm_size": 64, "nm_belt": 2}, "looks": [[1] * 38] * 4, "source": {"game": "WCW/nWo Revenge"}}


class T(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        with open(os.path.join(self.d, "wrestler.json"), "w") as f:
            json.dump(BASE, f)

    def tearDown(self):
        shutil.rmtree(self.d)

    def over(self, o):
        with open(os.path.join(self.d, WO.OVERRIDE_FILE), "w") as f:
            f.write(o if isinstance(o, str) else json.dumps(o))

    def test_none(self):                      # no file: wrestler.json as it is
        self.assertEqual(WO.effective(self.d), BASE)
        self.assertEqual(WO.check_override(self.d), ([], []))

    def test_replace_and_merge(self):         # O1, O2, O4
        self.over({"name": "LIZ THE GREAT", "body": {"nm_size": 70}})
        e = WO.effective(self.d)
        self.assertEqual(e["name"], "LIZ THE GREAT")
        self.assertEqual(e["body"], {"nm_size": 70, "nm_belt": 2})
        self.assertEqual(e["short_name"], "LIZ")

    def test_delete(self):                    # O3
        self.over({"nick": None, "body": {"nm_belt": None}})
        e = WO.effective(self.d)
        self.assertNotIn("nick", e)
        self.assertEqual(e["body"], {"nm_size": 64})

    def test_list_replaced_whole(self):       # O2
        self.over({"looks": [[2] * 38] * 4})
        self.assertEqual(WO.effective(self.d)["looks"], [[2] * 38] * 4)
        self.over({"looks": [[2] * 38]})
        self.assertTrue(any("all four" in x for x in WO.check_override(self.d)[0]))

    def test_identity_left_out(self):         # O5
        self.over({"id": "0x0400", "replaces": "kane", "name": "X"})
        e = WO.effective(self.d)
        self.assertEqual(e["id"], "0x038C")
        self.assertNotIn("replaces", e)
        self.assertEqual(len(WO.check_override(self.d)[1]), 2)

    def test_not_an_object(self):             # O6
        self.over("[1, 2]")
        self.assertEqual(WO.effective(self.d), BASE)
        self.assertTrue(WO.check_override(self.d)[0])
        self.over("{not json")
        self.assertTrue(WO.check_override(self.d)[0])

    def test_checks(self):
        self.over({"height": "tall", "nmae": "typo", "short_name": "A VERY LONG NAME"})
        errs, warns = WO.check_override(self.d)
        self.assertTrue(any('"height"' in x for x in errs))
        self.assertTrue(any('"nmae"' in x for x in warns))
        self.assertTrue(any('"short_name"' in x for x in warns))

    def test_tree(self):
        self.over({"name": "X"})
        rows = WO.check_tree(os.path.dirname(self.d))
        self.assertTrue(any(p.startswith(self.d) for p, _, _ in rows))


if __name__ == "__main__":
    unittest.main()
