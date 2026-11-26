#!/usr/bin/env python3
"""Tests of families_check.py (families, 2026-11-14). Run: python -m unittest test_families_check (with wrestler_check.py and its
story\\gen beside families_check.py, as in the editor's validators folder, or on PYTHONPATH)."""
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import families_check as fc  # noqa: E402


def write(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f)


def wrestler(root, rel, **kw):
    j = {"based_on": "steve_austin"}
    j.update(kw)
    write(os.path.join(root, rel, "wrestler.json"), j)


def v(w, n): return {"wrestler": w, "variant": n}


class Base(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="families_test_")
        wrestler(self.root, "WCW/sting_x")
        wrestler(self.root, "WCW/hogan_x")
        wrestler(self.root, "Japan/muta_x")

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def check(self):
        return fc.check_families(self.root)

    def fam(self, rep, name):
        for f in rep.families:
            if f[1] == name: return f
        self.fail("no family %s in %s" % (name, [f[1] for f in rep.families]))

    def names(self, rep, f):
        k = fc.known_from_folder(self.root)
        return ["%s %d" % (k.name(n), look) for n, look in f[3]]


class TestDefault(Base):
    def test_no_file_is_the_default(self):                      # F1
        rep = self.check()
        self.assertTrue(rep.from_default)
        self.assertEqual([f[1] for f in rep.families], [i for i, _ in fc.DEFAULT])
        self.assertEqual(len(self.fam(rep, "undertaker")[3]), 8)
        self.assertEqual(rep.errors, [])

    def test_written_default_is_the_same(self):                 # --write-default
        path = os.path.join(self.root, "families.json")
        self.assertEqual(fc.main(["--write-default", path]), 0)
        rep = self.check()
        self.assertFalse(rep.from_default)
        self.assertEqual(rep.errors, [])
        self.assertEqual([(f[1], f[3]) for f in rep.families], [(f[1], f[3]) for f in fc.check_families(tempfile.mkdtemp()).families])

    def test_empty_list_means_none(self):                       # F1: a file with an empty list: no families at all
        write(os.path.join(self.root, "families.json"), {"families": []})
        rep = self.check()
        self.assertEqual(rep.families, [])
        self.assertTrue(any("undertaker" in n for n in rep.notes))

    def test_hidden_only_keeps_the_default(self):               # F1 / H1: "hidden" alone doesn't replace the families
        write(os.path.join(self.root, "families.json"), {"hidden": [{"wrestler": "kane"}]})
        rep = self.check()
        self.assertTrue(rep.from_default)
        self.assertEqual(rep.hidden, {4: 0xF})


