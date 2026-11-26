#!/usr/bin/env python3
"""release_audit.py: refuses the repository if anything extracted from a game, or any ROM-derived bytes, is in it.

  python tools/release/release_audit.py [--rom <ROM or .zip> ...] [--forbid <file or folder> ...] [--no-bytes]
                                        [--report <out.txt>] [--only <path> ...] [--package <folder> [--mod-build <folder>]]

What is checked is what git would publish: the tracked files plus the untracked ones that are not ignored
(git ls-files -co --exclude-standard), from the repository root. Run it before every commit that is meant to go out, and in
CI (without --rom it does the rule checks only and says so: the result is then INCOMPLETE, exit 1 -- relprep2 2026-11-24).

1. RULES (always). A file is refused when it is
   - a ROM or disc image, a game-made pack or an archive that can't be looked into (.z64 .v64 .n64 .rvz .iso .wbfs .pak
     .7z .rar ...), or starts with an N64 ROM's first bytes in any byte order;
   - inside a wrestler or arena folder (any path with a wrestlers/ or arenas/ folder in it) and of an EXTRACTED type --
     the importers make these from the player's ROMs: model/ (meshes, textures, model.json), photo*.bin, theme.bin,
     theme.meta, video.bin, moves.json, import_report.txt, and wrestler.json anywhere but in _example/. OURS in such a
     folder (allowed): README.txt / *.md, _example/*, families.json, aliases.json, select_pages.json, arena.json;
   - one of the private folder's linked paths (tools/layout/private_links.txt): those are never published;
   - a binary data file (.bin .pkl .npy .dat .raw .rdram .st .sav .eep .mpk .wav .ogg .mp3 .png .jpg ...) that is not on
     the reviewed list ALLOWED_BINARIES below (fonts, icons, the program's own pictures with their source given).
2. BYTES (with --rom; the player's ROMs from ../private/roms are used when none are given and that folder exists): every
   file is scanned with tools/release/cleanroom.py's method -- any run of 35 or more bytes shared with a ROM (or a
   --forbid file: the game folder's content packs and wrestler folders, which hold captured data no ROM has) is found, as
   it is and byte-swapped -- and every text file's numeric arrays (C / Python / JSON lists of numbers: 0x12, 0x3456, ...)
   are turned back into the bytes they spell (each number at its own width, big-endian like the N64) and scanned the same
   way, so a header that holds game data written out as hex is caught too.

--package <folder>: audits a release package (tools/release/make_package.ps1) instead of the repository: every file in
the folder (zips and .nrm opened), the same byte scan, and the package's rules (PACKAGE_ALLOWED below: the program, its
DLLs, the mods, the UI assets; no ROM, no content pack, nothing extracted in a wrestler or arena folder, no other binary).

--mod-build (package only): the mod's build folder the package was made from (default mod/build/mod_six) -- the package's
firstrun/mod_binary.holes is checked against it (RULES 'holes').

Sources (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur with
your recommendations"): a ROM is known by its CONTENT (its SHA-1: KNOWN_ROMS below), not by its file's name, and two files with
the same ROM in them (a .zip and the .z64 taken out of it) are one source. Every source a window is found in counts
(cleanroom.Corpus.lookup_all), so a verdict doesn't depend on the order the ROMs are given in or on which copy of a ROM is
loaded; more ROMs can only make it stricter (a reviewed window found in a ROM its review didn't list is refused). A ROM not in
KNOWN_ROMS is named by its file and SHA-1 and is in no reviewed entry.

Patched ROMs (relprep2 2026-11-24; the user, 2026-11-24: "As far as audits go, use your recommendation"; independent review:
the brief audit_sources_review_brief.md): No Mercy Plus V3 and WCW/nWo Revenge Redux reach the player as third-party
patches (an .xdelta, alone or inside the release's .zip) over a ROM the audit already loads. Their bytes are sources too: each
patch named in PATCHED_ROMS (known by its own SHA-1) that is among the ROMs given, or in the ROM folder, is applied IN MEMORY
to its base (the project's VCDIFF decoder, tools/handlers/handlers/aki/vcdiff.py) and the result is used only when its SHA-1
is the patched ROM's; nothing is written. A patch whose base is there but that does not give the known ROM makes the audit
INCOMPLETE (exit 1); one whose base is missing is reported as not built -- with --package, INCOMPLETE too. No Mercy Plus V3
counts in an 'also' only beside a stock No Mercy ROM. The ROM folder is read with its subfolders. A run with no ROM source
(--no-bytes, no ROMs) is INCOMPLETE, never CLEAN. The report starts with the sources and every note about a ROM or patch.
Also INCOMPLETE: a file given as a ROM that can't be read (a corrupt .zip); with --package, a patch not read (over PATCH_MAX,
or unreadable), and a package audit without NoMercySix and a stock No Mercy ROM (v1.0 or Rev 1) among the sources.

Exit 0 clean, 1 refused (or incomplete: a patched ROM not built), 2 bad usage. Needs numpy (the toolchain's Python has it).
"""
import argparse, fnmatch, hashlib, os, re, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cleanroom as CR   # noqa: E402
import numpy as np       # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, '..', '..'))

CLOSED = ('.z64', '.v64', '.n64', '.rvz', '.iso', '.wbfs', '.gcm', '.pak', '.7z', '.rar', '.xdelta', '.bps', '.ips')
EXTRACTED_IN_FOLDERS = ('photo*.bin', 'theme.bin', 'theme.meta', 'video.bin', 'moves.json', 'import_report.txt', '*.bin')
OURS_IN_FOLDERS = ('readme.txt', '*.md', 'families.json', 'aliases.json', 'select_pages.json', 'arena.json')
BINARY_EXT = ('.bin', '.pkl', '.npy', '.npz', '.dat', '.raw', '.rdram', '.st', '.sav', '.eep', '.mpk', '.sra', '.fla',
              '.wav', '.ogg', '.mp3', '.flac', '.mp4', '.webm', '.mkv', '.avi', '.png', '.jpg', '.jpeg', '.bmp', '.gif',
              '.tga', '.dds', '.elf', '.o', '.obj', '.a', '.lib', '.exe', '.dll', '.so', '.nrm', '.zip', '.ttf', '.otf',
              '.ico', '.svg', '.icns', '.webp')
