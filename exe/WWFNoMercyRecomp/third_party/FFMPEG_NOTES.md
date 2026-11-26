# Our own FFmpeg, built from source (2026-11-11)

This section is the second task (PROMPT_FFMPEG.md). Everything below it is the first task's notes, with the lines this
task made out of date corrected and marked.

## What changed

- **FFmpeg is now built here from source**, with the project's compiler, LGPL 2.1 or later, as five DLLs beside the exe.
  The downloaded BtbN build is gone: `third_party/ffmpeg-prebuilt` was deleted after the new build passed every test.
- **The DLLs are 12.7 MB in all, against 128 MB** for the prebuilt ones.
- **The game starts without the DLLs.** They are delay-loaded and looked for once, before the first call into them.
- **Software AV1 is in**: dav1d, also built from source, inside `avcodec-62.dll`.
- Two player fixes the new tests turned up (below): it moves to the CPU when the hardware decoder can't keep time, and
  catching up after falling behind no longer jumps back to a key frame it has already passed.
- Still true: nothing has run inside the game, and the mod has not been built for the N64.

## The source

| What | From | Checked |
|---|---|---|
| FFmpeg 8.1.3 | `https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz`, sha256 `7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3` | GPG signature `ffmpeg-8.1.3.tar.xz.asc`: good, made by the FFmpeg release signing key, fingerprint `FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8` (key from `https://ffmpeg.org/ffmpeg-devel.asc`) |
| dav1d 1.5.4 | `https://downloads.videolan.org/pub/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz` | sha256 `686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5`, equal to the `.sha256` file published beside it. Its `.asc` is kept but was not verified (I had no VideoLAN key to check it against) |

Unpacked in `third_party/ffmpeg-src/ffmpeg-8.1.3/` and `third_party/dav1d-src/dav1d-1.5.4/`, with the tarballs,
signatures and FFmpeg's key beside them. Nothing in either is run but its own configure / make / meson build.
8.1.3 is the plain release; the prebuilt one was 34 commits after it on the same branch.

## The build environment, and why

`powershell -ExecutionPolicy Bypass -File tools\build_ffmpeg.ps1 [clean]` rebuilds everything from the source. The
`.ps1` only lowers the priority and starts `tools/build_ffmpeg.sh`, which has every option written out. It builds dav1d
first (`tools/build_dav1d.sh`). From nothing it took roughly ten minutes here (not timed exactly) at below-normal priority, four compiles at a
time, with the pipeline running.

What it uses:

- **The compiler**: the project's llvm-mingw (clang 23.1.1), read-only. Its own `mingw32-make` (GNU Make 4.4.1).
- **The shell for FFmpeg's configure**: Git for Windows' `sh`, already on this PC. I chose it over MSYS2 or WSL because
  nothing has to be installed, and configure ran on it unchanged.
- **The shell for make's commands**: the busybox `sh` that llvm-mingw itself ships (`llvm-mingw/busybox/bin`). This was
  forced: with Git's `sh`, this make drops the `> file` of any command longer than about 8,000 characters, and the
  link of avcodec's DLL has one of 9,231. I reproduced it with a three-line makefile; with the busybox `sh` it works.
- **NASM 2.16.03** (`tools/nasm/nasm.exe`, BSD 2-clause, from `https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip`,
  sha256 `3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6`) for FFmpeg's and dav1d's hand-written x86
  code. This is a downloaded program, used only to build: nothing of it ends up in the DLLs. Without it the build works
  with `--disable-x86asm`, but CPU decoding is several times slower.
- **Meson 1.12.1** (`tools/meson`, Apache 2, sha256 `ab0a6ca09f8ef70c564c8241fb5a23957886a0b53fb58412b5e07eaf07dba743`
  of the release tarball), which dav1d needs. It is Python source, run from that folder with the Python already here;
  nothing was installed. The project's own `ninja.exe` does the building.
- **`tools/pkg-config-dav1d.sh`**: this PC has no pkg-config, and FFmpeg's configure asks it where dav1d is. Twenty
  lines of shell answer for that one library and say no to everything else.

Two things to know about the scripts:

- Paths to the compiler and ninja are written in (the project's `toolchain/...`, found from the script's own folder); the environment
  variables `ARCADE_LLVM_MINGW` and `ARCADE_NINJA_DIR` override them.
- configure writes the source folder the shell's way (`/d/...`), which this make can't open. The script rewrites
  it to `D:/...` in the two files make reads.

## The configuration

configure's own summary: `License: LGPL version 2.1 or later` (the script stops if it says anything else). No
`--enable-gpl`, no `--enable-nonfree`, no `--enable-version3`. No programs, no docs, no network code, no encoders, no
muxers, no filters, no devices. The line as it ran (also in `third_party/ffmpeg/BUILD.txt`, which goes beside the exe):

