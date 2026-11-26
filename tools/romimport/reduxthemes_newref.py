"""reduxthemes_newref.py (reduxthemes, 2026-11-18): the NEW reference for the user's decision C (Revenge Redux's entrance themes
built from the player's Redux patch, gen/reduxmusic.py: each clip at its own speed, as loud as the old themes; every Revenge
man Redux gives a song).

Checks that a ROM-only chain run (chain.py --only legacy_in,mkcontent,imports in a worktree with this change) differs from
the references before it (romrender's / revmusic's overlays, chain.py ref_of without this one) ONLY where Redux's themes are:
  content_index.h   every line the same but the instrument / song tables, the music-slot lines (user_music, EXT_SLOTS_N,
                    ext_slot_fx, their counts) and the sizes; every instrument but the 15 Redux MP3s' the same (record, note,
                    data) in the same order; one "Revenge Redux song 0xNN" row per song the ports play, its data decoding
                    (VADPCM) to reduxmusic.theme; every Superstar with a music slot before still has one, playing the same
                    instrument (or, for the 15 MP3s, his Redux song); the new ones are Redux songs of ports with no music
                    file (Jim Neidhart); every other song the same
  content.bin       those instruments' data and the Titantron frames the same bytes
  the folders       only theme.bin / theme.meta and wrestler.json's source.theme note, only in the Revenge folders whose
                    model code has a Redux song (reduxmusic.song_for), each now theme.py redux_files(his song)
then (--write) copies them into the overlay private\\reference\\reduxthemes (the latest wins in chain.py's ref_of).

    python reduxthemes_newref.py [--write] [--run <worktree>] [--wrestlers <the run's folders>]
    python reduxthemes_newref.py --combine        (only the combined wrestler.json expectations: combine())"""
import argparse, filecmp, json, os, re, shutil, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))
PRIV = os.path.dirname(os.path.dirname(SRC))
OVERLAY = os.path.join(PRIV, "reference", "reduxthemes")
BIN = os.path.join(PRIV, "build", "mod", "build", "content.bin")
IDX = os.path.join(PRIV, "generated", "mod", "src", "content_index.h")
WRESTLERS = os.path.join(PRIV, "reference", "wrestlers_tested")
INST = re.compile(r"^\s*\{ 0x([0-9A-F]+), 0x([0-9A-F]+), \{ ([^}]*) \}, (\d+), 0x([0-9A-F]+), (\d) \},\s*// (.*)$")
SONG = re.compile(r"^\s*\{ (\d+), (\d+), (\d+), (\d+), \{ ([^}]*) \} \},?$")
VARIES = ("CONTENT_SIZE", "CONTENT_THEMES_SIZE", "CONTENT_FRAMES", "CT_INSTRUMENTS", "CT_SONGS", "SONGS_N", "EXT_SLOTS_N",
          "USER_MUSIC_N")

def previous(p, overlays=("romrender", "revmusic")):
    """the reference before this decision: the latest earlier overlay with a copy, else the old one"""
    for r in reversed(overlays):
        q = os.path.join(PRIV, "reference", r, os.path.relpath(p, PRIV))
        if os.path.exists(q): return q
    return p
def mine_or_previous(p):
    """for the folders: this overlay's combined wrestler.json (combine()) first"""
    q = os.path.join(OVERLAY, os.path.relpath(p, PRIV))
    return q if os.path.exists(q) and p.endswith("wrestler.json") else previous(p)

def parse(path):
    lines = open(path).read().splitlines()
    insts, songs, rest, um = [], {}, [], None
    defs_ms = []
    for l in lines:
        m, s = INST.match(l), SONG.match(l)
        if m: insts.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3), int(m.group(4)), m.group(5), m.group(6), m.group(7)))
        elif s: songs[int(s.group(1))] = (int(s.group(2)), s.group(3), s.group(4), s.group(5))
        elif l.startswith("static const u16 user_music"):
            um = [(int(a, 16), int(b)) for a, b in re.findall(r"\{ 0x([0-9A-F]+), (\d+) \}", l)]
        elif l.startswith("static const u32 ext_slot_fx"): rest.append("ext_slot_fx")
        elif l.startswith("s16 media_songs"):                     # (the song table: one entry a music slot's song more)
            rest.append("media_songs"); defs_ms.extend(int(v) for v in re.findall(r"-?\d+", l.split("=", 1)[1]))
        else:
            d = re.match(r"^#define (\w+) ", l)
            rest.append(("#define " + d.group(1)) if d and d.group(1) in VARIES else l)
    defs = dict(re.findall(r"#define (\w+) (?:0x)?([0-9A-Fa-f]+)", "\n".join(lines)))
    defs["media_songs"] = defs_ms
    return insts, songs, rest, um, defs

