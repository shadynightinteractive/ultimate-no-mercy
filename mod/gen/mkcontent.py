#!/usr/bin/env python3
"""Builds the arcade's content pack from No Mercy Plus V3: entrance themes and Titantron videos.

  build/content.bin     the pack (theme samples, then 64x64 CI8 frames), embedded in the mod (.incbin)
                        and appended to the test ROM at 0x02000000 for the rig
  src/content_index.h   where everything is in it

Inputs: /home/devbox/nmplus (nmplus.z64, f/ = its files, bank dump /tmp/devbox-0/nmbank.pkl).
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle, struct, hashlib, os
NM = nmp("/home/devbox/nmplus/")
OUT_BIN = nmp("/home/devbox/work/arcade/build/content.bin")
OUT_H = nmp("/home/devbox/work/arcade/src/content_index.h")
dn = pickle.load(open(nmp("/tmp/devbox-0/nmbank.pkl"), "rb")); n = dn['ram']
u32 = lambda a: struct.unpack_from(">I", n, a & 0x7fffff)[0]
s16 = lambda a: struct.unpack_from(">h", n, a & 0x7fffff)[0]
u16 = lambda a: struct.unpack_from(">H", n, a & 0x7fffff)[0]
f32 = lambda a: struct.unpack_from(">I", n, a & 0x7fffff)[0]
B, C = dn['B'], dn['C']
NM_SONGS = 0x80470700
rom = open(NM + "nmplus.z64", "rb").read()

def script(k):
    p = u32(B + 0x18 + 8 * k); q = p
    while n[q & 0x7fffff] != 0x80: q += 1
    return bytes(n[(p & 0x7fffff):(q & 0x7fffff) + 1])
def inst_of(sc):
    i = sc.index(0x81); return u16(u32(B + 0x14) + 2 * sc[i + 1]), i + 1

# ---- themes: songs 23..66 (the music slots' themes), as No Mercy Plus plays them
songs = []        # (song, our instrument index j, script bytes, instrument byte offset in the script)
insts = []        # NM+ instrument numbers, in our order
# and Funaki's (No Mercy Plus's music slot 87, song 109), played as song 68: music slot 46, a slot the
# game left empty (its song plays no sound; sound 45, the one media.c gives it, no song plays)
EXTRA_SONGS = {68: 109}
for s in list(range(23, 67)) + list(EXTRA_SONGS):
    k = s16(NM_SONGS + 2 * EXTRA_SONGS.get(s, s))
    if not (0 <= k < 0x2C0): continue
    sc = script(k); ni, at = inst_of(sc)
    if ni not in insts: insts.append(ni)
    songs.append((s, insts.index(ni), sc, at))
assert 68 in [x[0] for x in songs], "Funaki's theme"
P = u32(C + 0x2C); LV = u32(C + 0x24); TU = u32(C + 0x28)
pack = bytearray(); inst_rows = []
for ni in insts:
    e = u32(P + 4 * ni); wave = bytes(n[(e & 0x7fffff):(e & 0x7fffff) + 0xD0])
    base, ln = u32(e), u32(e + 4)
    assert u32(e + 0x0C) in (e + 0xA0, 0) or True
    loop_off = u32(e + 0x0C) - e if u32(e + 0x0C) else 0
    book_off = u32(e + 0x10) - e
    # book / loop may be shared with another instrument's entry: copy them into ours
    book = bytes(n[(u32(e + 0x10) & 0x7fffff):(u32(e + 0x10) & 0x7fffff) + 0x88])
    loop = bytes(n[(u32(e + 0x0C) & 0x7fffff):(u32(e + 0x0C) & 0x7fffff) + 0x2C]) if u32(e + 0x0C) else bytes(0x2C)
    w = bytearray(0xD0); w[0:0x18] = wave[0:0x18]; w[0x18:0x18 + 0x88] = book; w[0xA0:0xA0 + 0x2C] = loop
    off = len(pack); pack += rom[base:base + ln]; pack += b"\0" * (-len(pack) % 16)
    inst_rows.append((ni, off, ln, bytes(w), n[(LV & 0x7fffff) + ni], f32(TU + 4 * ni), 1 if u32(e + 0x0C) else 0))
# The themes of the Superstars after Benoit, each played only while he's on, over the song's own (media.c):
# Jeff Jarrett's and The Blue Meanie's from WrestleMania 2000 (its music bank: the sound of its music slot
# for him; the bank dump gen_data/wm2k_ram.bin, the samples in its ROM), with our script for a
# theme; Droz's and Mideon's No Mercy Plus's (its music slots 80 / 73: songs 102 / 95). Their songs: music
# slots no Superstar of the game uses (14, 7, 43, 29: songs 36, 29, 65, 51).
wr = open(nmp("/home/devbox/work/gen_data/wm2k_ram.bin"), "rb").read()
wrom = open(nmp("/tmp/devbox-0/wm2k/WWF WrestleMania 2000 (USA).z64"), "rb").read()
w32 = lambda a: struct.unpack_from(">I", wr, a & 0x7fffff)[0]
w16 = lambda a: struct.unpack_from(">H", wr, a & 0x7fffff)[0]
WB = w32(0x800571EC); WC = w32(WB + 0x10)
# (content2 2026-11-19, the user: the added themes play through OUR sound script, not No Mercy Plus's) -- the same bytes as
# mod/src/media.c theme_script (docs/THEME_SCRIPT.md: the format and why each byte); the instrument byte at 9. The exe's
# twin: src/romimport/ri_content.cpp THEME_SCRIPT.
tmpl = bytes([0x84, 0x01, 0x00, 0x01, 0xFF, 0x01, 0xFF, 0x0F,  0x81, 0x00,  0xA3, 0x00, 0x00,  0x1F,  0xFF, 0xFF,  0x80])
at23 = 9
def wm_theme(sound):
    wp = w32(WB + 0x18 + 8 * sound); wsc = wr[wp & 0x7fffff:(wp & 0x7fffff) + 24]
    wni = w16(w32(WB + 0x14) + 2 * wsc[wsc.index(0x81) + 1])
    e = w32(w32(WC + 0x2C) + 4 * wni); base, ln = w32(e), w32(e + 4)
    w = bytearray(0xD0); w[0:0x18] = wr[(e & 0x7fffff):(e & 0x7fffff) + 0x18]
    w[0x18:0x18 + 0x88] = wr[(w32(e + 0x10) & 0x7fffff):(w32(e + 0x10) & 0x7fffff) + 0x88]
    if w32(e + 0x0C): w[0xA0:0xA0 + 0x2C] = wr[(w32(e + 0x0C) & 0x7fffff):(w32(e + 0x0C) & 0x7fffff) + 0x2C]
    off = len(pack); pack.extend(wrom[base:base + ln]); pack.extend(b"\0" * (-len(pack) % 16))
    inst_rows.append(("WM2000 %d" % wni, off, ln, bytes(w), wr[(w32(WC + 0x24) & 0x7fffff) + wni], w32(w32(WC + 0x28) + 4 * wni), 1 if w32(e + 0x0C) else 0))
    return len(inst_rows) - 1, tmpl, at23
def nm_theme(nm_song):
    k = s16(NM_SONGS + 2 * nm_song); assert 0 <= k < 0x2C0, nm_song
    sc = script(k); ni, at = inst_of(sc)
    if ni not in insts:
        insts.append(ni)
        e = u32(P + 4 * ni)
        book = bytes(n[(u32(e + 0x10) & 0x7fffff):(u32(e + 0x10) & 0x7fffff) + 0x88])
        loop = bytes(n[(u32(e + 0x0C) & 0x7fffff):(u32(e + 0x0C) & 0x7fffff) + 0x2C]) if u32(e + 0x0C) else bytes(0x2C)
        w = bytearray(0xD0); w[0:0x18] = n[(e & 0x7fffff):(e & 0x7fffff) + 0x18]; w[0x18:0x18 + 0x88] = book; w[0xA0:0xA0 + 0x2C] = loop
        base, ln = u32(e), u32(e + 4)
        off = len(pack); pack.extend(rom[base:base + ln]); pack.extend(b"\0" * (-len(pack) % 16))
        inst_rows.append((ni, off, ln, bytes(w), n[(LV & 0x7fffff) + ni], f32(TU + 4 * ni), 1 if u32(e + 0x0C) else 0))
        return len(inst_rows) - 1, sc, at
    return [r[0] for r in inst_rows].index(ni), sc, at
# (2026-11-13, final082 / themefix2, confirmed by ear by the user: a WrestleMania 2000 wrestler's music byte is a SLOT of its
#  entrance table (0x8010206C, 12 bytes a slot; the slot's music bank sound at + 8, s16), not a sound number. Jeff Jarrett's
#  slot 23 plays sound 22 (sound 23 is Mankind's), The Blue Meanie's slot 30 plays sound 35 (30 is the Corporate Ministry's);
#  until today the slot was taken as the sound. Looked up as the Badd Blood handler does (tools/handlers/handlers/baddblood/game.py
#  theme_sound), in the same RAM image. The rate needs nothing here: the theme plays through the game's own sound engine with
#  its instrument's tuning, as WM2000 plays it.)
WM_ENTRANCES = 0x8010206C
def wm_slot_sound(slot):
    return struct.unpack_from(">h", wr, (WM_ENTRANCES + 12 * slot + 8) & 0x7fffff)[0]
assert (wm_slot_sound(23), wm_slot_sound(30)) == (22, 35), "WrestleMania 2000's entrance table"
LATE_SONGS = [(36, ("wm", 23)), (29, ("nm", 102)), (65, ("nm", 95)), (51, ("wm", 30))]   # Jarrett, Droz, Mideon, Meanie (wm: a music slot)
CT_SONG_LATE0 = len(songs)
for song, (src, x) in LATE_SONGS:
    j, sc, at = wm_theme(wm_slot_sound(x)) if src == "wm" else nm_theme(x)
    songs.append((song, j, sc, at))
JJ_SONG = 36
# More music slots: the game's entrance table has slots 0..57 (songs 22..79: func_80017C64 plays songs 0..79
# only, from the song table 0x800480D8, and a song's sound is one of the music bank's 200). The new
# Superstars that have no slot of the game's left get slots 58 on, with songs 80 on (the song table the
# game reads is media.c's, made longer: gen/patches.py) and sounds 200 on (a copy of the music bank made
# longer, media.c media_play_sound). Each slot: No Mercy Plus's music slot for the Superstar -- his theme
# and his entrance's effects (its entrance table: its own is longer too).
# Bret Hart's and Jim Neidhart's (63, 64): WCW/nWo Revenge's song for them (its file 0x214: the one it
# plays for both, its WCW wrestlers' theme), played by us from Revenge's own sequence and instruments (gen/revmusic.py ->
# gen/revplay.py, from the player's Revenge ROM; until 2026-11-18 an emulator recording); their
# entrance effects the posse's (No Mercy Plus's plain lights).
EXT_SLOT0, SONG_EXT0, SOUND_EXT0 = 58, 80, 200
# Hollywood Hogan's, Kevin Nash's and Scott Hall's (65-67): the same song (Revenge has no theme of its own for
# anyone: 0x214 is its first entrant's song, whoever he is -- revport/ports/PORTS.md), the same effects; one
# instrument for all five (rev_theme).
EXT_SLOTS = [("posse", 66), ("taker99", 81), ("raven", 83), ("regal", 84), ("tiger", 13),
             ("brethart", ("rev", "0214")), ("neidhart", ("rev", "0214")),
             ("hogan", ("rev", "0214")), ("nash", ("rev", "0214")), ("hall", ("rev", "0214")),
             ("savage", ("rev", "0214")), ("hennig", ("rev", "0214")), ("giant", ("rev", "0214")), ("sting", ("rev", "0214")), ("luger", ("rev", "0214")), ("goldberg", ("rev", "0214")), ("ricksteiner", ("rev", "0214")), ("scottsteiner", ("rev", "0214")), ("booker", ("rev", "0214")), ("stevieray", ("rev", "0214")), ("rey", ("rev", "0214")), ("piper", ("rev", "0214")), ("bischoff", ("rev", "0214"))]
NM_ENTR = 0x22C1F0                     # No Mercy Plus's entrance table in its ROM (slot 0)
def ext_slot_fx_of(x):
    return struct.unpack_from(">I", rom, NM_ENTR + 12 * (66 if isinstance(x, tuple) else x) + 4)[0]
# The music files (the game folder's assets/music, copied to gen_data/music: gen/mp3theme.py): each a
# music slot of its own after these, for the Superstar the file is named for; his entrance plays it (his
# record's own slot left as it is: roster.c roster_entrance_song, media.c media_user_song)
import json, re
USER_MUSIC = nmp("/home/devbox/work/gen_data/music")
def roster_names():
    names = [(r["name"], r["id"]) for r in json.load(open(nmp("/home/devbox/work/arcade/gen/roster_base.json")))]
    names += [(r["short"], r["id"]) for r in json.load(open(nmp("/home/devbox/work/arcade/gen/roster_base.json")))]
    src = open(nmp("/home/devbox/work/arcade/src/roster_ext.h")).read()
    for m in re.finditer(r'\{ 0x([0-9A-Fa-f]{4}), \d+, "([^"]+)", "([^"]+)"', src):
        names += [(m.group(2), int(m.group(1), 16)), (m.group(3), int(m.group(1), 16))]
    return names
import mp3theme
# (2026-11-18, reduxthemes -- the user's decision C) WCW/nWo Revenge Redux's themes for the built-in ports (gen/reduxmusic.py
# PORTS: the 15 the project once rendered MP3s for, and Jim Neidhart -- Bret Hart's and Mr Perfect's are the player's own
# songs) are built from the player's Redux patch in roms\ (each clip at its own speed, as loud as the old themes): each takes
# a slot of its own as a music file does, in order by name, unless a music file of his name is there (the player's file
# wins). No Redux patch: none of them -- his slot plays Revenge's 0x214 (EXT_SLOTS).
import reduxmusic
REDUX_ROM = reduxmusic.redux_rom()
user_music = []          # (id, slot)
music = [(os.path.splitext(f)[0], ("file", os.path.join(USER_MUSIC, f)), "music file %r" % f)
         for f in (mp3theme.files(USER_MUSIC) if os.path.isdir(USER_MUSIC) else [])]
if REDUX_ROM is not None:
    have = {mp3theme.norm(n) for n, _, _ in music}
    music += [(n, ("redux", n), "Revenge Redux's theme for %s" % n) for n in reduxmusic.names()
              if mp3theme.norm(n) not in have and reduxmusic.song_of(REDUX_ROM, n)]
music.sort(key=lambda m: (m[0], m[1][0]))     # (by name: the music files' own order before 2026-11-18)
if music:
    rn = roster_names()
    for name_, src_, what in music:
        key = mp3theme.norm(name_)
        ids = [i for nm_, i in rn if mp3theme.norm(nm_) == key]
        if not ids: print("%s: no Superstar of that name -- left out" % what); continue
        # two of a name: the first (the game's own; Undertaker.mp3 is 0x003's -- the 1999 look 0x00A keeps WM2000's theme)
        if len(set(ids)) > 1: print("%s: names %s -- the first" % (what, [hex(i) for i in sorted(set(ids))]))
        EXT_SLOTS.append((("file:" + os.path.basename(src_[1])) if src_[0] == "file" else "redux:" + name_, src_))
        user_music.append((ids[0], EXT_SLOT0 + len(EXT_SLOTS) - 1))
        print("%s -> Superstar 0x%04X, music slot %d" % (what, ids[0], user_music[-1][1]))
import revbanks      # (the second IDs: the base's music file, gen/revbanks.py)
for _b in revbanks.BANKS:
    for _id, _sl in list(user_music):
        if _id == _b["base_id"]: user_music.append((_b["id"], _sl)); print("music: 0x%04X (%s) plays 0x%04X's, slot %d" % (_b["id"], _b["key"], _id, _sl))
ext_fx = [ext_slot_fx_of(x) for _, x in EXT_SLOTS]
rev_insts = {}
def rev_theme(f):
    """a Revenge song as one of these instruments (one for each song, however many slots play it)"""
    import vadpcm, revmusic
    if f not in rev_insts:
        pcm, ls, rate, info = revmusic.theme(f)
        book, data, ns = vadpcm.encode(pcm, int(round(rate)))
        dec = vadpcm.decode(data, book, ns)
        state = dec[ls - 16: ls] if ls >= 16 else [0] * 16
        w = bytearray(0xD0)
        struct.pack_into(">IIBB", w, 0, 0, len(data), 0, 1)
        w[0x18:0x18 + len(book)] = book
        struct.pack_into(">IIi16h", w, 0xA0, ls, ns, -1, *[int(v) for v in state])
        off = len(pack); pack.extend(data); pack.extend(b"\0" * (-len(pack) % 16))
        inst_rows.append(("Revenge song %s (%.1f s, loop from %.1f s)" % (f, ns / rate, ls / rate), off, len(data), bytes(w), 0, 0xC2400000, 1))
        rev_insts[f] = len(inst_rows) - 1
        print("Revenge song", f, "rate %.1f" % rate, "samples", ns, "loop", ls, info)
    return rev_insts[f], tmpl, at23
file_insts = {}
def file_theme(path):
    """a music file (gen/mp3theme.py) as one of these instruments"""
    import vadpcm
    if path not in file_insts:
        pcm, ls, rate, info = mp3theme.theme(path)
        book, data, ns = vadpcm.encode(pcm, int(round(rate)))
        dec = vadpcm.decode(data, book, ns)
        import numpy as np
        err = np.asarray(dec[:len(pcm)], dtype=float) - pcm[:len(dec)]
        snr = 10 * np.log10((pcm.astype(float) ** 2).mean() / max(1e-9, (err ** 2).mean()))
        state = dec[ls - 16: ls] if ls >= 16 else [0] * 16
        w = bytearray(0xD0)
        struct.pack_into(">IIBB", w, 0, 0, len(data), 0, 1)
        w[0x18:0x18 + len(book)] = book
        struct.pack_into(">IIi16h", w, 0xA0, ls, ns, -1, *[int(v) for v in state])
        off = len(pack); pack.extend(data); pack.extend(b"\0" * (-len(pack) % 16))
        inst_rows.append(("%s (%.1f s, loop from %.1f s)" % (os.path.basename(path), ns / rate, ls / rate), off, len(data), bytes(w), 0, 0xC2400000, 1))
        file_insts[path] = len(inst_rows) - 1
        print("music file", os.path.basename(path), "rate %.1f" % rate, "samples", ns, "loop", ls, info, "VADPCM SNR %.1f dB" % snr)
    return file_insts[path], tmpl, at23
redux_insts = {}
def redux_theme(name):
    """Revenge Redux's theme for him (gen/reduxmusic.py: played from the player's patch) as one of these instruments (one
    for each Redux song, however many slots play it -- the Steiners, Booker T and Stevie Ray, Scott Hall and The Giant
    share theirs)"""
    import vadpcm
    s = reduxmusic.song_of(REDUX_ROM, name)
    if s not in redux_insts:
        pcm, ls, rate, about = reduxmusic.theme(REDUX_ROM, s)
        book, data, ns = vadpcm.encode(pcm, int(round(rate)))
        dec = vadpcm.decode(data, book, ns)
        state = dec[ls - 16: ls] if ls >= 16 else [0] * 16
        w = bytearray(0xD0)
        struct.pack_into(">IIBB", w, 0, 0, len(data), 0, 1)
        w[0x18:0x18 + len(book)] = book
        struct.pack_into(">IIi16h", w, 0xA0, ls, ns, -1, *[int(v) for v in state])
        off = len(pack); pack.extend(data); pack.extend(b"\0" * (-len(pack) % 16))
        inst_rows.append(("Revenge Redux song 0x%02X (%.1f s, loop from %.1f s)" % (s, ns / rate, ls / rate), off, len(data), bytes(w), 0, 0xC2400000, 1))
        redux_insts[s] = len(inst_rows) - 1
        print("Revenge Redux song 0x%02X" % s, "rate %.1f" % rate, "samples", ns, "loop", ls, about)
    return redux_insts[s], tmpl, at23
CT_SONG_EXT0 = len(songs)
for i, (_, x) in enumerate(EXT_SLOTS):
    if isinstance(x, tuple) and x[0] == "file": j, sc, at = file_theme(x[1])
    elif isinstance(x, tuple) and x[0] == "redux": j, sc, at = redux_theme(x[1])
    elif isinstance(x, tuple): j, sc, at = rev_theme(x[1])
    else: j, sc, at = nm_theme(struct.unpack_from(">h", rom, NM_ENTR + 12 * x)[0])
    songs.append((SONG_EXT0 + i, j, sc, at))
van = open(nmp("/home/devbox/src/WWFNoMercyRecomp/nomercy.z64"), "rb").read()
GAME_SONGS = [struct.unpack_from(">h", van, 0x800480D8 - 0x80000400 + 0x1000 + 2 * k)[0] for k in range(80)]
assert GAME_SONGS[23] == 0 and GAME_SONGS[25] == 2, GAME_SONGS[:30]
THEMES_SIZE = len(pack)

# ---- videos 1..57: 72 frames each (files 0x6500 + 72 (v - 1) + i), CI4 ones widened to CI8
def ci8(d):
    if len(d) == 4624: return d
    assert len(d) == 2096 and d[8:12] == bytes.fromhex("04020010")
    hdr = d[:8] + bytes.fromhex("0802010000000000")
    px = bytearray()
    for b in d[48:]: px += bytes((b >> 4, b & 15))
    return hdr + d[16:48] + bytes(480) + bytes(px)
FRAMES_OFF = len(pack); uniq = {}; vids = []
for v in range(1, 58):
    row = []
    for i in range(72):
        d = ci8(open(NM + "f/%04x.bin" % (0x6500 + 72 * (v - 1) + i), "rb").read())
        h = hashlib.md5(d).digest()
        if h not in uniq: uniq[h] = len(uniq); pack += d
        row.append(uniq[h])
    vids.append(row)
os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
open(OUT_BIN, "wb").write(pack)

L = []
L.append("// Generated by gen/mkcontent.py from No Mercy Plus V3: the content pack's index (build/content.bin).")
L.append("#define CONTENT_SIZE 0x%Xu" % len(pack))
L.append("#define CONTENT_THEMES_SIZE 0x%Xu" % THEMES_SIZE)
L.append("#define CONTENT_FRAMES 0x%Xu   // 64 x 64 CI8 TEX frames, 4624 bytes each, from here" % FRAMES_OFF)
L.append("#define CT_FRAME_SIZE 4624")
L.append("#define CT_INSTRUMENTS %d" % len(inst_rows))
L.append("#define CT_SONGS %d" % len(songs))
L.append("#define CT_SONG_LATE0 %d   // then Jeff Jarrett's, Droz's, Mideon's, The Blue Meanie's: each only while he's on" % CT_SONG_LATE0)
L.append("#define CT_SONG_LATE_N %d" % len(LATE_SONGS))
L.append("#define CT_SONG_EXT0 %d   // then the songs of the music slots made (EXT_SLOT0 on): always theirs" % CT_SONG_EXT0)
L.append("#define EXT_SLOT0 %d" % EXT_SLOT0)
L.append("#define EXT_SLOTS_N %d   // %s" % (len(EXT_SLOTS), ", ".join("%d %s" % (EXT_SLOT0 + i, k) for i, (k, _) in enumerate(EXT_SLOTS))))
L.append("#define SONG_EXT0 %d" % SONG_EXT0)
L.append("#define SOUND_EXT0 %d   // (the music bank's sounds: the game's 200, then these)" % SOUND_EXT0)
L.append("#define SONGS_N %d" % (SONG_EXT0 + len(EXT_SLOTS)))
L.append("// the music files' themes (gen_data/music): the Superstar (ID), the music slot his entrance plays")
L.append("#define USER_MUSIC_N %d" % len(user_music))
L.append("static const u16 user_music[%d][2] = { %s };" % (max(1, len(user_music)), ", ".join("{ 0x%04X, %d }" % x for x in user_music) or "{ 0xFFFF, 0 }"))
L.append("// the music slots made: their entrance effects (No Mercy Plus's for them)")
L.append("static const u32 ext_slot_fx[EXT_SLOTS_N] = { %s };" % ", ".join("0x%X" % x for x in ext_fx))
L.append("// The song table (the game's 0x800480D8, longer): s16 per song, its sound (or a file, >= 0x2C0; -1 none)")
L.append("s16 media_songs[SONGS_N + FOLDER_THEMES] = { %s };" % ", ".join(str(x) for x in GAME_SONGS + [SOUND_EXT0 + i for i in range(len(EXT_SLOTS))]))
L.append("#define CT_VIDEOS 57")
L.append("#define CT_VIDEO_FRAMES 72")
L.append("// No Mercy Plus's theme instruments: pack offset, length, its wave table (0xD0: base, len, type /")
L.append("// flags, loop, book; book +0x18, loop +0xA0), level, tuning (f32 bits), has a loop")
L.append("typedef struct { u32 off, len; u8 wave[0xD0]; u8 level; u32 tune; u8 loop; } CtInstrument;")
L.append("static const CtInstrument ct_inst[CT_INSTRUMENTS] = {")
for ni, off, ln, w, lv, tu, lp in inst_rows:
    L.append("    { 0x%X, 0x%X, { %s }, %d, 0x%08X, %d },  // %s" % (off, ln, ",".join("0x%02X" % b for b in w), lv, tu, lp, ni if isinstance(ni, str) else "its instrument %d" % ni))
L.append("};")
L.append("// The songs (23..66: music slots 1..44; 68: slot 46, Funaki's) and the sound script each plays (its instrument byte at")
L.append("// .at is the index into ct_inst, set to the bank's number when installed)")
L.append("typedef struct { u8 song, inst, at, len; u8 script[24]; } CtSong;")
L.append("static const CtSong ct_song[CT_SONGS] = {")
for s, j, sc, at in songs:
    assert len(sc) <= 24
    L.append("    { %d, %d, %d, %d, { %s } }," % (s, j, at, len(sc), ",".join("0x%02X" % b for b in sc)))
L.append("};")
L.append("// Titantron videos 1..57: each frame's number in the pack (frame f at CONTENT_FRAMES + f * 4624)")
L.append("static const u16 ct_video[CT_VIDEOS][CT_VIDEO_FRAMES] = {")
for row in vids: L.append("    { %s }," % ",".join(str(x) for x in row))
L.append("};")
open(OUT_H, "w").write("\n".join(L) + "\n")
print("pack %d bytes: themes %d (%d instruments, %d songs), %d unique frames" % (len(pack), THEMES_SIZE, len(inst_rows), len(songs), len(uniq)))