# Reviewed binaries that may be published (path patterns, from the repository root) and why.
ALLOWED_BINARIES = [
    ('mod/gen/fonts/*.ttf', 'Anton (SIL OFL 1.1, Google Fonts; OFL.txt beside it): the arena labels\' font'),
    ('exe/WWFNoMercyRecomp/assets/*.ttf', 'Inter, Noto Emoji (SIL OFL 1.1): the recomp\'s UI fonts'),
    ('exe/WWFNoMercyRecomp/assets/promptfont/*', 'PromptFont (SIL OFL 1.1, its LICENSE beside it)'),
    ('exe/WWFNoMercyRecomp/assets/icons/*', 'the recomp\'s UI icons (its own, GPL-3.0 project)'),
    ('exe/WWFNoMercyRecomp/assets/icons/*/*', 'the recomp\'s UI icons (its own, GPL-3.0 project)'),
    ('exe/WWFNoMercyRecomp/assets/mods/aki_*.nrm', 'the AkiMods (GPL-3.0, source in libs/AkiMods), as the recomp ships them'),
    ('exe/WWFNoMercyRecomp/icons/*', 'the program\'s icon (the recomp\'s)'),
    ('exe/WWFNoMercyRecomp/docs/screenshots/*', 'the recomp\'s own documentation pictures (NOT scanned as game data: review)'),
    ('exe/WWFNoMercyRecomp/third_party/ffmpeg-src/*', 'FFmpeg\'s source tarball contents (LGPL): its test and doc files'),
    ('exe/WWFNoMercyRecomp/third_party/dav1d-src/*', 'dav1d\'s source tarball contents (BSD)'),
    ('exe/WWFNoMercyRecomp/lib/*', 'vendored libraries (their own licences)'),
    ('libs/*', 'vendored libraries (their own licences)'),
    ('tools/lift/third_party/*', 'vendored m2c (GPL-3.0) and its tests'),
]


# What a release package may hold besides text (paths in the package, / between folders) -- the program and the third-party
# files it runs with. Anything else binary in a package is refused: it would be data, and data is the player's to make.
PACKAGE_ALLOWED = [
    ('Ultimate No Mercy.exe', 'the program (built from this repository)'),
    ('*.dll', 'SDL2 and our FFmpeg build (LGPL; FFmpeg-*.txt beside them)'),
    ('mods/*.nrm', 'the Ultimate No Mercy mod and the AkiMods (GPL-3.0), as built from this repository'),
    ('assets/mods/*.nrm', 'the same mods where the recomp looks for its built-in ones'),
    ('firstrun/plan.bin', "how the first start fills the mod's game-data tables from the player's ROM (tools/firstrun/mkplan.py): offsets, sizes, our own bytes"),
    ('firstrun/mod_binary.holes', "the mod's image with every table made from a game zero (tools/firstrun/mkplan.py)"),
    ('firstrun/mod_syms.bin', "the mod's symbol table, as built (ours)"),
    # (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur with
    #  your recommendations". Only the binary-file rule is lifted for this one name: its bytes are scanned like every other
    #  file's)
    ('firstrun/hdrfill.bin', "the layouts the first start fills the extras' holes by from the headers the importer makes "
     "(tools/firstrun/hdrlayout.py): MIPS o32 layouts of our own tables -- offsets, sizes, leaf indices, the link's words; "
     "no game bytes"),
    ('assets/*.ttf', 'Inter, Noto Emoji (SIL OFL 1.1): the UI fonts'),
    ('assets/promptfont/*', 'PromptFont (SIL OFL 1.1, its LICENSE beside it)'),
    ('assets/icons/*', "the recomp's UI icons"),
    ('assets/icons/*/*', "the recomp's UI icons"),
]
PACKAGE_REFUSED_FOLDERS = ('content/',)   # the content packs: made from the player's ROMs, never shipped


# The ROMs the reviews name, known by their CONTENT (the SHA-1 of the ROM in its own byte order, cleanroom.rom_order): the
# names below are what the reports and the reviewed entries say, whatever the files are called and in whatever order they are
# given. (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur
# with your recommendations".)
KNOWN_ROMS = {
    '6b4ff404b2505772fbab591e901ab96cc4891dd8': 'ROM NoMercySix',          # the project's six-man port ROM (six-engine)
    '91cee3d096f4a76644d8b35b9aead6448909abd1': 'ROM WWF No Mercy (USA) (Rev 1)',
    '3566d9b5723291937582453ad0453ce517cfc358': 'ROM WWF No Mercy (USA) v1.0',
    'e1711a2511394b9357b5f1ac8ca5cc17bd674836': 'ROM WCW-nWo Revenge (USA)',
    '442d417a52ed672ca1a47e7261a5414debb1e27a': 'ROM WWF WrestleMania 2000 (USA)',
    'aa8efede5a93cbd943b34fc2a46fe780a1e54825': 'ROM WWF WarZone - Badd Blood (USA) (v1.0)',
    # (relprep2 2026-11-24) the two patched ROMs, as PATCHED_ROMS builds them (or the patched ROM itself, if given)
    '9f67445f44db5eb464f629f6a49719753bf52abe': 'ROM No Mercy Plus V3',
    '0695b127b654a1d6b79ffe7e62fb8f2981c26d5c': 'ROM WCW-nWo Revenge Redux',
}
SIX = 'ROM NoMercySix'
NM_REV1, NM_V10 = 'ROM WWF No Mercy (USA) (Rev 1)', 'ROM WWF No Mercy (USA) v1.0'
REVENGE, WM2000 = 'ROM WCW-nWo Revenge (USA)', 'ROM WWF WrestleMania 2000 (USA)'
BADDBLOOD = 'ROM WWF WarZone - Badd Blood (USA) (v1.0)'
NMPLUS, REDUX = 'ROM No Mercy Plus V3', 'ROM WCW-nWo Revenge Redux'

# (relprep2 2026-11-24, see the docstring) the third-party patches: the patch's SHA-1 -> the ROM it makes, from which base.
# The SHA-1s are tools/romimport/roms.py's (the first start's own fingerprints for the same files).
PATCHED_ROMS = {
    '61a67d187a0d4fd34380bdfb93c7e9baafb04633': dict(name=NMPLUS, base=NM_V10, sha1='9f67445f44db5eb464f629f6a49719753bf52abe'),
    '9341b5224eeab62cacfb80d4323bf0afc602e722': dict(name=REDUX, base=REVENGE, sha1='0695b127b654a1d6b79ffe7e62fb8f2981c26d5c'),
}
PATCH_MAX = 1 << 28        # (a .zip member larger than this is not read: a patch, not a bomb)


