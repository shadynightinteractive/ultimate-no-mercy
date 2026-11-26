# Links the private folder's material into this source tree, where the scripts and tools expect it.
#
# The project's tools were written for one folder holding everything (the toolchain, the ROMs, the files extracted from
# them, build outputs, caches). The source tree (this repository) holds only what we made; everything else lives in the
# private folder beside it (..\private, or $env:UNM_PRIVATE) and is linked in here at the old paths, so every tool, build
# and cache key works unchanged:
#   - a folder: a directory junction (no administrator rights needed);
#   - a single file: a hard link (the same file under two names; a tool that rewrites it in place updates both).
# Every linked path is git-ignored (.gitignore, section "private material linked in"); tools\release\release_audit.py
# checks that none of it is ever tracked.
#
# The list is private_links.txt beside this script: <kind> <path here> <path in the private folder>, kind dir or file.
#   .\tools\layout\link_private.ps1            make the links that are missing (and the empty private folders they need)
#   .\tools\layout\link_private.ps1 -Check     only report: linked / missing / blocked by a real file or folder
#
# Share this tree only through git (git push, git archive): a zip of the folder itself would follow the links.
param([switch]$Check)
$ErrorActionPreference = 'Stop'
$src = (Resolve-Path "$PSScriptRoot\..\..").Path
$priv = if ($env:UNM_PRIVATE) { $env:UNM_PRIVATE } else { Join-Path (Split-Path $src -Parent) 'private' }
$list = Get-Content "$PSScriptRoot\private_links.txt" | Where-Object { $_ -match '\S' -and $_ -notmatch '^\s*#' }
$made = 0; $ok = 0; $bad = 0
foreach ($line in $list) {
    $kind, $here, $there = ($line.Trim() -split '\s*\|\s*')
    $here = Join-Path $src ($here -replace '/', '\'); $there = Join-Path $priv ($there -replace '/', '\')
    $item = Get-Item -LiteralPath $here -Force -ErrorAction SilentlyContinue
    if ($item) {
        $linked = if ($kind -eq 'dir') { $item.LinkType -eq 'Junction' -and ($item.Target -replace '\$', '') -eq ($there -replace '\$', '') }
                  else { $item.LinkType -eq 'HardLink' -or ((Test-Path -LiteralPath $there) -and (fsutil hardlink list $here 2>$null | Measure-Object).Count -gt 1) }
        if ($linked) { $ok++ } else { "BLOCKED (a real $kind is there, not a link): $here"; $bad++ }
        continue
    }
    if ($Check) { "missing: $here"; $bad++; continue }
    if ($kind -eq 'dir') {
        New-Item -ItemType Directory -Force $there | Out-Null
        New-Item -ItemType Directory -Force (Split-Path $here -Parent) | Out-Null
        New-Item -ItemType Junction -Path $here -Target $there | Out-Null
    } else {
        if (-not (Test-Path -LiteralPath $there)) { "not in the private folder (made by its generator first): $there"; $bad++; continue }
        New-Item -ItemType Directory -Force (Split-Path $here -Parent) | Out-Null
        New-Item -ItemType HardLink -Path $here -Target $there | Out-Null
    }
    $made++
}
"private folder: $priv -- $ok linked, $made made now, $bad missing or blocked"
if ($bad) { exit 1 }