class TestRules(Base):
    def test_order_and_interleave(self):
        write(os.path.join(self.root, "families.json"), {"families": [{"id": "x", "variants": [
            v("sting_x", 2), v("hogan_x", 1), v("sting_x", 1), v("0x0002", 4)]}]})
        rep = self.check()
        f = self.fam(rep, "x")
        self.assertEqual(self.names(rep, f), ["sting_x 2", "hogan_x 1", "sting_x 1", "steve_austin 4", "sting_x 3", "sting_x 4"])   # (U1)
        self.assertEqual(rep.errors, [])

    def test_errors(self):                                      # F3, F4, F5
        write(os.path.join(self.root, "families.json"), {"families": [
            {"id": "a", "variants": [v("sting_x", 1), v("nobody", 1), v("sting_x", 5), v("sting_x", 1), {"wrestler": "hogan_x"}]},
            {"id": "b", "variants": [v("sting_x", 2), v("hogan_x", 1)]},
            {"id": "c"}]})
        rep = self.check()
        e = " | ".join(x for _, x in rep.errors)
        for want in ("must be 1-4", "variant 1 is in it already", "needs \"wrestler\" and \"variant\"",
                     "is in families.json, family a already", "needs a \"variants\" list"):
            self.assertIn(want, e)
        # (F3, round 4 point 3: a wrestler not there now is a warning -- left out while he isn't)
        self.assertNotIn("nobody", e)
        self.assertTrue(any("no wrestler \"nobody\" now" in x for _, x in rep.warnings))
        self.assertEqual(self.names(rep, self.fam(rep, "b")), ["hogan_x 1", "hogan_x 2", "hogan_x 3", "hogan_x 4"])   # (U1)

    def test_not_json(self):                                    # F2
        with open(os.path.join(self.root, "families.json"), "w") as f: f.write("{nope")
        rep = self.check()
        self.assertTrue(rep.errors and "valid JSON" in rep.errors[0][1])
        self.assertTrue(rep.from_default)

    def test_collections_in_case_insensitive_order(self):       # the files' order (D2): the earlier file wins a wrestler (F4)
        write(os.path.join(self.root, "b", "families.json"), {"families": [{"id": "from_b", "variants": [v("muta_x", 1)]}]})
        write(os.path.join(self.root, "A", "families.json"), {"families": [{"id": "from_A", "variants": [v("muta_x", 2)]}]})
        write(os.path.join(self.root, "families.json"), {"families": [{"id": "from_root", "variants": [v("hogan_x", 1)]}]})
        files = [os.path.relpath(p, self.root).replace("\\", "/") for p in fc.family_files(self.root)]
        self.assertEqual(files, ["families.json", "A/families.json", "b/families.json"])
        rep = self.check()
        self.assertEqual(self.names(rep, self.fam(rep, "from_A")), ["muta_x 2", "muta_x 1", "muta_x 3", "muta_x 4"])   # (U1)
        self.assertTrue(any("is in A" in x for _, x in rep.errors))

    def test_clash_id_and_big_show_warnings(self):
        wrestler(self.root, "Other/sting_x")                    # (a second folder named sting_x: sting_x~2)
        write(os.path.join(self.root, "families.json"), {"families": [
            {"id": "s", "variants": [v("sting_x", 1), v("sting_x~2", 1)]},
            {"id": "bs", "variants": [v("big_show", 1), v("big_show", 2)]}]})
        rep = self.check()
        w = [x for _, x in rep.warnings]
        self.assertTrue(any("clash ID" in x for x in w))
        self.assertEqual(sum("Big Show option" in x for x in w), 1)


class TestNesting(Base):
    def test_nested_after_the_outer(self):                      # N1, deeper ones too, in case-insensitive path order
        wrestler(self.root, "WCW/sting_x/Zed_inner")
        wrestler(self.root, "WCW/sting_x/alpha_inner")
        wrestler(self.root, "WCW/sting_x/alpha_inner/deeper")
        rep = self.check()
        f = self.fam(rep, "sting_x")
        self.assertEqual([n.split()[0] for n in self.names(rep, f)][::4], ["sting_x", "alpha_inner", "deeper", "zed_inner"])
        self.assertEqual(len(f[3]), 16)

    def test_own_folders_are_not_collections(self):             # a nested folder's neighbours: never a collection
        os.makedirs(os.path.join(self.root, "WCW", "sting_x", "model", "files"))
        write(os.path.join(self.root, "WCW", "sting_x", "model", "families.json"), {"families": []})
        write(os.path.join(self.root, "WCW", "sting_x", "stock_roster.json"), {"builtin": ["the_rock"]})
        self.assertEqual(fc.family_files(self.root), [])
        rep = self.check()
        self.assertTrue(rep.from_default)
        self.assertEqual(fc.known_from_folder(self.root).removed, set())

    def test_data_wins(self):                                   # N2: named elsewhere by the data: there
        wrestler(self.root, "WCW/sting_x/inner")
        write(os.path.join(self.root, "families.json"), {"families": [{"id": "h", "variants": [v("hogan_x", 1), v("inner", 3)]}]})
        rep = self.check()
        self.assertEqual(self.names(rep, self.fam(rep, "h")), ["hogan_x 1", "inner 3", "hogan_x 2", "hogan_x 3", "hogan_x 4"])
        self.assertEqual(self.names(rep, self.fam(rep, "inner")), ["inner 1", "inner 2", "inner 4"])   # (U1: his own box)
        self.assertTrue(any("the data wins" in n for n in rep.notes))

    def test_nested_joins_the_outer_ones_data_family(self):     # N3: the outer one in a data family: at its end
        wrestler(self.root, "WCW/sting_x/inner")
        write(os.path.join(self.root, "families.json"), {"families": [{"id": "h", "variants": [v("hogan_x", 1), v("sting_x", 1)]}]})
        rep = self.check()
        self.assertEqual(self.names(rep, self.fam(rep, "h")), ["hogan_x 1", "sting_x 1", "inner 1", "inner 2", "inner 3", "inner 4",
                                                              "hogan_x 2", "hogan_x 3", "hogan_x 4"])   # (nested, then U1)
        self.assertEqual(self.names(rep, self.fam(rep, "sting_x")), ["sting_x 2", "sting_x 3", "sting_x 4"])

    def test_nested_ids_as_top_level(self):                     # IDs / clashes as for any folder
        wrestler(self.root, "WCW/sting_x/hogan_x")              # (a nested folder named like another: hogan_x~2)
        k = fc.known_from_folder(self.root)
        self.assertIn("hogan_x~2", k.sids)