# Reviewed byte matches. Each entry:
#   path    the file (a pattern, from the repository root / the package's root)
#   in      every window must be found in at least one of these sources (what its review explained it by)
#   also    the other sources a window may ALSO be found in -- no others
#   max     at most this many windows (None, relprep2 2026-11-24: no count -- the entry's rule holds every window)
#   why, status   'allowed', or 'flagged' = waits for the user's decision (PUBLISHING.md): listed in every report, does not
#                 refuse
#   sha256  (optional) only for this exact file
#   rule    (optional) a check the file must pass as well (RULES, in main)
# (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur with your
#  recommendations". Was: ONE source per window -- the first of the ROMs in the order given -- held to a name prefix, so a
#  verdict changed with the set or the order of the ROMs (REVIEW_ALLOWANCES.txt finding 0: all ROMs loaded refused files the
#  standard set passed). Now each window's WHOLE set of sources is held to the entry. With the standard set -- NoMercySix first,
#  then Revenge, No Mercy Rev 1 and v1.0, Badd Blood, WrestleMania 2000 -- the old prefix test said "the first of the window's
#  sources is X"; 'in' = (X,) and 'also' = only sources that come AFTER X in that order keeps every entry at least as strict as
#  it was.)
# (relprep2 2026-11-24, see the docstring: No Mercy Plus V3 is a source now) No Mercy Plus V3 is No Mercy v1.0 with a patch
#  over it, so a window of stock No Mercy is in it too: it is in 'also' wherever both stock No Mercy ROMs are. Nothing else
#  changed.
# Change only after a review.
REVIEWED = [
    dict(path='exe/WWFNoMercyRecomp/assets/mods/aki_quickmatch.nrm', **{'in': (SIX,)}, also=(NM_REV1, NM_V10, NMPLUS), max=1,
         why="one 32-byte window of the AkiMods' Quick Match MIPS code (a compiler's common instruction run; agent portable's "
             "audit)", status='allowed'),
    dict(path='exe/WWFNoMercyRecomp/src/main/six_blob.h', **{'in': (SIX,)}, also=(NM_REV1, NM_V10, NMPLUS), max=4000,
         why="the six-man / 32-slot engine's own MIPS code, built by six-engine/work/rw/build.py (SIX_SLOTS=32) from rw/r_*.c, "
             "stubs.S, six.h (rebuilt byte for byte with no ROM); its windows match only its own copy in the NoMercySix port ROM, "
             "plus one standard register-save prologue also in stock No Mercy (independent review 2026-11-18, "
             "REVIEW_ALLOWANCES.txt; allowed by the user's decision for this exact file only)", status='allowed',
         sha256='f734da7cfd784ae54ab9715547c82a806b765b1a42d2c6a05db119c94ff9f66f'),
    dict(path='mod/src/hands.c', **{'in': (BADDBLOOD,)}, also=(WM2000,), max=3,
         why="an 80-number table of hand poses -> hand meshes that equals 40 bytes of WrestleMania 2000's own table (its Badd "
             "Blood copy matched): game data in our C -- to make at build time from the ROM, or keep by the user's decision",
         status='flagged'),
    dict(path='mod/gen/arenas_wm.py', **{'in': (BADDBLOOD,)}, also=(WM2000,), max=22,
         why="lists of WrestleMania 2000 file numbers that equal the game's own arena file tables (2-byte numbers): references "
             "to files, not their data -- kept unless the user decides otherwise", status='flagged'),
]


# The same for a package (paths in the package).
PACKAGE_REVIEWED = [
    dict(REVIEWED[0], path='*mods/aki_quickmatch.nrm'),
    # (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur with
    #  your recommendations" -- narrow the exe's entry; independent review REVIEW_RELCHECK.txt R2) The exe compiles six_blob.h
    #  in (allowed above, for that exact file). arcade_audio_table.h's filter table copied from the ROM, which the old entry
    #  was flagged for, is gone (main 51b8392: read from the player's ROM), and so are the stage lists (stagelists). ALLOWED
    #  when every window lies inside the exe's copy of six_blob.h's words (rule 'six_blob_in_exe'); otherwise REFUSED.
    # (exename 2026-11-20, approved by the user on 2026-11-20: "Go with your recommendations") The second entry, rule
    #  'exe_blob_and_sdk' (FLAGGED: six_blob.h's windows plus at most 31 N64 SDK / C library constants found in every known
    #  ROM, pinned by content), is retired: relclean took those constants out of the exe (the VI mode read from the player's
    #  ROM, the doubles made at run time, RecompiledFuncs without SLP vectorizing), so the exe has no window outside the blob
    #  and the rule matched nothing. An exe with any window outside six_blob.h's copy is now refused, not flagged.
    dict(path='Ultimate No Mercy.exe', **{'in': (SIX,)}, also=(NM_REV1, NM_V10, NMPLUS), max=1800,
         why="the six-man engine's code (six_blob.h, allowed above for that exact file), compiled in as its words: every window "
             "lies inside that copy (REVIEW_ALLOWANCES.txt; narrowed REVIEW_RELCHECK.txt)", status='allowed',
         rule='six_blob_in_exe'),
    # (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur with your
    #  recommendations") No SHA-256 pin any more: the file must be the source build's image with plan.bin's holes zero, byte for
    #  byte, and every window must lie in the build's compiled code (rule 'holes') -- a new mod build needs no new pin, and
    #  nothing but our build can pass
    # (relprep 2026-11-24, approved by the user on 2026-11-23; independent review 2026-11-24, the release's holes_review
    #  folder: all 40 windows our own compiled code -- prologues, epilogues, stubs, *0x358 chains; none new since pkg8, the
    #  count reached 40 when lumberjack.c came in) max 39 -> 40; nothing else changed
    # (relprep2 2026-11-24, approved by the user on 2026-11-24: the fixed count, max 40, replaced by an automatic check)
    #  max None: no count -- instead rule 'holes_idioms' holds EVERY window to our own compiled code (no hole, no data, no
    #  rodata) and to one of the compiler's idioms (IDIOMS: prologue, epilogue, jr ra stubs, multiply chain, a function's
    #  end with the next one's prologue); anything else is refused. in / also unchanged.
    dict(path='firstrun/mod_binary.holes', **{'in': (SIX,)}, also=(NM_REV1, NM_V10, NMPLUS), max=None,
         why="the mod's image with every game-data table and patches.o zeroed (mkplan.py), rebuilt here from the source build "
             "(the same bytes); every window a compiler idiom in our own compiled C -- prologues / epilogues / jr ra stubs / "
             "multiply-by-constant chains / a function's end and the next one's prologue (rule holes_idioms; independent "
             "reviews 2026-11-18 REVIEW_ALLOWANCES.txt, REVIEW_RELCHECK.txt, 2026-11-24 holes_review)",
         status='allowed', rule='holes_idioms'),
]


def rom_label(name, data):
    """a ROM source's name: KNOWN_ROMS' for a known content, else its file's name with its SHA-1 (in no reviewed entry)"""
    sha = hashlib.sha1(data).hexdigest()
    return KNOWN_ROMS.get(sha) or 'ROM %s [sha1 %s]' % (name, sha[:12]), sha


def vcdiff_module():
    """the project's VCDIFF decoder (tools/handlers/handlers/aki/vcdiff.py), loaded from its file alone"""
    import importlib.util
    spec = importlib.util.spec_from_file_location('unm_vcdiff', os.path.join(REPO, 'tools', 'handlers', 'handlers', 'aki', 'vcdiff.py'))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m


def patches_in(paths, notes, skipped=None, unreadable=()):
    """[(where, bytes)]: every .xdelta among paths -- a file, or a member of a .zip (read in memory, never extracted). A patch
    larger than PATCH_MAX, or a file that can't be read, is not read and says so in notes (for the report); each such
    patch's name also goes into skipped, when given. unreadable: paths already found unreadable (said once, not again)"""
    out = []
    def skip(what):
        notes.append('(skipped, larger than PATCH_MAX, not read: %s)' % what)
        if skipped is not None: skipped.append(what)
    for r in paths:
        low = r.lower()
        if r in unreadable: continue
        try:
            if low.endswith('.xdelta'):
                if os.path.getsize(r) <= PATCH_MAX: out.append((os.path.basename(r), open(r, 'rb').read()))
                else: skip('%s, %d bytes' % (os.path.basename(r), os.path.getsize(r)))
            elif low.endswith('.zip'):
                with zipfile.ZipFile(r) as z:
                    for m in z.infolist():
                        if not m.filename.lower().endswith('.xdelta'): continue
                        if m.file_size <= PATCH_MAX: out.append((os.path.basename(r) + ':' + m.filename, z.read(m)))
                        else: skip('%s:%s, %d bytes' % (os.path.basename(r), m.filename, m.file_size))
        except (OSError, zipfile.BadZipFile) as x:
            notes.append('(could not read %s: %s)' % (os.path.basename(r), x))
            if skipped is not None: skipped.append('%s (could not be read)' % os.path.basename(r))
    return out


