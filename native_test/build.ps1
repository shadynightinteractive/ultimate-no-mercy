# The native test program: its own folder, its own exe; nothing else in Development is written.
#   .\build.ps1            compile -> native_test\NoMercyNativeTest.exe
#   .\build.ps1 -Data      also redo data\game.pak (tools\export.py; build\arena.pkl must be there: tools\capture_arena.py)
param([switch]$Data)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\toolchain\env.ps1"
Set-Location $PSScriptRoot
if ($Data) { Push-Location tools; python export.py; if ($LASTEXITCODE) { throw 'export failed' }; Pop-Location }
$ErrorActionPreference = 'Continue'
x86_64-w64-mingw32-clang++ -std=c++17 -O2 -march=x86-64-v2 -static -static-libgcc -static-libstdc++ src\main.cpp -o NoMercyNativeTest.exe -ld3d11 -ldxgi -ld3dcompiler_47 -lxinput -luser32 -lgdi32 2>&1 | ForEach-Object { $_.ToString() } | Select-String -Pattern "error|warning: unused" | Select-Object -First 25
if ($LASTEXITCODE) { throw 'compile failed' }
Get-Item NoMercyNativeTest.exe | Select-Object Length, LastWriteTime | Format-List | Out-String