class TestHidden(Base):
    def test_hidden_head_and_variant(self):                     # H1, H2, H3
        write(os.path.join(self.root, "families.json"), {
            "families": [{"id": "x", "variants": [v("sting_x", 1), v("hogan_x", 1), v("hogan_x", 2)]}],
            "hidden": [{"wrestler": "sting_x"}, {"wrestler": "hogan_x", "variant": 2}, {"wrestler": "muta_x", "variant": 4},
                       {"wrestler": "nobody"}, {"wrestler": "muta_x", "variant": 0}]})
        rep = self.check()
        f = self.fam(rep, "x")
        self.assertEqual(self.names(rep, f), ["hogan_x 1"])
        self.assertEqual(fc.known_from_folder(self.root).name(f[2]), "sting_x")      # (the slot: his place kept)
        self.assertTrue(any("headed by hogan_x" in n for n in rep.notes))
        self.assertEqual(self.names(rep, self.fam(rep, "muta_x")), ["muta_x 1", "muta_x 2", "muta_x 3"])
        e = " | ".join(x for _, x in rep.errors)
        self.assertIn("must be 1-4", e)
        self.assertTrue(any("no wrestler \"nobody\" now" in x for _, x in rep.warnings))

    def test_all_hidden_no_box(self):
        write(os.path.join(self.root, "families.json"), {"families": [{"id": "x", "variants": [v("sting_x", 1)]}],
                                                          "hidden": [{"wrestler": "sting_x"}]})
        rep = self.check()
        self.assertTrue(any("every variant of it is hidden" in x for _, x in rep.warnings))