```
../../third_party/ffmpeg-src/ffmpeg-8.1.3/configure --prefix=../../third_party/ffmpeg --arch=x86_64
 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32- --cc=x86_64-w64-mingw32-clang --cxx=x86_64-w64-mingw32-clang++
 --x86asmexe=nasm --enable-shared --disable-static --disable-programs --disable-doc --disable-debug
 --disable-autodetect --disable-network --disable-avdevice --disable-avfilter --enable-w32threads --disable-pthreads
 --enable-d3d11va --enable-dxva2 --disable-everything --enable-protocol=file
 --enable-demuxer=mov,matroska,avi,asf,ogg,mp3,flac,wav,mpegps,mpegts,aac,ac3,mpegvideo,m4v,h264,hevc
 --enable-decoder=h264,hevc,vp8,vp9,av1,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,h263,wmv1,wmv2,wmv3,vc1,mpeg1video,
   mpeg2video,mjpeg,aac,aac_latm,mp3float,mp3,mp2float,opus,vorbis,flac,ac3,eac3,wmav1,wmav2,wmapro,wmalossless,
   pcm_s16le,pcm_s16be,pcm_u8,pcm_s8,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,
   pcm_mulaw,adpcm_ms,adpcm_ima_wav,adpcm_ima_qt
 --enable-parser=h264,hevc,vp8,vp9,av1,mpeg4video,h263,vc1,mpegvideo,mjpeg,aac,aac_latm,mpegaudio,opus,vorbis,flac,ac3
 --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,vp9_superframe_split,av1_frame_split,extract_extradata,aac_adtstoasc,
   mpeg4_unpack_bframes
 --enable-hwaccel=h264_d3d11va,h264_d3d11va2,h264_dxva2,hevc_d3d11va,hevc_d3d11va2,hevc_dxva2,vp9_d3d11va,
   vp9_d3d11va2,vp9_dxva2,av1_d3d11va,av1_d3d11va2,av1_dxva2
 --enable-libdav1d --enable-decoder=libdav1d --pkg-config=<this folder>/tools/pkg-config-dav1d.sh
```

Beyond the list you gave, I added, each for a reason:

- **MPEG program and transport streams** (`mpegps`, `mpegts`) and MP2 sound: that is where MPEG-1/2 video lives
  (`.mpg`, camera `.mts`).
- **The four raw-stream readers** (`mpegvideo`, `m4v`, `h264`, `hevc`): a `.mpg` file doesn't say what its video is, and
  FFmpeg finds out by trying these. The test found this: without them the `.mpg` sample opened with sound and no
  picture.
