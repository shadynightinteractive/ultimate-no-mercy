# link_romonly.ps1 (romimport, 2026-11-17): points THIS worktree's private links at a ROM-only private folder, so every
# generator and importer run here reads only what the ROMs give (plus our own code and data) -- a capture it still needs
# is simply not there and the step fails, naming it.
#   .\tools\romimport\link_romonly.ps1 [-RomOnly <folder>]     (default: <private>\romonly)
# What changes (this worktree only; the shared private folder is never written):
#   data\                 -> <romonly>\data      (extract.py's ROM files, ROM-made images, our code copied from data\*.py)
#   mod\build             -> <romonly>\mod_build (the packs this tree makes)
#   tools\handlers\work   -> <romonly>\handlers_work
#   scratch               -> <romonly>\scratch   (the generators' /tmp/devbox-0 files: nmbank.pkl ...)
#   mod\src\<generated>.h  hard links replaced by plain copies (a generator here rewrites its own copy, never the shared one)
param([string]$RomOnly = '')
$ErrorActionPreference = 'Stop'
$src = (Resolve-Path "$PSScriptRoot\..\..").Path
$priv = if ($env:UNM_PRIVATE) { $env:UNM_PRIVATE } else { (Resolve-Path "$src\..\..").Path }   # (this worktree is <private>\wt\romimport)
if (-not $RomOnly) { $RomOnly = Join-Path $priv 'romonly' }
if ($src -notlike '*\wt\romimport' -and $src -notlike '*\wt\romrender' -and $src -notlike '*\wt\revmusic' -and $src -notlike '*\wt\reduxthemes') { throw "run this only in the romimport, romrender, revmusic or reduxthemes worktree (here: $src)" }
function Relink($here, $there) {
    $p = Join-Path $src $here
    $it = Get-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
    if ($it) {
        if ($it.LinkType -ne 'Junction') { throw "$p is not a junction: left alone" }
        cmd /c rmdir "$p" | Out-Null          # (the junction only, never its target)
    }
    New-Item -ItemType Directory -Force $there | Out-Null
    New-Item -ItemType Junction -Path $p -Target $there | Out-Null
    "{0,-22} -> {1}" -f $here, $there
}
Relink 'data' (Join-Path $RomOnly 'data')
Relink 'mod\build' (Join-Path $RomOnly 'mod_build')
Relink 'tools\handlers\work' (Join-Path $RomOnly 'handlers_work')
Relink 'scratch' (Join-Path $RomOnly 'scratch')
# (2026-11-18, reduxthemes) the music files: only the player's own songs (legacy_in.py puts them there) -- Revenge Redux's
# themes are built from the patch in roms\ (decision C), no longer from the MP3s once rendered from it
Relink 'mod\music' (Join-Path $RomOnly 'player_music')
# our code that lives in the private data folder (the research scripts the generators import): copied, never captures
$n = 0
Get-ChildItem (Join-Path $priv 'data') -Recurse -File -Include *.py | Where-Object { $_.FullName -notlike '*\handlers_rig\*' } | ForEach-Object {
    $rel = $_.FullName.Substring((Join-Path $priv 'data').Length + 1)
    $to = Join-Path (Join-Path $RomOnly 'data') $rel
    New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
    Copy-Item -LiteralPath $_.FullName $to -Force; $n++
}
"copied $n .py files from data\ (our code)"
# generated headers: plain copies here
$k = 0
Get-Content "$src\tools\layout\private_links.txt" | Where-Object { $_ -match '^\s*file\s*\|\s*mod/src/' } | ForEach-Object {
    $rel = ($_ -split '\s*\|\s*')[1].Trim() -replace '/', '\'
    $p = Join-Path $src $rel
    if (Test-Path -LiteralPath $p) {
        $b = [IO.File]::ReadAllBytes($p); Remove-Item -LiteralPath $p -Force; [IO.File]::WriteAllBytes($p, $b); $k++
    }
}
"mod\src: $k generated headers now plain copies"
