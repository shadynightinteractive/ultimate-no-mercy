# Builds the Ultimate No Mercy mod (.nrm): zig clang (clang 18 with MIPS), zig's ld.lld and N64Recomp's
# RecompModTool, all in toolchain\.
#   .\build_mod.ps1              the six-man build (what ships) -> mod\build\mod_six\nomercy_arcade.nrm
#   .\build_mod.ps1 -NoSix       the vanilla-engine build       -> mod\build\mod\nomercy_arcade.nrm
#   -NoStory                     without the story runner (its four Story Mode patches; on in what ships). The
#                                stories built in: mod\stories\*.json (not test_*), by gen\storyc.py -> src\story_packs.h
#   -NoMovex                     without the made-over moves (movex.c)
# Then .\deploy.ps1 puts it in the game.
param([switch]$NoSix, [switch]$Story, [switch]$NoStory, [switch]$NoMovex, [string]$ModDir = '')   # (-ModDir: another copy of mod\, 2026-11-07)
# Native tools print warnings on stderr; failures are caught via $LASTEXITCODE below.
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\toolchain\env.ps1"

$mod = if ($ModDir) { $ModDir } else { "$PSScriptRoot\mod" }
# (the generators find the mod folder through nmpaths: NM_MOD_DIR points them at the copy too -- without it, -ModDir still
#  wrote the real mod\build\patches, 2026-11-08)
if ($ModDir) { $env:NM_MOD_DIR = $ModDir } else { Remove-Item Env:NM_MOD_DIR -ErrorAction SilentlyContinue }
$aki = "$PSScriptRoot\libs\AkiMods\common"
$syms = "$PSScriptRoot\exe\WWFNoMercyRecomp\syms\dump.toml"