class TestRound4(Base):
    def test_partial_family(self):                             # U1: unlisted variants never vanish
        write(os.path.join(self.root, "families.json"), {"families": [
            {"id": "x", "variants": [v("sting_x", 1), v("hogan_x", 2), v("sting_x", 3)]}]})
        rep = self.check()
        self.assertEqual(self.names(rep, self.fam(rep, "x")), ["sting_x 1", "hogan_x 2", "sting_x 3", "sting_x 2", "sting_x 4"])
        own = self.fam(rep, "hogan_x")
        self.assertTrue(own[0].startswith("(his own box"))
        self.assertEqual(self.names(rep, own), ["hogan_x 1", "hogan_x 3", "hogan_x 4"])

    def test_partial_off_and_hidden(self):                     # U1 "unlisted": "off"; hidden ones never put back
        write(os.path.join(self.root, "families.json"), {
            "families": [{"id": "x", "unlisted": "off", "variants": [v("sting_x", 1), v("hogan_x", 2)]},
                         {"id": "y", "variants": [v("muta_x", 1)]}],
            "hidden": [{"wrestler": "muta_x", "variant": 3}]})
        rep = self.check()
        self.assertEqual(self.names(rep, self.fam(rep, "x")), ["sting_x 1", "hogan_x 2"])
        self.assertFalse(any(f[1] == "hogan_x" for f in rep.families))
        self.assertEqual(self.names(rep, self.fam(rep, "y")), ["muta_x 1", "muta_x 2", "muta_x 4"])

    def test_default_has_no_second_looks(self):                # (2026-11-15, revfix: the ports' "_b" looks dropped)
        rep = self.check()
        self.assertEqual(len(rep.families), 2)
        self.assertFalse(any(w.endswith("_b") for f in fc.default_json()["families"] for w in (x["wrestler"] for x in f["variants"])))

    def test_gone_wrestler_is_a_warning(self):                 # F3 / H1: not there now -> warning, left out while he isn't
        write(os.path.join(self.root, "families.json"), {"families": [{"id": "x", "variants": [v("sting_x", 1), v("deleted_guy", 1)]}],
                                                          "hidden": [{"wrestler": "deleted_guy"}, {"variant": 2}]})
        rep = self.check()
        self.assertEqual([x for _, x in rep.errors], ['hidden 2: needs "wrestler": not read (H1)'])
        self.assertEqual(sum("deleted_guy" in x for _, x in rep.warnings), 2)

    def test_family_order_is_only_f4(self):                     # the order of families changes nothing but F4
        a = {"id": "a", "variants": [v("sting_x", 1), v("sting_x", 2)]}
        b = {"id": "b", "variants": [v("hogan_x", 1), v("muta_x", 1)]}
        write(os.path.join(self.root, "families.json"), {"families": [a, b]})
        r1 = {f[1]: f[3] for f in self.check().families}
        write(os.path.join(self.root, "families.json"), {"families": [b, a]})
        r2 = {f[1]: f[3] for f in self.check().families}
        self.assertEqual(r1, r2)

    def test_other_drive(self):                                 # round 4 point 6: relpath across drives
        real = os.path.relpath
        def boom(p, start=None): raise ValueError("path is on mount 'E:', start on mount 'D:'")
        os.path.relpath = boom
        try: self.assertEqual(fc.shown_path("E:/x/families.json", self.root), "E:/x/families.json")
        finally: os.path.relpath = real
        other = tempfile.mkdtemp(prefix="families_other_")
        try:
            write(os.path.join(other, "families.json"), {"families": [{"id": "x", "variants": [v("sting_x", 1)]}]})
            rep = fc.check_families(self.root, files=[os.path.join(other, "families.json")])
            self.assertEqual(rep.errors, [])
        finally: shutil.rmtree(other, ignore_errors=True)


class TestPairForm(Base):
    """(startup 2026-11-19, the user's approval "Approve, with a review") the {"id", "game_number"} pair, as the exe reads it"""
    def pair(self, sid, number): return {"id": sid, "game_number": number}

    def test_pair_resolves_by_string_id(self):
        k = fc.known_from_folder(self.root)
        sting, hogan = k.sids["sting_x"], k.sids["hogan_x"]
        cases = [(self.pair("sting_x", "0x%04X" % sting), sting),      # the number is his: the same man
                 (self.pair("sting_x", "0x%04X" % hogan), sting),      # another man's number: his string ID wins
                 (self.pair("sting_x", hogan), sting),                 # (a JSON number too)
                 (self.pair("nobody_here", "0x%04X" % hogan), hogan),  # a string ID nobody has: the number
                 ({"id": "sting_x"}, sting),                           # the string ID alone
                 ({"game_number": "0x%04X" % hogan}, hogan),           # the number alone
                 (self.pair("nobody_here", "0x0FFE"), 0),              # neither: nobody
                 ({}, 0), ({"id": 5}, 0),
                 (self.pair("Sting X", "0x%04X" % hogan), sting),      # (reviewcheckers R1) the pair's ID as clean_id makes it
                 ({"id": 5, "game_number": "0x%04X" % hogan}, hogan),  # a non-text "id": the number
                 ("Sting X", sting)]                                   # a plain value that needs clean_id: his number
        for w, want in cases:
            self.assertEqual(k.who(w), want, w)
        for w in ("sting_x", "0x%04X" % hogan, hogan, "nobody_here"):       # every older form exactly as before
            self.assertEqual(k.who(w), k.who_one(w), w)

    def test_pair_in_families_and_hidden(self):
        k = fc.known_from_folder(self.root)
        hogan = k.sids["hogan_x"]
        write(os.path.join(self.root, "families.json"), {"families": [{"id": "p", "variants": [
            {"wrestler": self.pair("sting_x", "0x%04X" % hogan), "variant": 1}, v("hogan_x", 1)]}],
            "hidden": [{"wrestler": {"id": "muta_x", "game_number": "0x%04X" % hogan, "note": 1}}]})
        rep = self.check()
        self.assertEqual(rep.errors, [])
        self.assertEqual(self.names(rep, self.fam(rep, "p"))[:2], ["sting_x 1", "hogan_x 1"])
        self.assertTrue(any('"note" isn\'t read' in x for _, x in rep.warnings))
        # (reviewcheckers R1) the one hidden is muta_x, by his string ID -- not hogan_x, whose number the pair carries
        hidden = {}
        errs, _ = fc.check_hidden_json({"hidden": [{"wrestler": {"id": "muta_x", "game_number": "0x%04X" % hogan}}]}, k, hidden)
        self.assertEqual(errs, [])
        self.assertEqual(hidden, {k.sids["muta_x"]: 0xF})

    def test_pair_clash_id_warned_as_a_plain_one(self):
        wrestler(self.root, "Other/sting_x")                    # (sting_x~2)
        write(os.path.join(self.root, "families.json"), {"families": [{"id": "s", "variants": [
            v("sting_x", 1), {"wrestler": {"id": "sting_x~2", "game_number": "0x0001"}, "variant": 1}]}]})
        rep = self.check()
        self.assertTrue(any("sting_x~2 is a clash ID" in x for _, x in rep.warnings))



