# Builds the test blob for the six-man engine, which the test rig (rig\harness\rig.py, RIG_SIX=1) puts in the
# emulated game at 0x80700000 -> mod\build\test6\arcade_test.bin.
#   .\build_test.ps1 [-Movex]
param([switch]$Movex)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\toolchain\env.ps1"

$flags = @('-DNM_SIX', '-Ibuild/patches', '--target=mips-unknown-none-elf', '-mcpu=mips2', '-mabi=32', '-O2', '-G0', '-mno-abicalls',
    '-fno-pic', '-mno-odd-spreg', '-mno-check-zero-division', '-fomit-frame-pointer', '-fno-builtin', '-fno-jump-tables',
    '-nostdinc', '-ffreestanding', '-ffunction-sections', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-Isrc', '-DARCADE_TEST=1')
if ($Movex) { $flags += '-DARCADE_MOVEX' }
$asFlags = @('--target=mips-unknown-none-elf', '-mcpu=mips2', '-mabi=32', '-mno-abicalls', '-fno-pic')

Push-Location "$PSScriptRoot\mod"
try {
    New-Item -ItemType Directory -Force build\test6 | Out-Null
    $env:ARCADE_ROM = "$PSScriptRoot\six-engine\NoMercySix\nomercy.z64"
    python gen/asmpatch.py | Out-Null
    if ($LASTEXITCODE) { throw "gen/asmpatch.py failed" }
    zig clang @asFlags -c build/patches/test.S -o build/test6/patches.o
    if ($LASTEXITCODE) { throw "assembling the patches failed" }
    $sources = 'src/draw.c', 'src/logic.c', 'src/gameplay.c', 'src/bigshow.c', 'src/media.c', 'src/commentary.c',
        'src/roster.c', 'src/variants.c', 'src/files.c', 'src/parts.c', 'src/skel.c', 'src/hands.c', 'src/movex.c', 'src/announce.c',
        'src/arcctl.c', 'src/story.c', 'src/save.c', 'src/util.c', 'src/tron.c', 'test/hooks_test.c'
    $objs = @(foreach ($f in $sources) {
        $o = "build/test6/$([IO.Path]::GetFileNameWithoutExtension($f)).o"
        zig clang @flags -c $f -o $o 2>&1 | Where-Object { "$_" -notmatch 'small-data accesses' } | Out-Host
        if ($LASTEXITCODE) { throw "compiling $f failed" }
        $o
    })
    zig clang @asFlags -c test/tramp.S -o build/test6/tramp.o
    if ($LASTEXITCODE) { throw "assembling tramp.S failed" }
    # tramp.o first (its _start at the blob's base), patches.o and the game's symbols last
    zig ld.lld -m elf32btsmip -T test/test6.ld build/test6/tramp.o @objs build/test6/patches.o build/patches/gamesyms.ld `
        -o build/test6/arcade_test.elf
    if ($LASTEXITCODE) { throw "link failed" }
    llvm-objcopy -O binary --only-section=.text --only-section=.rodata --only-section=.data --only-section=.ann `
        build/test6/arcade_test.elf build/test6/arcade_test.bin
    llvm-nm -n build/test6/arcade_test.elf | Select-String -Pattern ' (T|t) (_start|test_frame_hook|arcade_pad_hook|arcade_frame)$'
    llvm-size build/test6/arcade_test.elf
} finally { Pop-Location }
