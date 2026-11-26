"""His entrance theme as WCW/nWo Revenge plays it (2026-11-14, revfix), for his folder (theme.bin + theme.meta).

Revenge keeps no theme per wrestler: its songs are sequences (files 0x210-0x224), and in every match the first man to come
out gets song 0x214, the second 0x215, whoever they are (data\revport\ports\PORTS.md, section 3 -- Bret, Hogan, Hall, Nash
in singles and tags, all six arenas). So the theme Revenge plays for him is 0x214, the one the 24 built-in ports play
(mod gen/mkcontent.py EXT_SLOTS ("rev", "0214")). It is made here exactly as mkcontent.py's rev_theme makes it for the
ports -- played by us from Revenge's own sequence and instruments in the player's Revenge ROM (gen/revmusic.py theme ->
gen/revplay.py; until 2026-11-18 an emulator recording, data\gen_data\rev_songs\0214.wav), looped where the
sequence comes round again, VADPCM -- and written as a folder theme (tools\wrestler_theme.py's two files: the 0xD0-byte wave
record, then its level, whether it loops, two spare bytes and the tuning), so the folder carries it and needs no music slot.
A folder that already has a theme of its own keeps it (the handler never writes over one)."""
import os, struct, sys
from .. import DEV
sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
SONG = "0214"
_made = {}
def encode(pcm, ls, rate):
    """a theme sample (PCM int16, loop start, rate) -> (theme.bin, theme.meta, its length in samples): VADPCM, then the
    0xD0-byte wave record (data length, book, loop from ls to the end for ever, the decoder's state at the loop) and the
    row's level 0, loops, two spare bytes, tune 0xC2400000 (as mkcontent.py's rows)"""
    import vadpcm
    book, data, ns = vadpcm.encode(pcm, int(round(rate)))
    dec = vadpcm.decode(data, book, ns)
    state = dec[ls - 16: ls] if ls >= 16 else [0] * 16
    w = bytearray(0xD0)
    struct.pack_into(">IIBB", w, 0, 0, len(data), 0, 1)
    w[0x18:0x18 + len(book)] = book
    struct.pack_into(">IIi16h", w, 0xA0, ls, ns, -1, *[int(v) for v in state])
    return bytes(data), bytes(w) + struct.pack(">BBxxI", 0, 1, 0xC2400000), ns
def theme_files(song=SONG):
    """(theme.bin bytes, theme.meta bytes, about) for Revenge's song (made once a run)"""
    if song not in _made:
        import revmusic
        pcm, ls, rate, info = revmusic.theme(song)
        data, meta, ns = encode(pcm, ls, rate)
        about = ("Revenge's song %s, as Revenge plays it at his entrance (%.1f s, looping from %.1f s; the 24 built-in ports' "
                 "theme too)" % (song, ns / rate, ls / rate))
        _made[song] = (data, meta, about)
    return _made[song]
def write(folder, song=SONG):
    """theme.bin / theme.meta into his folder, unless he has a theme already (theme.bin, or a theme.* sound file).
    -> what was done, for the report"""
    have = [f for f in os.listdir(folder) if f.lower().startswith("theme.")]
    if have: return "his own theme kept (%s)" % ", ".join(sorted(have))
    data, meta, about = theme_files(song)
    open(os.path.join(folder, "theme.bin"), "wb").write(data)
    open(os.path.join(folder, "theme.meta"), "wb").write(meta)
    return about

# ---- (2026-11-14, revfix) a theme the project gave him: the 24 ports' music files ------------------------------------------
# The built-in ports' themes were music files the user gave (mod\music\<his name>.mp3: gen/mkcontent.py USER_MUSIC, matched
# by name -- 17 of them), else Revenge's 0x214 (Jim Neidhart). In his folder they are made the way tools\wrestler_theme.py
# makes any music file a folder theme (gen/mp3theme.py: mono, the game's theme rate and level, its loop).
MUSIC_DIR = os.path.join(DEV, "mod", "music")
def music_file_for(name):
    """the music file named for him (mkcontent.py's match: mp3theme.norm of the file's name against his), or None"""
    import mp3theme
    key = mp3theme.norm(name)
    for f in sorted(os.listdir(MUSIC_DIR)) if os.path.isdir(MUSIC_DIR) else []:
        if mp3theme.norm(os.path.splitext(f)[0]) == key: return os.path.join(MUSIC_DIR, f)
    return None
def write_file(folder, path):
    """theme.bin / theme.meta from a music file, as tools\wrestler_theme.py writes them (unless he has a theme) -> about"""
    have = [f for f in os.listdir(folder) if f.lower().startswith("theme.")]
    if have: return "his own theme kept (%s)" % ", ".join(sorted(have))
    import mp3theme
    pcm, ls, rate, info = mp3theme.theme(path)
    data, meta, ns = encode(pcm, ls, rate)
    open(os.path.join(folder, "theme.bin"), "wb").write(data)
    open(os.path.join(folder, "theme.meta"), "wb").write(meta)
    return "the theme the project gave him: mod\music\%s (%.1f s, looping from %.1f s)" % (os.path.basename(path), ns / rate, ls / rate)

# ---- (2026-11-18, reduxthemes) the theme WCW/nWo Revenge Redux gives him -- the user's decision C -----------------------------
# Revenge Redux (a third-party patch over the Revenge ROM) gives each of the Superstars the project once took its themes for
# (gen/reduxmusic.py WRESTLERS: the ports' 15 + their second looks) an entrance song of his own. Those themes are built from
# the player's own patch (roms\: the patched ROM, or Revenge + its xdelta, by fingerprint), played as Redux plays it
# (gen/reduxmusic.py -> gen/revplay.py). Without the patch he gets Revenge's own theme (write above), as every Revenge man.
_redux = {}
def redux_song(name=None, code=None):
    """his Redux song (a port by his name, any other Revenge man by his model code), or None (no Redux song for him, or no
    Redux patch in roms\\)"""
    import reduxmusic
    if code is None and name is None: return None
    rom = reduxmusic.redux_rom()
    if rom is None: return None
    if name is not None and reduxmusic.of_name(name): return reduxmusic.song_of(rom, name)
    return reduxmusic.song_for(rom, code) if code is not None else None
def redux_files(song):
    """(theme.bin, theme.meta, about) of Redux's song (gen/reduxmusic.py: its clip at its own speed, the old themes' level)"""
    import reduxmusic
    if song not in _redux:
        pcm, ls, rate, about = reduxmusic.theme(reduxmusic.redux_rom(), song)
        data, meta, ns = encode(pcm, ls, rate)
        _redux[song] = (data, meta, about)
    return _redux[song]
def write_own(folder, name, code=None):
    """his theme into his folder, unless it has one: the music file named for him (mod\\music: the player's), else Redux's
    (when the player has the patch and Redux gives him a song: a port by name, the others by their model code), else
    Revenge's own 0x214 -> what was done, for the report"""
    mp3 = music_file_for(name)
    if mp3: return write_file(folder, mp3)
    song = redux_song(name, code)
    if song is None: return write(folder)
    have = [f for f in os.listdir(folder) if f.lower().startswith("theme.")]
    if have: return "his own theme kept (%s)" % ", ".join(sorted(have))
    rx = redux_files(song)
    open(os.path.join(folder, "theme.bin"), "wb").write(rx[0])
    open(os.path.join(folder, "theme.meta"), "wb").write(rx[1])
    return rx[2]