class TestAutoMerge(unittest.TestCase):
    """(automerge, 2026-11-23) A1: F2's MERGE SAME WRESTLERS -- the setting as a game folder keeps it, and the families the
    game makes with it on: the roster editor's own automatic rule, asked (this checker has no copy of it)."""
    def setUp(self):
        self.game = tempfile.mkdtemp(prefix="families_auto_test_")
        self.root = os.path.join(self.game, "wrestlers")
        wrestler(self.root, "WCW/muta_x", name="THE GREAT MUTA")
        wrestler(self.root, "Japan/great_muta", name="Great Muta")      # (the same full name: a leading THE, any case)
        wrestler(self.root, "WCW/sting_x", name="STING X")

    def tearDown(self):
        shutil.rmtree(self.game, ignore_errors=True)

    def settings(self, value):
        write(os.path.join(self.game, "mod_config", "nomercy_arcade.json"), {"mod_id": "nomercy_arcade", "storage": {} if value is None else {"auto_merge": value}})

    def test_the_setting(self):
        self.assertIsNone(fc.auto_merge_setting(self.game))                 # (no settings: the game's default, off)
        for value, want in ((1, True), (0, False), (1.0, True), ("On", True), ("Off", False), (True, True), (None, None)):
            self.settings(value)
            self.assertEqual(fc.auto_merge_setting(self.game), want, value)

    def test_the_editors_rule_merges_the_same_name(self):
        if fc.editor_families(os.environ.get("UNM_EDITOR")) is None:
            self.skipTest("the roster editor (its app/families.py) isn't beside this checker: set UNM_EDITOR")
        before = sorted(os.path.relpath(os.path.join(d, f), self.game) for d, _, fs in os.walk(self.game) for f in fs)
        props, doc = fc.auto_merge(self.root, os.environ.get("UNM_EDITOR"))
        k = fc.known_from_folder(self.root)
        muta = [p for p in props if p["key"] == "same:auto:greatmuta"]
        self.assertEqual(len(muta), 1)
        self.assertEqual(muta[0]["change"], "new")
        self.assertEqual(sorted(k.name(n) for n in muta[0]["members"]), ["great_muta", "muta_x"])
        self.assertFalse(any(k.sids["sting_x"] in p["members"] and len(p["members"]) > 1 for p in props))   # (STING X: nobody's)
        fams = [f for f in doc["families"] if any(v["wrestler"] in ("muta_x", "great_muta") for v in f["variants"])]
        self.assertEqual(len(fams), 1)
        self.assertEqual(len(fams[0]["variants"]), 8)                         # (each brings his own four)
        after = sorted(os.path.relpath(os.path.join(d, f), self.game) for d, _, fs in os.walk(self.game) for f in fs)
        self.assertEqual(after, before)                                       # (nothing written: the game makes it at start-up)
        # the command line: --auto-merge game follows the game folder's setting
        self.settings(1)
        self.assertEqual(fc.main([self.root, "--auto-merge", "game", "--json"] + (["--editor", os.environ["UNM_EDITOR"]] if os.environ.get("UNM_EDITOR") else [])), 0)


if __name__ == "__main__":
    unittest.main()