def patched_sources(paths, base_by_label, seen, need_base=False, unreadable=()):
    """(sources, problems, notes): each PATCHED_ROMS patch among paths applied IN MEMORY to its base (a ROM already loaded),
    used only when the result's SHA-1 is the patched ROM's; nothing is written. seen: SHA-1 -> where, the ROMs loaded so far.
    need_base (--package, relprep2 2026-11-24): a known patch whose base is not among the ROMs is a problem, not a note -- a
    release audit can't pass without the patched ROM its patch is there for; so is a patch not read (over PATCH_MAX, or
    unreadable), since what it is can't be known"""
    vc, out, problems, notes, skipped = None, [], [], [], []
    found = patches_in(paths, notes, skipped, unreadable)
    if need_base:
        problems.extend('a patch was not read: ' + s for s in skipped)
    for where, patch in found:
        psha = hashlib.sha1(patch).hexdigest(); e = PATCHED_ROMS.get(psha)
        if not e:
            notes.append('(an .xdelta no PATCHED_ROMS entry names, not applied: %s [sha1 %s])' % (where, psha[:12])); continue
        if e['sha1'] in seen:
            notes.append('(%s: loaded already, from %s; %s not applied again)' % (e['name'], seen[e['sha1']], where)); continue
        base = base_by_label.get(e['base'])
        if base is None:
            if need_base: problems.append('%s: %s is here, but its base, %s, is not among the ROMs' % (e['name'], where, e['base']))
            else: notes.append('(%s NOT built: its base, %s, is not among the ROMs)' % (e['name'], e['base']))
            continue
        vc = vc or vcdiff_module()
        try:
            rom = vc.apply(base, patch)
        except Exception as x:
            problems.append('%s: %s over %s could not be applied (%s: %s)' % (e['name'], where, e['base'], type(x).__name__, x))
            continue
        if hashlib.sha1(rom).hexdigest() != e['sha1']:
            problems.append("%s: %s over %s did not give the known ROM (its SHA-1)" % (e['name'], where, e['base']))
            continue
        seen[e['sha1']] = where
        out.append(CR.Source(e['name'], [(where + ' over ' + e['base'][4:], rom)], 'rom'))
        notes.append('(%s: %s applied to %s in memory, %d bytes, SHA-1 as known)' % (e['name'], where, e['base'], len(rom)))
    return out, problems, notes


def entry_problem(e, f, h, digest, rules):
    """None if the reviewed entry e covers file f's hits h (a cleanroom.scan_bytes result), else why not"""
    if not fnmatch.fnmatch(f, e['path']): return 'not this path'
    if e['max'] is not None and h['windows'] > e['max']: return '%d windows, the review allows %d' % (h['windows'], e['max'])
    if h['hits_cut'] or len(h['hits']) != h['windows']: return 'too many windows to check each one'
    anchor, ok = set(e['in']), set(e['in']) | set(e['also'])
    for off, lo, hi, names in h['hits']:
        found = set(names)
        if not found & anchor:
            return 'the window at 0x%X is in %s, none of the reviewed %s' % (off, ' + '.join(names), ' / '.join(e['in']))
        if not found <= ok:
            return 'the window at 0x%X is also in %s, which its review did not see' % (off, ' + '.join(sorted(found - ok)))
        # (relprep2 2026-11-24) No Mercy Plus V3 is in an 'also' only as stock No Mercy's patched copy: a window found in it
        #  must be in a stock No Mercy ROM too
        if NMPLUS in found and NMPLUS in e['also'] and NMPLUS not in anchor and not found & {NM_REV1, NM_V10}:
            return 'the window at 0x%X is in %s without a stock No Mercy ROM' % (off, NMPLUS)
    if e.get('sha256') and e['sha256'] != digest: return 'not the reviewed file (its SHA-256)'
    if e.get('rule'):
        why = rules[e['rule']](f, h)
        if why: return 'rule %s: %s' % (e['rule'], why)
    return None


def git_files():
    out = subprocess.run(['git', '-C', REPO, 'ls-files', '-co', '--exclude-standard', '-z'], capture_output=True)
    if out.returncode:
        raise SystemExit('release_audit: not a git repository? ' + out.stderr.decode(errors='replace'))
    return sorted(p for p in out.stdout.decode('utf-8').split('\0') if p)


def private_links():
    p = os.path.join(REPO, 'tools', 'layout', 'private_links.txt')
    if not os.path.exists(p): return []
    return [l.split('|')[1].strip() for l in open(p, encoding='utf-8') if l.strip() and not l.lstrip().startswith('#')]


def rule_problems(path, links):
    low = path.lower(); base = os.path.basename(low); parts = low.split('/')
    if low.endswith(CLOSED) or re.search(r'\.pak\.\d+$', low):
        return 'a ROM, disc image, game-made pack or closed archive'
    for l in links:
        if path == l or path.startswith(l.rstrip('/') + '/'):
            return 'a private linked path (tools/layout/private_links.txt): ' + l
    for i, part in enumerate(parts[:-1]):
        if part in ('wrestlers', 'arenas'):
            inner = parts[i + 1:]
            if inner and inner[0] == '_example':
                break
            if any(fnmatch.fnmatch(base, pat) for pat in OURS_IN_FOLDERS):
                break
            if 'model' in inner[:-1] or base == 'wrestler.json' or any(fnmatch.fnmatch(base, pat) for pat in EXTRACTED_IN_FOLDERS):
                return 'an EXTRACTED type in a %s folder (the importer makes it from the player\'s ROM)' % part
            break
    if low.endswith(BINARY_EXT):
        if not any(fnmatch.fnmatch(path, pat) for pat, _ in ALLOWED_BINARIES):
            return 'a binary data file not on the reviewed list (tools/release/release_audit.py ALLOWED_BINARIES)'
    return None


def package_rule_problems(path):
    low = path.lower()
    if low.startswith(PACKAGE_REFUSED_FOLDERS):
        return "a content pack (made from the player's ROMs, never shipped)"
    why = rule_problems(path, [])
    if why and why.startswith('a binary data file') and any(fnmatch.fnmatch(path, pat) for pat, _ in PACKAGE_ALLOWED):
        return None
    if why and why.startswith('a binary data file'):
        return 'a binary file not on the package list (tools/release/release_audit.py PACKAGE_ALLOWED)'
    return why


def rom_magic(data):
    return data[:4] in (b'\x80\x37\x12\x40', b'\x37\x80\x40\x12', b'\x40\x12\x37\x80')


NUM = re.compile(rb'(?<![\w.])(0[xX][0-9A-Fa-f]+|\d+)[uUlL]{0,3}(?![\w.])')
SEP = re.compile(rb'^[\s,{}\[\]()]*$')