def combine():
    """(after main merged romrender, 2026-11-18) a wrestler.json both earlier overlays changed -- romrender's (its photo note;
    the source.abilities note as made today) and revmusic's (its source.theme note) -- is expected with BOTH: romrender's text
    with revmusic's theme note put in (replaced in place). Written into this overlay. -> how many"""
    rr, rm = (os.path.join(PRIV, "reference", r) for r in ("romrender", "revmusic"))
    n = 0
    for r, _, fs in os.walk(rm):
        for f in fs:
            if f != "wrestler.json": continue
            rel = os.path.relpath(os.path.join(r, f), rm)
            a, b = os.path.join(rr, rel), os.path.join(rm, rel)
            if not os.path.exists(a): continue
            dst = os.path.join(OVERLAY, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, "w", encoding="utf-8", newline="").write(theme_note(a, json.load(open(b, encoding="utf-8"))["source"]["theme"])); n += 1
    return n
def theme_note(path, new):
    """the file's text with its source.theme note replaced in place by 'new' (every other byte the same)"""
    t = open(path, encoding="utf-8", newline="").read()
    old = json.loads(t)["source"]["theme"]
    eo, en = json.dumps(old)[1:-1], json.dumps(new)[1:-1]
    assert t.count(eo) == 1, path
    t = t.replace(eo, en)
    x = json.loads(t); y = json.load(open(path, encoding="utf-8")); y["source"]["theme"] = new
    assert x == y, path
    return t

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true"); ap.add_argument("--combine", action="store_true")
    ap.add_argument("--run", default=SRC); ap.add_argument("--wrestlers")
    A = ap.parse_args()
    if A.combine:
        print("combined wrestler.json: %d written under %s" % (combine(), os.path.relpath(OVERLAY, PRIV))); return 0
    new_bin, new_idx = os.path.join(A.run, "mod", "build", "content.bin"), os.path.join(A.run, "mod", "src", "content_index.h")
    new_w = A.wrestlers or os.path.join(A.run, "scratch", "chain", "imports")
    old_bin, old_idx = previous(BIN), previous(IDX)
    print("against: %s, %s" % (os.path.relpath(old_bin, PRIV), os.path.relpath(old_idx, PRIV)))
    sys.path.insert(0, os.path.join(A.run, "mod", "gen")); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(A.run, "tools", "handlers"))
    import reduxmusic, revplay, numpy as np
    from handlers.revenge import theme as TH
    rom = reduxmusic.redux_rom()
    assert rom is not None, "no Revenge Redux in roms\\"
    players = set(os.listdir(os.path.join(A.run, "mod", "music")))        # (the player's own songs: still music files)
    mp3 = {n + ".mp3": n for n in reduxmusic.names() if n + ".mp3" not in players}   # (the old Redux renders)
    oi, os_, orest, oum, odef = parse(old_idx)
    ni, ns_, nrest, num_, ndef = parse(new_idx)
    ob, nb = open(old_bin, "rb").read(), open(new_bin, "rb").read()
    assert orest == nrest, "content_index.h: a line outside the tables / music-slot lines / sizes differs"
    # the instruments
    gone = {k for k, r in enumerate(oi) if r[6].split(" (")[0] in mp3}
    rx = {k: r for k, r in enumerate(ni) if r[6].startswith("Revenge Redux song 0x")}
    ko, kn = [k for k in range(len(oi)) if k not in gone], [k for k in range(len(ni)) if k not in rx]
    assert len(gone) == 15 and len(ko) == len(kn), "instruments: %d gone, %d / %d kept" % (len(gone), len(ko), len(kn))
    remap = {}
    for a, b in zip(ko, kn):
        assert oi[a][1:] == ni[b][1:] and ob[oi[a][0]:oi[a][0] + oi[a][1]] == nb[ni[b][0]:ni[b][0] + ni[b][1]], "instrument %s" % oi[a][6]
        remap[a] = b
    by_song = {}
    for k, r in rx.items():
        s = int(r[6][len("Revenge Redux song 0x"):][:2], 16)
        wave = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", r[2]))
        y, ls, rate, about = reduxmusic.theme(rom, s)
        order, npred = struct.unpack_from(">ii", wave, 0x18)
        book = struct.unpack_from(">%dh" % (order * npred * 8), wave, 0x20)
        n = struct.unpack_from(">I", wave, 0xA4)[0]
        block = nb[r[0]:r[0] + r[1]]
        dec = np.array(revplay.vadpcm_decode(block, book, 0, len(block) // 9, 0, 0)[:n], float)
        assert n == len(y) and struct.unpack_from(">I", wave, 0xA0)[0] == ls
        snr = 10 * np.log10((y.astype(float) ** 2).mean() / ((dec - y) ** 2).mean())
        assert snr > 15, "Redux song 0x%02X: SNR %.1f dB" % (s, snr)
        by_song[s] = k
    print("instruments: %d the same (record, note, data); the 15 Redux MP3 rows gone; %d Revenge Redux rows (songs %s), each "
          "decoding to its clip theme" % (len(ko), len(rx), " ".join("0x%02X" % s for s in sorted(by_song))))
    # the music slots: every Superstar's slot plays the same instrument as before, or his Redux song
    song_of_slot = lambda slot, d: int(d["SONG_EXT0"]) + slot - int(d["EXT_SLOT0"])
    o_ids, n_ids = dict(oum), dict(num_)
    assert set(o_ids) <= set(n_ids), "a Superstar lost his music slot"
    user_songs_o = {song_of_slot(s, odef) for s in o_ids.values()}; user_songs_n = {song_of_slot(s, ndef) for s in n_ids.values()}
    for i, slot in n_ids.items():
        nsng = ns_[song_of_slot(slot, ndef)]
        if i in o_ids:
            osng = os_[song_of_slot(o_ids[i], odef)]
            assert osng[1:] == nsng[1:]
            if osng[0] in remap: assert nsng[0] == remap[osng[0]], "0x%04X: his instrument" % i
            else:
                name = mp3[oi[osng[0]][6].split(" (")[0]]
                assert nsng[0] == by_song[reduxmusic.song_of(rom, name)], "0x%04X (%s): not his Redux song" % (i, name)
        else:
            assert nsng[0] in rx, "0x%04X: a new music slot that is not a Redux song" % i
    added = sorted(set(n_ids) - set(o_ids))
    assert len(ndef["media_songs"]) - len(odef["media_songs"]) == len(set(n_ids.values())) - len(set(o_ids.values())), "media_songs"
    other_o = {k: v for k, v in os_.items() if k not in user_songs_o}; other_n = {k: v for k, v in ns_.items() if k not in user_songs_n}
    assert sorted(other_o) == sorted(other_n) and all(other_n[k][1:] == v[1:] and other_n[k][0] == remap.get(v[0], -1) for k, v in other_o.items()), "the other songs"
    assert ob[int(odef["CONTENT_FRAMES"], 16):] == nb[int(ndef["CONTENT_FRAMES"], 16):], "the Titantron frames"
    print("music slots: %d Superstars as before (the 15 MP3s' now their Redux songs), %d new: %s; %d other songs the same; "
          "frames the same; content.bin %d -> %d" % (len(o_ids), len(added), " ".join("0x%04X" % i for i in added), len(other_o), len(ob), len(nb)))
    # the folders
    changed, notes, nf = [], [], 0
    for col in ("No Mercy", "Revenge", "WrestleMania 2000", "Imported"):
        pa, pb = os.path.join(new_w, col), os.path.join(WRESTLERS, col)
        fa = {os.path.relpath(os.path.join(r, f), pa) for r, _, fs in os.walk(pa) for f in fs}
        fb = {os.path.relpath(os.path.join(r, f), pb) for r, _, fs in os.walk(pb) for f in fs}
        assert fa == fb, "%s: the files differ" % col
        for f in sorted(fa):
            ref = mine_or_previous(os.path.join(pb, f))
            if filecmp.cmp(os.path.join(pa, f), ref, shallow=False): continue
            assert col == "Revenge", "%s: %s differs" % (col, f)
            d = os.path.dirname(os.path.join(pa, f)); w = json.load(open(os.path.join(d, "wrestler.json"), encoding="utf-8"))
            song = reduxmusic.song_for(rom, reduxmusic.code_of_folder(w.get("string_id") or w["id"], w["source"].get("code")))
            assert song is not None, "%s: %s differs, and he has no Redux song" % (col, f)
            if os.path.basename(f) == "wrestler.json":
                x, y = json.load(open(os.path.join(pa, f), encoding="utf-8")), json.load(open(ref, encoding="utf-8"))
                y["source"]["theme"] = x["source"]["theme"]
                assert x == y, "%s: %s differs beyond its theme note" % (col, f); notes.append((col, f, x["source"]["theme"], ref))
            else:
                assert os.path.basename(f) in ("theme.bin", "theme.meta"), "%s: %s differs" % (col, f)
                rxf = TH.redux_files(song)
                assert open(os.path.join(pa, f), "rb").read() == rxf[0 if f.endswith(".bin") else 1], "%s: %s is not his Redux theme" % (col, f)
                changed.append((col, f)); nf += f.endswith(".bin")
    print("folders: only theme.bin / theme.meta and the theme note differ, in %d Revenge folders -- each his Redux song's theme" % nf)
    if A.write:
        put = [(new_bin, BIN), (new_idx, IDX)] + [(os.path.join(new_w, c, f), os.path.join(WRESTLERS, c, f)) for c, f in changed]
        for src, ref in put:
            dst = os.path.join(OVERLAY, os.path.relpath(ref, PRIV)); os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
        for c, f, note, ref in notes:
            dst = os.path.join(OVERLAY, os.path.relpath(os.path.join(WRESTLERS, c, f), PRIV)); os.makedirs(os.path.dirname(dst), exist_ok=True)
            t = theme_note(ref, note)
            open(dst, "w", encoding="utf-8", newline="").write(t)
        print("written: %d files under %s" % (len(put) + len(notes), os.path.relpath(OVERLAY, PRIV)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