- **Motion JPEG** (older cameras' AVI files), **H.263** (MPEG-4 part 2 is built on it), **E-AC-3, WMA Pro / Lossless**,
  and a few PCM / ADPCM kinds that AVI, MOV and WAV carry.

dav1d is built static, release, library only, 8 and 10-bit, and linked into `avcodec-62.dll` (3.3 MB as a library; it
adds 2.0 MB to the DLL). `ARCADE_FFMPEG_AV1=0` builds without it; AV1 then plays only where the graphics chip does it.

## Sizes

| DLL | Ours | Prebuilt (BtbN 8.1.3) |
|---|---|---|
| avcodec-62.dll | 9,317,376 | 91,486,208 |
| avformat-62.dll | 656,896 | 19,846,144 |
| avutil-60.dll | 893,440 | 3,021,824 |
| swscale-9.dll | 1,688,064 | 12,972,544 |
| swresample-6.dll | 145,408 | 747,008 |
| **all five** | **12.7 MB** | **128.1 MB** |

They import only Windows' own DLLs (KERNEL32, ole32, the universal C runtime) and each other. No libc++, no
winpthread.

## Where the main project keeps it

- `third_party/ffmpeg-src/`, `third_party/dav1d-src/`: the sources (about 128 MB unpacked). In version control or not
  is your call; the tarballs and their checks are enough to recreate them.
- `tools/build_ffmpeg.ps1`, `build_ffmpeg.sh`, `build_dav1d.sh`, `pkg-config-dav1d.sh`, `tools/nasm/`, `tools/meson/`.
- `third_party/ffmpeg/`: **the built result** (`include/`, `lib/`, `bin/`, `LICENSE.txt`, `dav1d-LICENSE.txt`,
  `BUILD.txt`). `CMakeLists.txt` looks for it at `<exe source>/third_party/ffmpeg` (or `-DARCADE_FFMPEG_DIR=`), so
  either build it there or copy it there. In this folder everything sits at the top, beside `exe_src`.
- Nothing in the player's interface changed for the switch. The header and library names are the same.

## The game starts without the DLLs

How: `CMakeLists.txt` links each FFmpeg DLL delay-loaded (`-Wl,--delayload=<name>` with llvm-mingw, `/DELAYLOAD:` with
MSVC, plus the `delayimp` library), so Windows doesn't look for one until the first call into it. Before the first
call, `ffmpeg_ready()` in `arcade_media_decode.cpp` loads all five itself by the names the build's headers give
(`avutil-60.dll` ...). If one is missing or won't load, it logs one line, and from then on every video, a video's
sound, and any theme only FFmpeg reads are refused with a reason, the same way an undecodable file is. The start-up
scan therefore finds no videos, the import answers without the video bit, and entrances play the old way. Themes in
mp3, flac, wav and ogg don't use FFmpeg and still play natively.

Proof (`python media_groundwork/build_mediatest.py run no-ffmpeg`, output in `mediatest_noffmpeg_output.txt`):
`mediatest.exe` is linked the same way and run with the DLLs nowhere on the PATH. It starts; the four themes decode;
`open_video` and `open_video_sound` are refused with "can't be played: FFmpeg's DLLs aren't beside the game"; the
player given a theme and a video plays the theme and shows no picture; a video with sound and no theme plays nothing.
14 checks, all pass, no crash. The log line: `avutil-60.dll isn't beside the game or won't load (Windows error 126)`.

One caution the test itself showed: any call into FFmpeg that isn't behind `ffmpeg_ready()` ends the program when the
DLLs are missing (the test's own version banner did, until I moved it). In the game every FFmpeg call is in
`arcade_media_decode.cpp`, behind the three `open_*` functions, which all check first.

## mediatest with the new build

`python media_groundwork/build_mediatest.py run`: **86 checks, all pass** (`mediatest_output.txt`). The count is up
from 50 because of the new cases: nine more formats, three more theme formats, CPU-only fast-forward.

As the game decodes (to RGBA, at most 1024 wide), times real speed, last run:

| File | Hardware asked for | CPU, 4 threads | CPU, 1 thread |
|---|---|---|---|
| 720p30 H.264 + AAC | 14.1x (D3D11VA) | 31.7x | 18.6x |
| 1080p60 H.264 | 2.6x (D3D11VA) | 13.5x | 6.2x |
| 720p VP9 WebM | 12.7x (D3D11VA) | 35.7x | 11.0x |
| 640x480 WMV2 + WMA | 107x (CPU: no hardware decoder for WMV2) | 105x | 104x |

- CPU speeds are the same as the prebuilt build's or a little better, so the assembler code is in.
- The hardware path was again slower than the CPU on this PC, and swung between 0.6x and 3.5x for 1080p60 from run to
  run with the pipeline's load on the graphics card.
- Formats, three-second samples in `media_groundwork/samples_extra` (made with the project's own
  `toolchain/ffmpeg/ffmpeg.exe`): AV1 in MP4 and in Matroska with Opus, HEVC with AC-3, H.264 in MOV, VP8 with Vorbis,
  MPEG-4 with MP3 in AVI, MS-MPEG-4 v3 with WMA, MPEG-2 with MP2 in `.mpg`, Motion JPEG with PCM. All 90 pictures and
  the sound from each, on the CPU and with the hardware decoder asked for.
- **AV1**: the graphics chip decoded it here (`d3d11va`), and with hardware off dav1d did (`cpu, libdav1d`).
- Themes through FFmpeg: `.m4a`, `.opus`, `.wma` decode.

## Two player fixes found on the way

1. **The hardware decoder too slow.** In some runs 1080p60 through D3D11VA fell under real time and the player kept
   dropping pictures. Now, the second time it finds itself late while on the hardware decoder, it moves that video to
   the CPU for good, logged ("the hardware decoder is too slow here"). There is a hitch of a quarter to half a second
   at the move. New in the interface: `MediaVideo::use_cpu()`.
2. **Catching up never caught up on the CPU.** When more than a second behind, the decoder jumped to the key frame
   before the wanted time. In a file with a key frame every 8 seconds, in fast-forward, that key frame was behind
   where the decoder already was, so each jump went backwards. It now jumps only if the key frame is ahead. The new
   CPU-only fast-forward test went from 63 pictures decoded to about 1,800.

Also: FFmpeg's own messages now go to the game's log as `[media] FFmpeg: ...`, errors only, the first forty. A wrongly
identified file made one decoder print the same line hundreds of times.

## About the test's timing

The player's timing tests now run as a separate process started at normal priority. Started below normal and raised
afterwards, the same process decoded several times slower in the player tests on this PC, which made checks fail that
pass when run alone. I did not establish why; Windows 11 treating such a process as background work is my guess.
The 1080p60 check now runs 15 seconds and asks for 80% of pictures shown, so one move to the CPU doesn't fail it.

## Licences

- `third_party/ffmpeg/LICENSE.txt`: the LGPL 2.1 text, from the source's `COPYING.LGPLv2.1`. CMake copies it beside the
  exe as `FFmpeg-LICENSE.txt`.
- `third_party/ffmpeg/dav1d-LICENSE.txt`: dav1d's BSD 2-clause notice, which must go along with a binary that contains
  it. CMake copies it as `FFmpeg-dav1d-LICENSE.txt`.
- `third_party/ffmpeg/BUILD.txt`: version, source address, compiler, and the configure line. Copied as
  `FFmpeg-BUILD.txt`. Together with the source and `tools/build_ffmpeg.ps1` this is what lets a player rebuild or
  replace the DLLs, which is what the LGPL asks. The DLLs are separate files and never linked into the exe.
- Credits line for the game (wording only):

  > This game uses FFmpeg 8.1.3 (https://ffmpeg.org), licensed under the GNU Lesser General Public License, version
  > 2.1 or later, as separate DLLs built from unmodified source; and dav1d 1.5.4, © 2018-2025, VideoLAN and dav1d
  > authors, under the BSD 2-Clause licence. Their licences and build details are in the game's folder
  > (FFmpeg-LICENSE.txt, FFmpeg-dav1d-LICENSE.txt, FFmpeg-BUILD.txt). The source and build script are available from
  > [where the project publishes them].

  "Where the project publishes them" needs a real address: the LGPL wants the source for these DLLs offered with
  the game. (dav1d's copyright line is as its `COPYING` file has it.)

## Unsure, or not done

- Not run in the game; the mod not built. As before.
- The MSVC / clang-cl branch of the delay-load lines in `CMakeLists.txt` is written from knowledge, not tried. The
  llvm-mingw form is the one the test program links with.
- dav1d's signature was not verified, only its published checksum.
- `BUILD.txt` records the `--pkg-config=` path as it was on this PC (`<outside>...`).
- The HEVC, VP8, MPEG-4, MPEG-2 and Motion JPEG samples are small synthetic clips. A real phone or camera file of each
  would be a better test.
- No zlib in the build (`--disable-autodetect`): a MOV with a compressed header, or a Matroska file with compressed
  tracks, won't open. Both are rare. Say if you want it; it's one more small library to build from source.
- The player tests are sensitive to this PC's load. Over the evening the same build gave between 0 and 4 timing
  failures per run at below-normal priority, and none in the last three runs as arranged now.

---

# Entrance themes and Titantron videos straight from common files: what was built (2026-11-10)

## Where it stands

- The exe side is written and every changed or new exe file compiles with the real build's command (`compile_one.py`).
- The decoders and the player are proven without the game: `media_groundwork/mediatest.exe`, 50 checks, all pass
  (`media_groundwork/mediatest_output.txt`).
- **Nothing has run inside the game.** The game was not linked or started, and the mod was not built for the N64's CPU
  (its two changed files only pass a syntax check with a 32-bit x86 target). The renderer change and the silent song are
  untested: they are the first things for the in-game tests below.
- One line of glue is needed in a file that isn't in this folder (`mod/recomp/hooks_recomp.c`): see "The mod".

## The files

New:

| File | What it is |
|---|---|
| `exe_src/src/main/arcade_media.h` | The interface. `MediaVideo` / `MediaAudio` (the decoders, no decoder named), `Player`, and the four functions the game calls. |
| `exe_src/src/main/arcade_media_decode.cpp` | The decoders: FFmpeg for a video and its sound; dr_mp3 / dr_flac / dr_wav / stb_vorbis for theme files. The only file that names FFmpeg. No Windows calls. |
| `exe_src/src/main/arcade_media.cpp` | The player (one thread, the loop, pictures by the game's clock, the mixer) and the game's side: which folder has which file, and the import `recomp_arcade_entrance_media`. |
| `exe_src/src/main/third_party/dr_mp3.h`, `dr_flac.h`, `dr_wav.h`, `VENDORED.txt` | The single-file decoders, and where they came from. |
| `third_party/ffmpeg/` | FFmpeg: `include/`, `lib/`, `bin/` (5 DLLs), `LICENSE.txt`, `dav1d-LICENSE.txt`, `BUILD.txt`. (2026-11-11: now our own build; see the top section.) |
| `media_groundwork/mediatest.cpp`, `build_mediatest.py`, `stb_vorbis_impl.c`, `mediatest.exe`, `mediatest_output.txt` | The stand-alone test, how it's built, and its last run. |

Changed (small, each marked 2026-11-10 in the code):

| File | Change |
|---|---|
| `exe_src/src/main/main.cpp` | `queue_samples`: `arcade_media_mix` called after the commentary's mix, not in fast-forward. The exports registered after the commentary's. |
| `exe_src/src/main/arcade_view.cpp` | Texture id -4. The rectangle that draws the game's video frame (TRON_FRAME / TRON_FRAME_B) into the Titantron's picture draws the decoded picture while a native video plays; the picture is copied to a texture when a new one is due. |
| `exe_src/src/main/arcade_wrestlers.cpp` | Each wrestler remembers his folder; `arcade_wrestlers_folders()` hands out ID and folder. The header comment lists the new files. |
| `exe_src/CMakeLists.txt` | The two new sources; FFmpeg's include and library folders; the five libraries; its DLLs and licence copied beside the exe. |
| `mod/src/tron.c`, `media.c`, `arcade.h` | The signal, the silent song, the black placeholder frame. See "The mod". |

## Where FFmpeg and the decoders came from

(2026-11-11: the FFmpeg paragraph below describes the prebuilt build the first task used. It has been replaced by our own build and its files deleted: see the top section. The dr_libs and stb_vorbis paragraphs still hold.)

- **FFmpeg n8.1.3-34-ga35157abb9** (release branch 8.1: commit a35157abb9, 34 commits after the tag n8.1.3), win64, LGPL,
  shared. `avformat_license()` reports "LGPL version 3 or later". File
  `ffmpeg-n8.1.3-34-ga35157abb9-win64-lgpl-shared-8.1.zip` from
  `https://github.com/FFmpeg-Builds/FFmpeg-Builds/releases/tag/autobuild-2026-11-10-12-39` (the BtbN builds; GitHub now
  redirects `BtbN/FFmpeg-Builds` there). sha256 `3045c221…7c98da2c6`, checked against the release page's digest. Its
  `.dll.a` import libraries link with llvm-mingw as they are (the test program is linked that way). Kept: headers,
  import libraries, 5 DLLs, the licence. Removed: the three `.exe` files, docs, libavdevice, libavfilter. Nothing in the
  folder was run. Details: `third_party/ffmpeg/README.txt`.
  - I chose 8.1 over the newer 9.0 on offer: 9.0 is a new major version whose interface I could not check as well.
  - The LGPL's terms for the game: FFmpeg stays in DLLs the player can replace (never linked statically), its licence
    ships beside them (CMake copies it as `FFmpeg-LICENSE.txt`), and the game's credits should say which FFmpeg it is
    and where its source is. That last line is not written anywhere in the game yet.
- **dr_mp3 0.7.5, dr_flac 0.13.5, dr_wav 0.14.7**: `github.com/mackron/dr_libs` at commit `50c1320…`, unchanged. Public
  domain or MIT-0. These are the head of master, not a tagged release (the files say "UNRELEASED"). Hashes in
  `VENDORED.txt`.
- **stb_vorbis 1.22**: already in the game for the commentary (`commentary/stb_vorbis.c`). Not copied: the new file
  uses its declarations only, so the program has one copy of its code.
- No other library was added.

One addition beyond the brief, small: a theme file the four small decoders don't read (m4a, wma, opus, or an `.ogg`
that is really Opus) is handed to FFmpeg, since it is in the game anyway. mp3, flac, wav and ogg never touch FFmpeg.

## How it works

1. At start-up `arcade_media.cpp` takes the list of folder wrestlers and, on a thread of its own, looks in each folder
   for `theme.*` and `titantron.*` (else `video.*`), ignoring `.bin`, `.meta`, `.json`, `.txt`. Each file found is
   really opened and a little of it decoded. One log line per file: used, or why not.
2. When the entering wrestler's song starts, the mod calls `recomp_arcade_entrance_media(id, 0, 1)`. The exe answers
   with bits: 1 a native theme plays, 2 a native video, 4 the video's own sound. When the song stops or fades, the mod
   calls it with `on = 0` and the fade.
3. One thread decodes. It keeps half a second of sound and four pictures ahead, and goes back to the file's start at
   its end. A video's picture and sound are read through two separate file handles.
4. Sound: decoded to 16-bit stereo at the file's rate and resampled to the device's rate in `arcade_media_mix`
   (a 16-tap windowed sinc: the game's device is 32 kHz and themes are 44.1 or 48). Main Volume applies.
5. Pictures: each is due at its own timestamp, on the game's clock (`ultramodern::game_ticks_now`). The renderer asks
   for the picture due now every time it draws, so a 60-a-second file shows at 60 whatever the game's update rate.
6. Renderer: the mod still draws its video pass, with a black placeholder frame. The two rectangles of that pass are
   kept and given the decoded picture. The arena's truss, the panels and the 512 x 256 target are untouched.

Decisions I made that you may want to change:

- **Pictures wider than 1024 are made smaller as they are converted** (`WCW_MEDIA_MAXW`, 0 for never). The brief says
  "at full size"; the target is 512 x 256, and a 1920-wide texture drawn that small shimmers and costs more to copy.
  A 1280-wide file is brought down to 1024 too; anything 1024 wide or less is left as it is.
- **Crop**: the picture fills its part of the screen and is cropped evenly, never stretched, no bars. One function,
  `native_video_crop` in `arcade_view.cpp`. The part's shape comes from the rectangle the mod draws (about 2.17 : 1
  inside the truss, 2 : 1 on the close-up screen), so a 16:9 file loses about 18% of its height inside the truss and
  11% on the close-up.
- **Variants**: files are looked up in the wrestler's own folder by his ID. `files_of(id, variant_name)` takes a
  variant name it doesn't use yet, for the named variants planned. The import's `variant` argument is sent as 0.
- **Theme level**: the theme is mixed at full level times Main Volume (`WCW_MEDIA_VOLUME=0..200` for tests). It does
  not follow the game's music slider: I don't know where the game keeps it.
- **The winner's song after the bell** is not an entrance, so it stays the game's own.

## The user's three rules

1. **Sound.** `Player::start` decides: with a theme file the video's sound is never opened; with no theme and a video
   that has sound, the video's sound plays; with no theme and a silent video the answer is bit 2 only and the mod plays
   the game's song as before. Test: "theme + video with sound" returns bits 3 and the theme is heard; "video with
   sound, no theme" returns 6 and is heard; "silent video" returns 2 and adds no sound.
2. **Loop.** The thread rewinds at the end of the file. A video's own sound is cut or padded to the video's length each
   round so the two can't drift. Test: a 10 s video played for 25 s went round twice with no picture time going
   backwards.
3. **Old and stock unchanged.** Nothing native happens unless the entering wrestler's folder has a common-format file:
   the import returns 0 and the mod does what it did. `theme.bin` / `video.bin` are still read and still take their
   slots. A folder with both: the new file plays, logged ("theme.bin is there too: the new file is used"). A file that
   can't be decoded: logged, not used. `WCW_MEDIA=0`: the import returns 0 always.
   - One thing does change for everyone, in the mod: `tron_music(1)` is now called just before the song's sound starts
     instead of just after (`media_play_sound`), so the choice of song can depend on its answer. It sets the same
     state either way. Test 4 below checks stock entrances are the same.

## The mod (C for the N64: not compiled here)

`tron.c`
- `tron_music` now returns the bits. On `on` in an entrance it calls `plat_entrance_media(id, 0, 1)` with the
  entrant's ID (the same slot `arcade_entrant_video` reads). On off, and when `tron_mode` finds the game out of the
  entrances, `music_off()` calls `plat_entrance_media(fade, 0, 0)` if anything native was on.
- `tron_mode`: the entrance shows a video if he has a game video **or** a native one.
- `tron_main`: with a native video no game frames are read; a black frame is written to TRON_FRAME once.
- New: `tron_stop_fade`, `tron_native_cancel()`.

`media.c`
- **The silent song.** One more instrument and one more sound after the folder themes', set up in `install`: the
  added themes' own script, an instrument at level 0, and sample addresses in a range of its own (`SILENT_ROM`) for
  which `themes_audio_dma` fills the buffer with zeros. I took this over changing the bank's instrument levels, as
  advised: nothing other music uses is touched, and the game still starts, loops and stops a real song.
- `media_play_sound`: asks `tron_music(1)` first; if a native theme or a video's sound will play, the sound started is
  the silent one. If the silent song isn't available (the bank isn't set up, or its tables are full), the native part
  of that entrance is called off and it plays the old way.
- `dbg_silent_songs` counts them for QA.

`arcade.h`: the declarations and the three `NATIVE_*` bits.

**Needed in `mod/recomp/hooks_recomp.c`** (not in this folder, so not done):

```c
RECOMP_IMPORT("*", int recomp_arcade_entrance_media(u32 id, u32 variant, int on));
int plat_entrance_media(u32 id, u32 variant, int on) { return recomp_arcade_entrance_media(id, variant, on); }
```

and, so the fade's length reaches the exe, the existing hook changed from `{ tron_music(0); }` to:

```c
RECOMP_HOOK("func_800037BC") void tron_on_music_stop(s32 fade) { tron_stop_fade = fade; tron_music(0); }
```

Without the second change everything still works; the native theme just stops at once (30 ms) instead of fading. The
test rig's build (`ARCADE_TEST`) needs a `plat_entrance_media` that returns 0.

## What was measured

`mediatest.exe` on `media_groundwork/samples`, this PC, while the pipeline run was using it. Decode tests at
below-normal priority; the player's timing tests at normal priority (below normal, single frames were held up for over
a second by the other job, which says nothing about the player). The speeds moved a lot between runs with the other
job's load, so both runs are given where they differ.

Themes (30 s each, 44.1 kHz): all four decode and rewind. dr_mp3 144x to 1564x real time, dr_wav about 8000x,
stb_vorbis 124x to 1530x, dr_flac 94x to 1653x.

Videos, as the game decodes them (converted to RGBA, at most 1024 wide), times real speed:

| File | Hardware (D3D11VA, read back) | CPU, 4 threads | CPU, 1 thread |
|---|---|---|---|
| 720p30 H.264 + AAC, 20 s | 1.2x to 10.3x | 21x to 26x | 11.7x to 14.7x |
| 1080p60 H.264, 10 s | 1.2x to 2.9x | 8.1x to 12.3x | 3.8x to 5.0x |
| 720p VP9 WebM, 10 s | 9.3x to 15.9x | 28x to 30x | 9.9x to 10.5x |
| 640x480 WMV2 + WMA, 10 s | no hardware decoder for WMV2: CPU | 92x to 110x | 78x to 100x |

- **The hardware decoder was slower than the CPU on this PC in every run**, sometimes barely above real time. The cost
  is copying each picture back from the graphics card, and the card was busy with the pipeline's game. It still kept
  1080p60 playing in the player test (254 of 300 pictures shown in 5 s; the CPU path showed 225). Hardware is the
  default as you decided; `WCW_MEDIA_HW=0` turns it off. On a quiet PC it should be re-measured before deciding
  whether the default is right for Windows desktops.
- Media Foundation's 1000x problem is not there: one FFmpeg file handle reading picture and sound together took
  0.29 s against 0.26 s for the picture alone (720p file, the quieter run). The player uses two handles anyway.
- The WebM that reported 12 pictures a second to Media Foundation: FFmpeg states 24, and its timestamps give 24.001.
  The player uses the timestamps in any case.
- The hardware decoder made to fail at picture 40: the CPU carried on and the total was the same number of pictures,
  on all three hardware files.
- Player: no picture first seen more than 17 ms after it was due at normal speed; sound and picture of the WMV stayed
  together with no corrections over 12 s; at six times speed the 20 s video was in its third round at 60 s of game
  time; a stopped clock held the picture; a one-second fade reached silence; a sound file given as a video and a
  missing theme were logged and nothing played.

## Not done, or unsure

- **Not run in the game** (above). The renderer change in particular has only been compiled.
- **The fade's unit** is taken as 60 ticks a second (`SOUND_TICKS_PER_SECOND` in `arcade_media.cpp`), not measured.
  Each stop logs the ticks it was given and the seconds it took them for.
- **The silent song's level 0** rests on the bank's `CTL_LEVEL` byte being a volume. The zeroed samples make it silent
  whatever that byte means, so this is belt and braces, but neither was heard.
- **If a file passes the start-up check and then fails to open at the entrance**, the mod has already chosen the
  silent song and the black frame: that entrance is silent with a dark screen. Logged. I judged it too rare to add a
  second signal for.
- **The picture's sound runs later than the picture by the audio device's queue** (some tens of milliseconds). Not
  compensated.
- **Start delay**: the files are opened when the song starts, so sound and picture begin a few hundredths of a second
  later (the first hardware video of a session also makes the D3D11 device then). Opening when the entrant is chosen
  would hide it; there is no such signal yet.
- (2026-11-11: fixed, see "The game starts without the DLLs" at the top.) ~~A missing FFmpeg DLL stops the exe from starting.~~ They were ordinary imports. Delay-loading them would let the
  game run without video; not done.
- The wrestlers' folders are now read when the exports are registered (start-up) rather than at the mod's first
  request. The log lines come earlier; nothing else should differ.
- `compile_one.py` needs FFmpeg's headers on the include path. I ran it with
  `CPATH=<outside>` rather than change the script.
- `CMakeLists.txt` expects FFmpeg at `<exe source>/third_party/ffmpeg` (or `-DARCADE_FFMPEG_DIR=`). In this folder it
  sits at the top, beside `exe_src`: move it when merging.

## A copy-free path later

Today each picture goes: decoder (D3D11 texture, NV12) -> read back to memory -> converted to RGBA on the CPU ->
copied up to a D3D12 texture. To skip both copies on Windows: have FFmpeg decode with D3D12VA on the renderer's own
`ID3D12Device` (`AV_HWDEVICE_TYPE_D3D12VA`, the device handed over through `AVD3D12VADeviceContext`), keep the decoded
NV12 texture on the card, and sample its two planes in a small shader that does the YUV to RGB step. That needs the
renderer to share its device and command queue with the decoder, a fence between them, and a shader variant for
texture -4. The same shape on Linux is VAAPI to a DMA-BUF imported into Vulkan, and on a Mac VideoToolbox to a
Metal texture.

## Linux, Mac, Steam Deck

- `arcade_media.cpp` and `arcade_media_decode.cpp` have no Windows calls. The hardware decoder's type is one `#if`
  (`hardware_type()`): VAAPI on Linux and the Deck, VideoToolbox on a Mac. Untested there.
- FFmpeg: link the system's or a bundled LGPL build (`pkg-config libavformat libavcodec libswscale libswresample
  libavutil`); the CMake block has a comment where that goes. Check the build is LGPL (no `--enable-gpl`), and that it
  has the decoders wanted (some distributions leave H.264 or AAC out).
- The Deck's VAAPI decodes H.264, HEVC, VP9 and AV1; anything else falls to the CPU by the same code.
- The renderer side (texture -4 and its upload) is D3D12 like the rest of `arcade_view.cpp`, and ports with it.
- `mediatest.cpp` uses one Windows call, for process priority, behind `#ifdef _WIN32`.

## In-game tests (plan.md's table, updated)

Set up: a test folder wrestler with files from `media_groundwork/samples`, renamed. Log lines to look for start
`[media]`.

| # | Case (rule) | Scenario | Pass condition |
|---|---|---|---|
| 0 | It starts | the game with the new exe, DLLs beside it, no media files anywhere | starts; log: "0 folders have a theme or video in a common format"; nothing else changed |
| 1 | Theme + video (1, 2) | `theme.mp3` + `titantron.mp4` (with sound), entrance at normal speed | log: "native theme, native video (silent)"; the screen shows the video sharp, filling its part, looping if the entrance is longer; capture level > 0 during the entrance; the game's own song not heard; `dbg_silent_songs` went up |
| 2 | Video with sound (1) | `titantron.mp4` (sound), no theme | log: "the video's own sound plays"; capture level > 0; the game's song not heard |
| 3 | Silent video (1) | `tron_1080p60_silent.mp4` as `titantron.mp4`, no theme | video shown; his stock / base song plays as now; log line at the stop shows few pictures passed over |
| 4 | Stock unchanged (3) | `tron_entrances` + `hd_select` | pictures and entrance timing match the last run's (this also covers `tron_music(1)` moving before the sound's start) |
| 5 | Old files (3) | a folder with `theme.bin` / `theme.meta` + `video.bin` only | as today: no `[media] entrance` line, same pictures |
| 5b | Old and new together (3) | the same folder plus `theme.ogg` and `titantron.webm` | log: "theme.bin is there too: the new file is used"; the new ones play |
| 6 | Entrance length | cases 1-3 against the same wrestler with no files | entrance steps' frame counts equal (state.csv). **The silent song's first test** |
| 7 | Fast-forward | case 1 at x6 | no crash; the stop line shows video loops in step with the game's time; no theme in the capture (no device) |
| 8 | Many files | 20 folder wrestlers with theme + video, none with `.bin` files | all play; no cap message |
| 9 | The fade | case 1, let the entrance end normally | log: "the game's song stops over N ticks: taken as S s"; time the game's own fade for the same N to check 60 a second |
| 10 | Bad file | a text file renamed `theme.mp3`, a truncated `titantron.mp4` | each logged as not used at start-up; his entrance the old way |
| 11 | `WCW_MEDIA=0` | case 1 with it set | log says it's off; entrance exactly as without the files |
| 12 | `WCW_MEDIA_HW=0` | case 3 with it set | log: "decoded by cpu"; plays |
| 13 | Other formats | `theme.flac`, `theme.wav`, `tron.wmv`, `tron.webm` in turn | each plays |
| 14 | Two entrances in a row | both wrestlers with files, then one with and one without | the second's files replace the first's; the one without gets the game's song and video, nothing left over from the first |
| 15 | Other music | menus and a match after case 1 | menu and match music as before (the native path is only reached from the entrance hook) |
