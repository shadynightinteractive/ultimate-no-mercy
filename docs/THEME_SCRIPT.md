# The added themes' sound script

Every entrance theme the arcade adds -- the built-in ports' themes (WCW/nWo Revenge's song 0x214, Revenge Redux's songs),
Jeff Jarrett's and The Blue Meanie's (WrestleMania 2000's samples), each wrestler folder's `theme.bin`, and the silent song
that stands in while the program plays a theme file itself -- is one long sample played by a short *sound script* of the
game's own format. Until 2026-11-19 that script was No Mercy Plus V3's (its song 23's, read from its ROM), so without No
Mercy Plus no added theme could play. Now it is our own, written from the game's script interpreter; it plays with any set
of ROMs.

Where it is: `mod/src/media.c` (`theme_script`: folder themes and the silent song), `exe/WWFNoMercyRecomp/src/romimport/
ri_content.cpp` (`THEME_SCRIPT`: the content index's rows for the added themes, made at first start) and
`mod/gen/mkcontent.py` (`tmpl`: the developer's twin). The three hold the same 17 bytes.

## The format (the parts we use)

The game starts a bank sound on a sound channel it has just reset (`reset_sound_channel`, 0x80029470, in the converted code
`exe/WWFNoMercyRecomp/src/game/lifted.cpp`): every field zero, then fixed velocity 0x7F, no level byte with each note
(`velocity_per_note` 0 -- only a song's channels set it), the default envelope (time scale 1, start level 0, 1 frame to 0x7F,
255 frames to 0x7F, released over 15 frames), transpose 0. Its interpreter (`run_channel_script`, 0x800282C0) reads the
script a byte at a time: a byte from 0x80 up is a command, run through the command table at 0x80052E74 (each handler returns
where the script carries on; 0 ends it); a byte below 0x80 is a note, and the script stops there until the note ends.

| bytes | what | handler |
|---|---|---|
| `84 t s a p d u r` | the envelope: time scale `t` (0 taken as 1), start level `s`, `a` frames to the peak `p`, `d` frames to the sustain level `u`, `r` frames to silence once released | `script_set_envelope` (0x80029D4C) |
| `81 i` | the instrument: `i` (one byte below 0x80; two with bit 7 set: 15 bits) through the bank's instrument map | the 0x81 handler |
| `A3 n b` | the transpose: a random number below `n`, plus `b` (a signed byte). It steps the sound player's random register (`*0x8008D2A4`) eight times whatever `n` is | `script_random_transpose` (0x8002A344) |
| `note` | a note below 0x60 (0x60 is a rest): its pitch is note + transpose + the instrument's tuning, in semitones | `run_channel_script` |
| `len` | the note's length in ticks: one byte below 0x80, or two (the first with bit 7: 15 bits). 0x7FFF holds the note for ever: its envelope is never released (`start_channel_envelope`, 0x80028AFC) | `run_channel_script` |
| `80` | the end: the channel lets its song, bank and wave go | the 0x80 handler |

The envelope runs in `channel_envelope_step` (0x80028B6C): stage 1 ramps from the start level to the peak over `a` frames,
stage 2 from the peak to the sustain level over `d` frames, stage 3 holds, stage 4 (released) falls to 0 over `r` frames.

## Ours, byte by byte

```
84 01 00 01 FF 01 FF 0F    envelope: time scale 1; from level 0, 1 frame to the peak 0xFF; 1 frame to 0xFF (held there);
                           released over 15 frames when the game stops the theme
81 ii                      the theme's instrument (the importer and media.c write the bank's number into byte 9)
A3 00 00                   transpose: a random number below 0 (none), plus 0
1F                         note 31: with the added themes' tuning the sample plays at the rate it was made for
FF FF                      length 0x7FFF: held until the game stops it
80                         end
```

Why these values:
- full level from the first frame (peak and sustain 0xFF) and a 15-frame release: the themes start at once at full level and
  fade briefly when the game stops them -- as they always have;
- pitch 31 plus the instrument's tuning: the pitch every added theme has played at (No Mercy Plus's script reached it as
  note 47 with a transpose of -16); for every tuning in use (-48.0 for our samples, -72.0 for WrestleMania 2000's two) the
  single-precision result is the same number;
- a random transpose of range 0, not a plain one: the game's random register for sound scripts is stepped by every theme
  start, as it was before, so every later random sound (a crowd, a hit) comes out exactly as it did;
- 17 bytes: the length every row of the content index has had. The first start fills the mod's tables from the index's text
  by `firstrun/hdrfill.bin`'s layout, which places each row's numbers by their count.

No Mercy Plus's own songs (its themes for the game's music slots, with No Mercy Plus present) keep the scripts its ROM gives
them; only the themes the arcade adds use this one.