def decoded_arrays(text):
    """the bytes the text's runs of numbers spell (8 or more numbers in a row, only commas, brackets and blanks between):
    each at its own width (hex: by its digits; decimal: the smallest of 1, 2, 4 bytes that holds it), big-endian; runs
    joined with 64 zero bytes (a window never spans two)"""
    out = bytearray(); run = []; last_end = None
    def flush():
        # (a run that is mostly N64 addresses -- 0x80000000-0x80FFFFFF: the functions and variables a comment, a list or a
        #  table of ours names -- is left out: a list of code addresses equals the game's own pointer tables word for word,
        #  and says nothing of its data. Newhome's first run flagged hands.c, arenas_wm.py and unreach_roots.json for that.)
        addr = sum(1 for w in run if len(w) == 4 and w[0] == 0x80)
        if len(run) >= 8 and addr * 2 < len(run):
            out.extend(b''.join(run)); out.extend(bytes(64))
    for m in NUM.finditer(text):
        if last_end is not None and not SEP.match(text[last_end:m.start()]):
            flush(); run = []
        tok = m.group(1)
        if tok[:2] in (b'0x', b'0X'):
            digits = len(tok) - 2; v = int(tok, 16)
            width = 1 if digits <= 2 else 2 if digits <= 4 else 4 if digits <= 8 else 8
        else:
            v = int(tok); width = 1 if v < 256 else 2 if v < 65536 else 4 if v < 2 ** 32 else 8
        if v >= 2 ** (8 * width): v &= 2 ** (8 * width) - 1
        run.append(v.to_bytes(width, 'big')); last_end = m.end()
    flush()
    return bytes(out)


# ---- RULES: checks a reviewed entry can require of its file (relcheck 2026-11-19, approved by the user on 2026-11-19:
#      "I looked at your list you needed from me and I concur with your recommendations"; independent review: notes
#      REVIEW_RELCHECK.txt)
_holes_cache = {}

def read_plan(path):
    """plan.bin (tools/firstrun/mkplan.py; exe arcade_firstrun.cpp read_plan): (image size, {sha name: digest}, [(offset,
    size, class)]) -- the class 0 base, 1 extras"""
    import struct
    b = open(path, 'rb').read()
    if b[:8] != b'UNMPLAN1': raise ValueError('not a plan')
    ver, image_len, rom_len = struct.unpack_from('<III', b, 8)
    at = 20
    sha = dict(zip(('rom', 'holed', 'base', 'full'), (b[at + 32 * i:at + 32 * (i + 1)] for i in range(4)))); at += 128
    (npk,) = struct.unpack_from('<I', b, at); at += 4 + npk * (32 + 32 + 8)
    (nh,) = struct.unpack_from('<I', b, at); at += 4
    holes = []
    for i in range(nh):
        o, n, x = struct.unpack_from('<IIB', b, at); at += 12 + 32
        holes.append((o, n, x))
    return image_len, sha, holes


def holes_check(root, build):
    """(None, the build's executable ranges) when the package's firstrun/mod_binary.holes is the source build's image with every
    hole of the package's plan.bin zero, byte for byte (as mkplan.py makes it); else (why not, None). Also: the plan's holes
    are the ones mkplan.find_holes finds in that build, the plan was made from that very image, and the package's other two
    members of the mod (mod.json, mod_syms.bin) are the build's."""
    if 'r' in _holes_cache: return _holes_cache['r']
    def done(r): _holes_cache['r'] = r; return r
    fr = os.path.join(root, 'firstrun')
    for n in ('mod_binary.holes', 'plan.bin', 'mod_syms.bin', 'mod.json'):
        if not os.path.isfile(os.path.join(fr, n)): return done(('the package has no firstrun/' + n, None))
    for n in ('mod_binary.bin', 'mod.elf', 'mod.map', 'mod_syms.bin', 'mod.json'):
        if not os.path.isfile(os.path.join(build, n)): return done(('no mod build to check it against (%s: no %s)' % (build, n), None))
    sys.path.insert(0, os.path.join(REPO, 'tools', 'firstrun'))
    try:
        import mkplan, modelf
        image, holes, relocs = mkplan.find_holes(build)       # (it checks mod_binary.bin is the build's mod.elf, section by section)
        elf, secs, syms = modelf.read_elf(os.path.join(build, 'mod.elf'))
        image_len, sha, plan_holes = read_plan(os.path.join(fr, 'plan.bin'))
    except Exception as x:
        return done(('the build or the plan could not be read (%s: %s)' % (type(x).__name__, x), None))
    if image_len != len(image) or sha['full'] != hashlib.sha256(image).digest():
        return done(("the plan was not made from this build's image (its size / SHA-256)", None))
    found = [(s, e - s, 0 if cls == 'base' else 1) for s, e, cls, label in holes]
    if plan_holes != found:
        return done(("the plan's holes (%d) are not the ones mkplan.find_holes finds in the build (%d)" % (len(plan_holes), len(found)), None))
    blank = bytearray(image)
    for o, n, x in plan_holes: blank[o:o + n] = bytes(n)
    shipped = open(os.path.join(fr, 'mod_binary.holes'), 'rb').read()
    if bytes(blank) != shipped:
        return done(("not the build's image with the plan's holes zero (%d bytes differ%s)" % (
            sum(1 for x, y in zip(blank, shipped) if x != y) + abs(len(blank) - len(shipped)),
            '' if len(blank) == len(shipped) else ', sizes %d / %d' % (len(blank), len(shipped))), None))
    if sha['holed'] != hashlib.sha256(shipped).digest():
        return done(("the plan's own holed SHA-256 is not this file's", None))
    for n in ('mod_syms.bin', 'mod.json'):
        if open(os.path.join(fr, n), 'rb').read() != open(os.path.join(build, n), 'rb').read():
            return done(('firstrun/%s is not the build\'s' % n, None))
    B = modelf.BASE
    code = sorted((x['addr'] - B, x['addr'] - B + x['size']) for x in secs if x['flags'] & 2 and x['flags'] & 4 and x['size'])
    # (each function is its own section: a window over one function's last instructions and the next one's first -- an
    #  epilogue and a prologue -- is still compiled code. Sections that touch, with no byte between, are joined; a gap is not)
    joined = []
    for f, t in code:
        if joined and f == joined[-1][1]: joined[-1] = (joined[-1][0], t)
        else: joined.append((f, t))
    # (relprep2 2026-11-24, rule 'holes_idioms') our own code only: the executable ranges above with every hole of the plan
    #  taken out (patches.o's patched copies are holes: game code, never ours), and where our functions start
    own = []
    cuts = sorted((o, o + n) for o, n, x in plan_holes)
    for f, t in joined:
        at = f
        for s, e in cuts:
            if e <= at or s >= t: continue
            if s > at: own.append((at, s))
            at = max(at, e)
        if at < t: own.append((at, t))
    starts = {y['addr'] - B for y in syms if y['type'] == 2 and y['size'] and y['alloc']}
    _holes_cache['own'] = dict(own=own, starts=starts, data=shipped)
    return done((None, joined))


