# Builds the game's own FFmpeg DLLs from source (third_party/ffmpeg-src -> third_party/ffmpeg). 2026-11-10.
#   powershell -ExecutionPolicy Bypass -File tools\ffmpeg_build\build_ffmpeg.ps1 [clean]
# This only lowers the priority (a long job may be using the PC; what it starts inherits it) and runs the real steps,
# tools/build_ffmpeg.sh, with the POSIX shell that comes with Git for Windows. Every option is in that file.
# ARCADE_FFMPEG_JOBS (4 unless set): how many compiles at once. ARCADE_LLVM_MINGW: another llvm-mingw.
$ErrorActionPreference = 'Stop'
(Get-Process -Id $PID).PriorityClass = 'BelowNormal'
$sh = @("$env:ProgramFiles\Git\bin\sh.exe", "${env:ProgramFiles(x86)}\Git\bin\sh.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sh) { throw "Git for Windows' sh.exe not found (FFmpeg's configure needs a POSIX shell)" }
& $sh (Join-Path $PSScriptRoot 'build_ffmpeg.sh') @args
exit $LASTEXITCODE
