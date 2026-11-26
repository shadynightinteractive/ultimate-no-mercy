# 0.58.0 (fast-forward for tests): Versus again at 6x (its limit raised to 1800 game s), then -- only if it passed --
# the start-up test at normal speed, which deploys. The other six passed at 6x on this build (2026-11-06 16:01-16:31).
Set-Location (Split-Path $PSScriptRoot -Parent)
$r = .\qa_set.ps1 -Tests versus_1p -Speed 6 2>&1 | Where-Object { $_ -notmatch '  ok$' }
$r
if ($r -match 'not passing') { 'Versus failed at 6x: not deploying'; exit 0 }
"== start-up at normal speed (deploys if it passes)"
.\qa_set.ps1 -Tests menu_from_attract -Deploy 2>&1 | Where-Object { $_ -notmatch '  ok$' }