def holes_rule(root, build, h):
    """rule 'holes': the file is the source build's image with the plan's holes zero (holes_check), and each window lies
    whole in the build's executable sections -- touching function sections joined, no gap (compiled code: a window in our DATA
    outside the holes is not covered)"""
    why, code = holes_check(root, build)
    if why: return why
    for off, lo, hi, names in h['hits']:
        if not any(f <= lo and hi <= t for f, t in code):
            return 'the window at 0x%X is not in compiled code (an executable section of the build\'s mod.elf)' % off
    return None


# ---- rule 'holes_idioms' (relprep2 2026-11-24; the user approved it on 2026-11-24, replacing the entry's fixed count, max 40):
#      every window firstrun/mod_binary.holes shares with a ROM must lie wholly in OUR executable code (an executable section
#      of the build's mod.elf, touching sections joined, with every hole of the plan taken out -- so never in patches.o's
#      patched copies, in data or in rodata), and its 32 bytes, read as big-endian MIPS words, must be one of the compiler's
#      own idioms below. Anything else is refused, however few windows there are. (The idioms are the ones the independent
#      review of 2026-11-24, release_export\holes_review, found the 40 windows to be.)
_SAVED = set(range(16, 24)) | {30, 31}          # (s0..s7, fp, ra: the registers a prologue saves and an epilogue restores)
IDIOMS = ('prologue', 'epilogue', 'stubs', 'multiply chain', 'function end + next prologue')


def mips_kind(w):
    """one word's part in the idioms: 'nop', 'jr_ra', 'sp_down' (addiu sp, sp, -N), 'sp_up' (addiu sp, sp, +N), 'save'
    (sw s0..s7 / fp / ra, N(sp)), 'restore' (lw the same), 'mul' (sll by 1..31 / addu / subu between registers), 'lui_addr'
    (lui of an N64 RAM address's upper half, 0x8000..0x80FF: the array a multiply chain indexes), or None (anything else)"""
    op, rs, rt, rd, sa, fn, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63, w & 0xFFFF
    if w == 0: return 'nop'
    if w == 0x03E00008: return 'jr_ra'
    if op == 9 and rs == 29 and rt == 29 and imm % 8 == 0:
        return 'sp_down' if imm & 0x8000 else 'sp_up' if imm else None
    if op in (0x2B, 0x23) and rs == 29 and rt in _SAVED and imm % 4 == 0 and not imm & 0x8000:
        return 'save' if op == 0x2B else 'restore'
    if op == 0:
        if fn == 0 and rs == 0 and sa and rd: return 'mul'
        if fn in (0x21, 0x23) and sa == 0 and rd: return 'mul'
    if op == 0x0F and rs == 0 and 0x8000 <= imm <= 0x80FF: return 'lui_addr'
    return None


def mul_chain(ops):
    """(relprep2, the review of e2dfe80) whether these sll / addu / subu words, in order, are ONE multiply-by-constant chain:
    every sll shifts the same source register x (not $zero); every addu / subu adds or subtracts values the chain has made
    (x itself, or an earlier sll's or addu's / subu's result in the window); and every value made is used by a later addu /
    subu, except the last instruction's (the product) -- the destinations feed forward"""
    sll = [w for w in ops if w & 63 == 0]
    if not sll: return False
    xs = {(w >> 16) & 31 for w in sll}
    if len(xs) != 1 or 0 in xs: return False
    x = xs.pop()
    made, unused = {x}, {}            # (registers holding a value of the chain; values not yet used: register -> index)
    for i, w in enumerate(ops):
        rs, rt, rd = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        if w & 63 == 0:                                     # sll rd, x, sa
            if rt not in made: return False
            unused.pop(rt, None) if rt != x else None
            if rd in unused: return False                   # (a value overwritten unused)
        else:                                               # addu / subu rd, rs, rt
            if rs not in made or rt not in made: return False
            unused.pop(rs, None); unused.pop(rt, None)
            if rd in unused: return False
        made.add(rd); unused[rd] = i
        if rd == x and w & 63 == 0 and i != len(ops) - 1 and any(o & 63 == 0 for o in ops[i + 1:]):
            return False                                    # (x shifted onto itself while later shifts still need it)
    return set(unused.values()) <= {len(ops) - 1}


def window_idiom(words, at, starts, prev=None):
    """the idiom the window's words are (IDIOMS), or None. words: its 8 instructions; at: its offset in the image; starts:
    the image offsets where our functions begin; prev: the word just before the window (a window may begin in the delay
    slot of a jr ra)"""
    k = [mips_kind(w) for w in words]
    if None in k: return None
    def prologue(i, at_start):
        # (words i.. are a prologue: addiu sp, sp, -N, then saves only, each inside the N bytes just made. at_start: the
        #  addiu is where one of our functions begins. A prologue alone need not be: the compiler often puts a function's
        #  early-out test before it -- shrink-wrapping -- so it starts after that branch's delay slot)
        if k[i] != 'sp_down' or (at_start and at + 4 * i not in starts): return False
        frame = 0x10000 - (words[i] & 0xFFFF)
        return all(x == 'save' and (words[j] & 0xFFFF) + 4 <= frame for j, x in enumerate(k[i + 1:], i + 1))
    def end_part(part):     # (a function's end: restores, jr ra and its delay slot, nops / jr ra stubs -- with a jr ra in it,
        #                     or the window begun in a jr ra's delay slot)
        if not all(x in ('restore', 'jr_ra', 'sp_up', 'nop') for x in part) or not ('jr_ra' in part or prev == 0x03E00008):
            return False
        # (the restores read inside the frame the addiu sp, sp, +N gives back, when it is in the window)
        up = [words[j] & 0xFFFF for j, x in enumerate(part) if x == 'sp_up']
        return not up or all((words[j] & 0xFFFF) + 4 <= up[0] for j, x in enumerate(part) if x == 'restore')
    # (a multiply by a constant: shifts and adds / subtracts between registers, a nop before, the array's lui at the end --
    #  and a real chain: mul_chain below)
    if set(k) <= {'mul', 'nop', 'lui_addr'} and k.count('mul') >= 5 and k.count('lui_addr') <= 1 and \
            ('lui_addr' not in k or k[-1] == 'lui_addr') and ('nop' not in k or k[0] == 'nop' and k.count('nop') == 1) and \
            mul_chain([w for w, x in zip(words, k) if x == 'mul']):
        return 'multiply chain'
    first = next((i for i, x in enumerate(k) if x != 'nop'), None)
    if first is not None and prologue(first, False): return 'prologue'
    if set(k) <= {'jr_ra', 'nop'} and k.count('jr_ra') >= 2: return 'stubs'
    if end_part(k): return 'epilogue'
    for i in range(1, 8):
        if end_part(k[:i]) and prologue(i, True): return 'function end + next prologue'
    return None


_idiom_counts = {}


