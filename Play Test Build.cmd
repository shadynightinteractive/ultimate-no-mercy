@echo off
rem Play the newest test build (the exe and mod just built) in a copy of the game -- your own game is never touched --
rem with everything recorded: the game's state, a screenshot every 10 s, and a report the moment something breaks.
rem Play as long as you like, then close the game window; the result shows here.
title Ultimate No Mercy - test build (recorded)
cd /d "%~dp0"
echo.
echo  Ultimate No Mercy - TEST BUILD (recorded)
echo  ------------------------------------------
echo  The game opens in a moment. Play as normal, then close the game window.
echo  Your real game is not touched: this runs a copy (Development\qa\game).
echo.
where pwsh >nul 2>nul
if %errorlevel%==0 (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0qa.ps1" -Record
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0qa.ps1" -Record
)
echo.
echo  Done. The recording is in Development\qa\runs (the newest "__record" folder).
echo  Tell the assistant "I played the test build" and it will read it.
echo.
pause