# "zig clang" is clang itself, taking these flags as they are ("zig cc" would turn on -mabicalls / PIC code)
$flags = @('--target=mips-unknown-none-elf', '-mcpu=mips2', '-mabi=32', '-O2', '-G0', '-mno-abicalls', '-fno-pic',
    '-mno-odd-spreg', '-Xclang', '-target-feature', '-Xclang', '+nooddspreg', '-mno-check-zero-division',
    '-fomit-frame-pointer', '-fno-builtin', '-fno-jump-tables', '-nostdinc', '-ffreestanding', '-ffunction-sections',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-Werror=section', '-Isrc', '-I', "$aki\include")
# (2026-11-14, approved by the user; modnative6/7) The semantics the native mod's C++ has (src\game\mod_native.cpp, proved
#  against this build): -fwrapv, a signed int's overflow wraps, as the N64's addu does, instead of being undefined -- the
#  compiler had turned `--rr_walk_in <= 0` into `rr_walk_in <= 1` and `a - 23 >= 0` into `a >= 23`, true only without
#  overflow; -fno-strict-aliasing, a store through one type may change what a load through another reads. For every value the
#  game has, the same results: only overflows and type-punned pointers are now defined.
$flags += '-fwrapv', '-fno-strict-aliasing'
$asFlags = @('--target=mips-unknown-none-elf', '-mcpu=mips2', '-mabi=32', '-mno-abicalls', '-fno-pic')
$out = 'build\mod'
# the build for the six-man engine (NM_SIX: its moved arrays, src/six_addrs.h; patched copies from its ROM)
if (-not $NoSix) {
    $flags += '-DNM_SIX'; $out = 'build\mod_six'
    $env:ARCADE_ROM = "$PSScriptRoot\six-engine\NoMercySix\nomercy.z64"
} else { Remove-Item Env:ARCADE_ROM -ErrorAction SilentlyContinue }
if (-not $NoStory) { $flags += '-DARCADE_STORY' }   # (-Story: the old switch, on anyway now)
if (-not $NoMovex) { $flags += '-DARCADE_MOVEX' }

Push-Location $mod
try {
    New-Item -ItemType Directory -Force $out | Out-Null
    # the stories (mod\stories\*.json) -> src\story_packs.h, checked as they're made
    python gen/storyc.py --header stories src/story_packs.h
    if ($LASTEXITCODE) { throw "gen/storyc.py failed (a story in mod\stories doesn't compile)" }
    # the exe's own story compiler (arcade_stories.cpp) reads <program folder>\stories\*.json at run time, with the
    # roster from _roster.json there: both made here into build\stories (qa.ps1 / deploy.ps1 copy them to the game)
    New-Item -ItemType Directory -Force build\stories | Out-Null
    python gen/storyc.py --roster build/stories/_roster.json
    if ($LASTEXITCODE) { throw "gen/storyc.py --roster failed" }
    Get-ChildItem stories\*.json | Where-Object { $_.Name -notlike 'test_*' -and $_.Name -notlike '_*' } |
        Copy-Item -Destination build\stories\ -Force
    # (the Superstars' voices, for the voice tokens: the exe reads them beside _roster.json)
    Copy-Item stories\_voices.json build\stories\ -Force
    # both compilers (gen/storyc.py, the exe's arcade_stories.cpp built alone) on every story: the same bytes
    python gen/storycmp.py
    if ($LASTEXITCODE) { throw "gen/storycmp.py: the two story compilers made different packs" }
    $sources = 'src/draw.c', 'src/logic.c', 'src/gameplay.c', 'src/bigshow.c', 'src/media.c', 'src/commentary.c',
        'src/roster.c', 'src/variants.c', 'src/files.c', 'src/parts.c', 'src/skel.c', 'src/hd.c', 'src/hands.c', 'src/movex.c', 'src/announce.c',
        'src/arcctl.c', 'src/story.c', 'src/save.c', 'src/util.c', 'src/tron.c', 'src/arena.c', 'src/arena_ringstage.c', 'src/arena_entrance.c', 'src/arena_ring.c', 'recomp/hooks_recomp.c',
        'src/slots.c',   # (any32, 2026-11-18: last, so the existing code keeps its places)
        'src/lumberjack.c',   # (lumberjack, 2026-11-21: after slots.c, the same reason)
        'src/guestref.c',   # (guestref, 2026-11-22: after lumberjack.c, the same reason)
        'src/extras.c'   # (firstrun 2026-11-17: the built-in extras' flag, after them all: its word is a hole the first start fills)
    $objs = @(foreach ($f in $sources) {
        $o = "$out\$([IO.Path]::GetFileNameWithoutExtension($f)).o"
        # Out-Host: compiler output must not leak into $objs
        zig clang @flags -c $f -o $o 2>&1 | Where-Object { "$_" -notmatch 'small-data accesses' } | Out-Host
        if ($LASTEXITCODE) { throw "compiling $f failed" }
        $o
    })
    python gen/asmpatch.py | Out-Null
    if ($LASTEXITCODE) { throw "gen/asmpatch.py failed" }
    zig clang @asFlags -c build/patches/mod.S -o "$out\patches.o"
    if ($LASTEXITCODE) { throw "assembling the patches failed" }
    $objs += "$out\patches.o"     # (The content packs aren't built in any more: they're files, exe src\main\arcade_packs.cpp.)
    # (relprep2 2026-11-24) two words of our own right after the patched functions, which the release ships as zeros:
    #  src/layout_guard.S says why. Linked after patches.o, so they are the last of the .recomp_patch section.
    zig clang @asFlags -c src/layout_guard.S -o "$out\layout_guard.o"
    if ($LASTEXITCODE) { throw "assembling src/layout_guard.S failed" }
    $objs += "$out\layout_guard.o"
    zig ld.lld @objs -nostdlib -T "$aki\mod.ld" -Map "$out\mod.map" --unresolved-symbols=ignore-all --emit-relocs -e 0 `
        --no-nmagic -gc-sections -o "$out\mod.elf"
    if ($LASTEXITCODE) { throw "link failed" }
    # the manifest, with the game's symbol file where it is here (TOML: forward slashes; no BOM; LF, or the text
    # in it gets CRs)
    $toml = (Get-Content recomp\mod.toml -Raw).Replace("`r`n", "`n").Replace('/home/devbox/src/WWFNoMercyRecomp/syms/dump.toml', ($syms -replace '\\', '/'))
    [IO.File]::WriteAllText("$mod\$out\mod.toml", $toml)
    RecompModTool "$out\mod.toml" $out
    if ($LASTEXITCODE) { throw "RecompModTool failed" }
    # (2026-11-17, reproducible builds: the zip's entries without the build time -- the same mod, the same .nrm byte for byte)
    python gen/nrm_reproducible.py "$out\nomercy_arcade.nrm" | Out-Host
    if ($LASTEXITCODE) { throw "gen/nrm_reproducible.py failed" }
    # The game's converted copies of the mod's replacement functions (tools/lift/modrep.py, in lifted.cpp) are still this
    # build's copies? A stale one is not used -- the game's mod loader runs the mod's own copy instead -- until tools/lift
    # converts it again (night.ps1). Said here, so a change to a replaced function is known to cost its conversion.
    if (-not $NoSix) {
        $env:LIFT_MOD_BUILD = "$mod\$out"
        python "$PSScriptRoot\tools\lift\lift.py" check-modrep 2>&1 | Select-Object -Last 6 | Out-Host
        Remove-Item Env:LIFT_MOD_BUILD
        # And the mods' ahead-of-time translation in the exe's source (tools\aot): made from this build of the mod? If not,
        # the exe as built translates the mod as it loads it, as it always did, until build_exe.ps1 is run. (2026-11-11)
        if (-not $ModDir) { python "$PSScriptRoot\tools\aot\aot.py" --check 2>&1 | Select-Object -Last 3 | Out-Host }
    }
    Get-Item "$out\nomercy_arcade.nrm" | Select-Object Length, LastWriteTime, FullName
} finally { Pop-Location }