def holes_idioms_rule(root, build, h):
    """rule 'holes_idioms' (above): holes_check, then every window wholly in our own code and one of IDIOMS"""
    why, code = holes_check(root, build)
    if why: return why
    own, starts, data = (_holes_cache['own'][x] for x in ('own', 'starts', 'data'))
    counts = dict.fromkeys(IDIOMS, 0)
    in_own = lambda s, e: any(f <= s and e <= t for f, t in own)
    for off, lo, hi, names in h['hits']:
        if not in_own(lo, hi):
            return ('the window at 0x%X is not wholly in our own compiled code (an executable section of the build\'s mod.elf, '
                    'outside every hole)' % off)
        # (the window is the file's 32 bytes at off; lo..hi is what it covers -- wider when it was found in a byte-swapped copy)
        if off % 4:
            return 'the window at 0x%X is not 8 whole instructions (not on a word)' % off
        words = [int.from_bytes(data[a:a + 4], 'big') for a in range(off, off + CR.W, 4)]
        # (relprep2, the review of e2dfe80) the word before counts (a window begun in a jr ra's delay slot) only when it is
        #  our own code too
        prev = int.from_bytes(data[off - 4:off], 'big') if off >= 4 and in_own(off - 4, off) else None
        idiom = window_idiom(words, off, starts, prev)
        if not idiom:
            return 'the window at 0x%X is not a recognised compiler idiom (%s)' % (off, ' '.join('%08X' % w for w in words))
        counts[idiom] += 1
    # (relprep2, the review of e2dfe80) a ceiling the count used to give: more than IDIOM_RUN_MAX windows back to back
    #  (each overlapping or touching the one before) is refused -- a long stretch of shared code is no single idiom; the
    #  build has runs of 2 at most
    run, longest, last = 0, 0, None
    for off in sorted(x[0] for x in h['hits']):
        run = run + 1 if last is not None and off <= last + CR.W else 1
        longest = max(longest, run); last = off
        if run > IDIOM_RUN_MAX:
            return '%d idiom windows back to back, up to the one at 0x%X (at most %d)' % (run, off, IDIOM_RUN_MAX)
    counts['longest run'] = longest
    _idiom_counts[id(h)] = counts
    return None


IDIOM_RUN_MAX = 3          # (relprep2: at most this many idiom windows back to back)
IDIOM_FLAG_TOTAL = 60      # (relprep2: more idiom windows than this in the file -- not refused, but FLAGGED in the report)


def six_blob_places(path):
    """(why not, None) or (None, [(start, end)]): where the exe holds six_blob.h's words (little-endian, as the compiler lays a
    uint32_t array out); six_blob.h must be the reviewed file (its SHA-256 in REVIEWED)"""
    hdr = os.path.join(REPO, 'exe', 'WWFNoMercyRecomp', 'src', 'main', 'six_blob.h')
    want = next(e['sha256'] for e in REVIEWED if e['path'].endswith('/six_blob.h'))
    if not os.path.isfile(hdr): return 'no six_blob.h in the repository', None
    t = open(hdr, 'rb').read()
    if hashlib.sha256(t).hexdigest() != want: return 'six_blob.h is not the reviewed file (its SHA-256)', None
    body = t[t.index(b'six_blob_words['):]
    words = [int(x, 16) for x in re.findall(rb'0x([0-9A-Fa-f]{8})', body[body.index(b'{'):body.index(b'}')])]
    import struct
    blob = struct.pack('<%dI' % len(words), *words)
    data = open(path, 'rb').read()
    at, places = data.find(blob), []
    while at >= 0:
        places.append((at, at + len(blob))); at = data.find(blob, at + 1)
    if not places: return "six_blob.h's words are not in it", None
    return None, places


def in_places(lo, hi, places):
    return any(f <= lo and hi <= t for f, t in places)


