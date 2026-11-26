# Commentary: the pack made from the player's own WWE '13 disc

Jim Ross and Jerry Lawler's lines come from WWE '13 (Wii). The audio never ships. What ships is the **recipe**
(`exe/WWFNoMercyRecomp/assets/commentary/wwe13/`: which line of the disc makes which clip, how it is trimmed and encoded,
and the director's index -- our ids and TAGS, not the transcripts: the user's decision, 2026-11-18). The game makes the pack itself, natively, the first time it finds the
player's WWE '13 image in `roms\`. Players need no Python and no other program.

## What the player does

Put the WWE '13 (Wii) disc image in the game's `roms\` folder, as **RVZ** (Dolphin: right-click the game, *Convert
File...*, RVZ) or WIA, or put a folder of the disc's files there (Dolphin: *Properties, Filesystem, Extract Entire
Disc*). A plain `.iso` / `.wbfs` is encrypted: it works too when the player puts **their own Wii key file** beside
it in `roms\` (the user's decision, 2026-11-18): BootMii's `keys.bin` (as Dolphin reads it: the common key at 0x114), or
`common-key.bin` / `common_key.bin` / `commonkey.bin` / `ckey.bin` (the 16 bytes), or the same names as `.txt` (32 hex
digits). Each candidate is only tried: the disc's partition must decrypt to a Wii boot header. **No key is shipped,
embedded, downloaded or logged.** Without a key file the game writes `commentary\wwe13_import_report.txt` (and the log, and
`content\firstrun.json`) saying to add the key file or convert the image to RVZ. Start the game. The pack is made in the
background, at low priority, in about a minute:

- the pack is written as `commentary\wwe13.pak`, and `commentary\wwe13_import_report.txt` says how every clip came out;
- the lines are on at once, with no restart.

It is made **once**. It is recorded in `content\firstrun.json` (`"commentary": {"wwe13": {"state": "done", ...}}`) and
never made again or written over. Nothing already in `commentary\` is written over: if `wwe13.pak` is taken, the pack
is written as `wwe13_built.pak` and the report says so.

## Which packs the game loads

Every pack in `commentary\` is loaded automatically, and so is `commentary.pak` beside the exe (its old place).
`src/main/commentary/arcade_commentary_packs.cpp` decides:

- one pack is loaded per **source**: the file name up to the first `.` or `_`, so `wwe13.pak` and `wwe13_built.pak`
  are both source `wwe13`;
- the player's own pack always beats the one the game built;
- packs of different sources are mixed;
- one pack alone is loaded exactly as it is.

## The chain, and the native code that repeats it

| step | developer's tools (this folder) | game (`exe/WWFNoMercyRecomp/src/main/commentary/packbuild/`) |
|---|---|---|
| read the disc | DolphinTool / an extracted folder | `wii_disc.cpp` (RVZ/WIA: the partition is stored decrypted, so no key is needed; zstd 1.5.6's decoder. A plain .iso/.wbfs: decrypted with the player's key file, `aes128.cpp`) |
| find the line | `akpk.py` (Wwise packages) | `comm_builder.cpp` |
| decode | vgmstream-cli (git f7f7641, libvorbis 1.3.7; float to s16 by truncation) | `wwise_decode.cpp` + vgmstream's own Wwise code (`packbuild/vgmstream`, ISC) |
| trim + encode | FFmpeg 6.1: `silenceremove` both ends, libvorbis q0, its Ogg muxer | `comm_encode.cpp` + libvorbis 1.3.7 / libogg 1.3.5 |
| header + serial | `oggpk.replace_comment` | `comm_encode.cpp` |
| cut pieces | `cutfit.apply_exact` (numpy) | `comm_builder.cpp` |
| pack | `compak_build.py` | `comm_pack.cpp` |

libvorbis runs under `third_party/vorbis_compat` so that it behaves as it does on Linux, where the developer's pack was
made. Three things change:

- libvorbis's own Windows `rint` replacement is switched off;
- `qsort` becomes a stable sort, as glibc's is;
- the math functions are CORE-MATH's correctly rounded ones.

`c2_vorbis_compat.h` says why.

## Proofs (2026-11-18, without the disc)

| test | result |
|---|---|
| `check_target.py` / `comm_pack_test` (native): the recipe + the installed AI-free pack's clips (by SHA-1) + the moves table made from the ROM | the old target **22812f22...** byte for byte (now `old_target_sha1`, reference only: see below; 4,149 clips, 5,160 lines; with the old text index instead of the tags: 4d8aeb43...) |
| `comm_moves_test` (native) / `rebuild.recipe_moves` (Python): the moves table made from the player's ROM by `moves_refs.tsv` | the old moves.tsv byte for byte (1,454 rows) |
| `director_same_test` (native): the director on the pack with the text (4d8aeb43) and on the pack with the tags (22812f22), the same 300,000 scripted events, clock and seed | the same line, chained name and gain at every step (135,221 lines said, 4,361 with a name, 4,632 counts) |
| `diff_aifree.py`: the text target (4d8aeb43) vs the installed AI-free pack (8fedf2c2...) | the same moves table and the same 4,149 clips. It leaves out 1,249 clips, all from Legends of WrestleMania: 1,411 Legends rows, plus Hogan's 12 rows, which are Legends audio. That is the user's decision, WWE '13 only. |
| `test_paging.py`: FFmpeg's Ogg paging model + `replace_comment` headers on every pack clip | 3,999 / 3,999 byte for byte (initial padding 512) |
| `test_encode.py`: native encoder vs FFmpeg + `replace_comment`, 300 inputs (trim and no trim, 20 and 22.05 kHz) | 300 / 300 (run with the Windows rint, as Windows FFmpeg has it) |
| `test_platform.py` + `test_platform_linux.sh`: native Windows build vs the same code built on Linux (gcc, glibc), 150 inputs | 150 / 150 with vorbis_compat (124 / 150 without: libvorbis's Windows rint) |
| `test_wem.py` (`ogg2wem.py`: Ogg Vorbis to Wwise .wem): the builder's decode vs libvorbis on the same packets | 240 / 240. Covers big- and little-endian files, both codebook lists (v62 aoTuV, v56 standard), and standard and modified packets (44.1 kHz). |
| `test_vgmstream_elf.py`: the builder's decode vs **the very vgmstream-cli build that made the pack** (run in WSL) | 60 / 60 the same samples |
| `mk_fake_disc.py`: a made-up disc folder of the pack's own clips as .wem: the whole builder (CLI and in the game) | 4,012 clips "equivalent" (a second encode), 1 wrong, 136 missing (not placed); the same pack SHA-1 (cc046fea...) from the CLI and the game, every run |
| `test_wii_crypt.py`: made-up **encrypted** Wii images (.iso and .wbfs) encrypted with a made-up test key (Python's `cryptography` AES, not ours) | 46 / 46: every file read back byte for byte with the key as `common-key.bin`, `common_key.txt` (hex) and BootMii `keys.bin`; refused with "add the key file / convert to RVZ" without a key and "does not open this disc" with a wrong one; the whole builder on the encrypted .iso gives the same pack (cc046fea...) as from the folder |

## With the real disc (2026-11-18): the canonical pack

The user's WWE '13 (USA) disc image (Wii, game id S3XE78, RVZ, 4.06 GB) was the final test. The native reader reads it in
place (461 files listed in 0.3 s; the recipe's 4,071 lines all found, all in `sound/Comm.pck`, english(us)).

**The user's decision (2026-11-18): the pack this builder makes from the disc is the canonical pack.** Its SHA-1,
**97cc39d1...** (recipe.json `target_sha1`), is the target. Each clip's SHA-1 is `clips.tsv`'s `disc_sha1`, and
"identical" in the game's report means those bytes. The developer's old pack (22812f22..., `old_target_sha1`; its
clips' SHA-1 are in `sha1`) is kept as reference only: it is no longer the target.

What the disc showed:

- **Decode: exact.** Our decode equals the very vgmstream-cli build that made the old pack, on real disc lines
  (5 of 5 the same samples).
- **Trim: exact.** Every line's trimmed length equals the old clip's (3,998 of 3,998).
- **Encode: not the same build.** Our Windows build and our Linux build (gcc, glibc, with and without FMA in libm)
  agree with each other but not with the old pack in 75% of the lines. In those lines only a few audio packets differ
  (e.g. 6 of 127). The developer's libvorbis was built differently, most likely with `-ffast-math`, which libvorbis's
  own configure adds for gcc on x86 Linux. It is not reproduced here, by decision.
- **Judge: fixed.** The builder used to compare the old clip's loudness, measured after decoding, with the loudness
  of the samples before encoding. Vorbis q0 adds up to ~1.5 dB, so 405 good lines were called "wrong". The builder now
  decodes the clip it made (`comm_encode.cpp` `decode_clip`) and judges that, as `rebuild.py` does.
- **The 151 cut pieces (names, lead-ins): all placed, and exact.** Every piece is in `Comm.pck`. A search at 2 kHz,
  resampled with an anti-aliasing filter, finds every piece at a correlation of 0.996 or more. (`trace_disc.py`'s plain
  `x[::5]` takes one sample in five without a filter, and finds none: a one-sample shift scrambles the aliased speech.)
  The fine pass on the whole line, before the trim, gives 0.995 or more. The pieces were cut exactly like this:
  `trunc(x / 32768 * w * 32767)` of the **untrimmed** line, so 61 of them start before the trim, down to -1,337
  samples. `w` is `numpy.linspace(0, 1, F)` at each end (mostly F = 160 or 200) and the gain is 1. With that model, all
  151 come out the very samples of the developer's pieces (`cutfit.apply_exact`, `comm_builder.cpp`). The `start`,
  `length`, `fade_in` and `fade_out` columns hold these values.

| test (the real disc) | result |
|---|---|
| `comm_build_test` on the RVZ, Windows, 6 threads (about 18 s) and again with 3 threads; the same built on Linux (WSL, gcc + glibc, our vendored code); from the extracted disc folder | **97cc39d1...** every time: 4,149 clips, 5,160 lines, 58,761,059 bytes, identical 4,149 |
| the new pack vs the old one, every clip decoded by FFmpeg (an independent decoder) | all 4,149 there, every duration (granule) exact. Byte-identical to the old clip: 820 of 3,998 lines and 60 of 151 pieces. The rest: loudness within **0.071 dB** (99% within 0.016), waveform correlation **>= 0.990** (median 1.0000), long-term spectrum within 1.44 dB RMS over 32 bands (99% within 0.14 dB) |
| `comm_pack_test`: the recipe + a pack's clips by SHA-1 | the new pack's clips give 97cc39d1 (`disc_sha1`); the old pack's give 22812f22 (`sha1`, `old_target_sha1`) |
| the game as a player gets it: a fresh install from the release package (`make_package.ps1`, audit CLEAN), the No Mercy ROM and the RVZ in `roms\`, first start (hidden) | firstrun made the mod, then the pack was built in the background in **26 s** and loaded at once (5,160 lines): `commentary\wwe13.pak` **97cc39d1...**, report "4149 identical", `contentirstrun.json` `commentary.wwe13.state` done. Second start: nothing built, the pack and its report untouched, loaded at once |

## Tags, not text; the moves table from the ROM (2026-11-18)

The user's decision: no transcripts on GitHub. The standing rule: no extracted game material. So:

- **`lines.tsv`'s last column holds a tag, not the text** (`make_tags.py`, run once by the developer). The game reads the
  text for two things only (`director.cpp`):
  - `way:N` on the "name" rows: takes with the same text are one way of saying the name, so equal texts get equal tags;
  - `count:N` on the "count" rows: the number the referee's count line says;
  - `-` everywhere else.
  `director.cpp` reads the tags, and the text in packs that still have it (players' own packs keep working).
- **The transcripts are the developer's own**, in the private folder: `data\commentary_text\wwe13_text.tsv` (clip, its
  SHA-1, event, text), with the spreadsheet (`commentary.csv`, `_export_lines.tsv`, `moves.txt`). `mod\gen\comcsv.py
  export` works there, and puts the text back in place of the tags when that file is there (found by each clip's SHA-1).
- **The moves table** (move number -> the game's move name, which the director matches the lines' move words against) is
  No Mercy's own text. The recipe ships only references, `moves_refs.tsv` (move number, AKI file, offset of the name:
  romimport's `names_index.json`). The game makes the table from the player's No Mercy (USA) (Rev 1) ROM when it builds
  the pack (`packbuild\comm_moves.cpp`; the same build-once and never-overwrite rules: it is part of the pack).
