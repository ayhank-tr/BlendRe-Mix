@echo off
rem BGE-Remix P8.30 diagnostic RTX run on this PC (RTX graphics card, the real
rem RTX Remix runtime): Remix's pictures with one setting changed at a time.
rem
rem Like run_local.bat: it unpacks the newest blendremix-win64 download from
rem C:\BGE-Remix\transfer, runs, and packs the results as
rem C:\BGE-Remix\transfer\p0-results.zip. It takes about 25 minutes. Game windows
rem open and close by themselves: leave the PC alone until this window says it
rem has finished.
python "%~dp0..\run_local.py" p8diag
echo.
pause