def six_blob_rule(path, h):
    """rule 'six_blob_in_exe': every window lies whole inside the exe's copy of six_blob.h's words"""
    why, places = six_blob_places(path)
    if why: return why
    for off, lo, hi, names in h['hits']:
        if not in_places(lo, hi, places):
            return 'the window at 0x%X is not in its copy of six_blob.h (0x%X..0x%X)' % (off, places[0][0], places[0][1])
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--rom', action='append', default=[])
    ap.add_argument('--forbid', action='append', default=[])
    ap.add_argument('--no-bytes', action='store_true', help='the rule checks only')
    ap.add_argument('--only', action='append', default=[], help='only these paths (a folder takes all under it)')
    ap.add_argument('--report')
    ap.add_argument('--package', help='audit this release package folder instead of the repository')
    ap.add_argument('--mod-build', default=os.path.join(REPO, 'mod', 'build', 'mod_six'),
                    help="(--package) the mod's build folder the package was made from: rule 'holes'")
    a = ap.parse_args()
    root = os.path.abspath(a.package) if a.package else REPO
    if a.package:
        if not os.path.isdir(root): raise SystemExit('release_audit: no such package folder: ' + a.package)
        files = sorted(rel for _, rel in CR.package_files(root))
    else:
        files = git_files()
    if a.only:
        files = [f for f in files if any(f == o or f.startswith(o.rstrip('/') + '/') for o in a.only)]
    links = private_links()
    lines, refused = [], []
    for f in files:
        why = package_rule_problems(f) if a.package else rule_problems(f, links)
        if why:
            refused.append(f); lines.append('REFUSED  %s: %s' % (f, why))
    print('rules: %d files, %d refused' % (len(files), len(refused)), flush=True)
    roms = list(a.rom)
    if not roms and not a.no_bytes:
        d = os.path.join(REPO, '..', 'private', 'roms')
        if not os.path.isdir(d):
            d = os.path.join(REPO, 'roms')   # (newuser 2026-11-17: the linked roms folder -- a worktree has no ../private beside it)
        if os.path.isdir(d):
            # (relprep2 2026-11-24) the whole folder, its subfolders too (as tools/romimport/roms.py looks), so a ROM or a
            #  patch in a subfolder is a source as well
            roms = sorted(os.path.join(dp, n) for dp, dn, fn in os.walk(d) for n in fn
                          if n.lower().endswith(('.z64', '.v64', '.n64', '.zip', '.xdelta')))
    # (relprep2 2026-11-24) problems: a source that should have been there and isn't -- the result is INCOMPLETE, not CLEAN.
    #  The source list and every note about a ROM or patch go into the report as well as on screen.
    problems, src_lines = [], []
    sources = []
    if a.no_bytes or not roms:
        src_lines.append('bytes: NOT scanned (%s)' % ('--no-bytes' if a.no_bytes else 'no ROMs given and no ../private/roms'))
        print(src_lines[-1])
    else:
        seen, base_by_label, unreadable = {}, {}, set()
        for r in roms:
            if r.lower().endswith('.xdelta'): continue          # (a patch: patched_sources below)
            try:
                for name, data in CR.load_roms(r):
                    # (relcheck 2026-11-19) known by its content; the same ROM twice (a .zip and its .z64) is one source
                    label, sha = rom_label(name, data)
                    if sha in seen:
                        src_lines.append('  (the same ROM as %s, once: %s)' % (seen[sha], name)); print(src_lines[-1]); continue
                    seen[sha] = name; base_by_label[label] = data
                    sources.append(CR.Source(label, [(name, data)], 'rom'))
            except SystemExit as e:
                src_lines.append('  (not a ROM, skipped: %s)' % os.path.basename(r)); print(src_lines[-1])
            except (OSError, zipfile.BadZipFile) as x:
                # (relprep2 2026-11-24) a file given as a ROM that can't be read (a corrupt .zip): said, and the audit is
                #  INCOMPLETE -- whatever it held was not checked against
                unreadable.add(r)
                src_lines.append('  (could not read %s: %s)' % (os.path.basename(r), x)); print(src_lines[-1])
                problems.append('%s could not be read (%s)' % (os.path.basename(r), type(x).__name__))
        # (relprep2 2026-11-24) No Mercy Plus V3 and Revenge Redux, from the player's patches over their bases, in memory
        more, more_problems, notes = patched_sources(roms, base_by_label, seen, need_base=bool(a.package), unreadable=unreadable)
        problems.extend(more_problems)
        sources.extend(more)
        for n in notes:
            src_lines.append('  ' + n); print(src_lines[-1])
        for fp in a.forbid:
            fp = fp.rstrip('/\\')
            # (only a forbidden folder's DATA files: the importers' JSON and READMEs in a wrestler folder are text our own
            #  handlers write -- found in the handlers' code they came from, which is what newhome's first run flagged)
            parts = ([(os.path.basename(fp), open(fp, 'rb').read())] if os.path.isfile(fp) else
                     [(rel, open(p, 'rb').read()) for p, rel in CR.package_files(fp)
                      if not rel.lower().endswith(('.json', '.txt', '.md', '.meta'))])
            sources.append(CR.Source('forbidden ' + os.path.basename(fp), parts, 'forbidden'))
        src_lines.append('bytes: %d sources (%s)' % (len(sources), ', '.join(s.name for s in sources)))
        print(src_lines[-1], flush=True)
        corpus = CR.Corpus(sources)
        rules = {'holes': (lambda f, h: holes_rule(root, a.mod_build, h)) if a.package else (lambda f, h: 'a package rule'),
                 'holes_idioms': (lambda f, h: holes_idioms_rule(root, a.mod_build, h)) if a.package else (lambda f, h: 'a package rule'),
                 'six_blob_in_exe': lambda f, h: six_blob_rule(os.path.join(root, f), h)}
        nbytes = 0
        for i, f in enumerate(files):
            p = os.path.join(root, f)
            if not os.path.isfile(p) or f in refused: continue
            for name, data in CR.members(p, f):
                if name == '!':
                    refused.append(f); lines.append('REFUSED  ' + data); continue
                nbytes += len(data)
                if rom_magic(data):
                    refused.append(f); lines.append('REFUSED  %s: an N64 ROM image' % name); continue
                text = b'\0' not in data[:8192]
                r = CR.scan_bytes(name, data, corpus, swaps=not text)
                hits = [r]
                if text:
                    dec = decoded_arrays(data)
                    if len(dec) >= CR.W:
                        hits.append(CR.scan_bytes(name + ' (its numbers as bytes)', dec, corpus, swaps=False))
                digest = hashlib.sha256(data).hexdigest()
                for h in hits:
                    if not h['windows']: continue
                    # (relcheck 2026-11-19) the first reviewed entry that covers every window's whole set of sources
                    _idiom_counts.clear()        # (relprep2: only this file's counts -- an id() can be reused)
                    tried = [(e, entry_problem(e, f, h, digest, rules))
                             for e in (PACKAGE_REVIEWED if a.package else REVIEWED) if fnmatch.fnmatch(f, e['path'])]
                    ex = next((e for e, why in tried if why is None), None)
                    sets = '; '.join('%s: %d' % kv for kv in sorted(h['sets'].items(), key=lambda kv: -kv[1]))
                    if ex:
                        lines.append('%s  %s: %d windows (%s) -- %s' % ('FLAGGED ' if ex['status'] == 'flagged' else 'allowed ',
                                     h['file'], h['windows'], sets, ex['why']))
                        ic = _idiom_counts.pop(id(h), None)
                        if ic:      # (rule holes_idioms: how many windows of each idiom)
                            lines.append('           idioms: ' + ', '.join('%s %d' % kv for kv in ic.items()))
                            if h['windows'] > IDIOM_FLAG_TOTAL:     # (relprep2: noticed, not refused)
                                lines.append('FLAGGED   %s: %d idiom windows, more than %d -- worth a look before it goes out' % (
                                    h['file'], h['windows'], IDIOM_FLAG_TOTAL))
                        continue
                    refused.append(f)
                    lines.append('REFUSED  %s: %d bytes shared with game data (%d windows; %s)' % (
                        h['file'], h['covered'], h['windows'], sets))
                    lines.extend('           reviewed entry does not cover it: ' + why for e, why in tried)
                    lines.extend('           ' + e for e in h['examples'][:3])
            if i % 2000 == 0:
                print('  %d / %d files, %.0f MB' % (i, len(files), nbytes / 1e6), flush=True)
        print('bytes: %d files, %.0f MB scanned' % (len(files), nbytes / 1e6))
    # (relcheck 2026-11-19, the review's R4) rule 'holes' runs on every package that has the first start's files, windows or
    #  not, with or without the byte scan: mod_binary.holes must be the source build's image with plan.bin's holes zero, and
    #  one of the two without the other is refused too (holes_check names what is missing)
    if a.package and any(os.path.exists(os.path.join(root, 'firstrun', n)) for n in ('mod_binary.holes', 'plan.bin')):
        why, _ = holes_check(root, a.mod_build)
        if why:
            refused.append('firstrun/mod_binary.holes')
            lines.append("REFUSED  firstrun/mod_binary.holes: rule holes: %s" % why)
        else:
            lines.append("rule holes: firstrun/mod_binary.holes is the source build's image with plan.bin's holes zero (%s)" % a.mod_build)
    refused = sorted(set(refused))
    # (relprep2 2026-11-24) a patched ROM that could not be built leaves its bytes unchecked: the audit is not complete
    lines.extend('INCOMPLETE  ' + p for p in problems)
    # (relprep2 2026-11-24) no ROM at all among the sources (--no-bytes, no ROM folder, none of the files a ROM): the bytes
    #  were checked against nothing, so the result can't be CLEAN
    if not any(s.kind == 'rom' for s in sources):
        problems.append('no ROM sources'); lines.append('INCOMPLETE  no ROM sources: the bytes were checked against no ROM')
    # (relprep2 2026-11-24) a package is checked against at least the ROMs its reviewed entries are about: NoMercySix and a
    #  stock No Mercy ROM (v1.0 or Rev 1)
    elif a.package:
        have = {s.name for s in sources}
        if SIX not in have or not have & {NM_REV1, NM_V10}:
            problems.append('the package needs NoMercySix and a stock No Mercy ROM among the sources')
            lines.append('INCOMPLETE  a package audit needs %s and %s or %s among the sources' % (SIX, NM_V10, NM_REV1))
    result = 'REFUSED' if refused else 'INCOMPLETE' if problems else 'CLEAN'
    # (relprep2 2026-11-24) the report begins with the sources and every note about a ROM or a patch (built, not built, not
    #  applied, skipped), as printed
    text = '\n'.join(src_lines + [''] + lines + ['', 'RESULT: %s (%s: %d files checked, %d refused)' % (result,
                      'package' if a.package else 'repository', len(files), len(refused))])
    print(text)
    if a.report:
        open(a.report, 'w', encoding='utf-8').write(text + '\n')
    sys.exit(1 if refused or problems else 0)


if __name__ == '__main__':
    main()
