# Puts this folder's tools first on PATH (dot-source it: . .\toolchain\env.ps1). Every build script does.
# Nothing here is installed on the PC: python, llvm-mingw, zig, cmake, ninja, ffmpeg and RecompModTool are all
# portable copies in this folder.
$tc = $PSScriptRoot
$env:PATH = (@("$tc\python", "$tc\python\Scripts", "$tc\llvm-mingw\bin", "$tc\zig", "$tc\cmake\bin", "$tc\ninja",
    "$tc\ffmpeg", "$tc\bin") -join ';') + ";$env:PATH"
$env:PYTHONNOUSERSITE = '1'   # only the packages in toolchain\python, never the user's own
