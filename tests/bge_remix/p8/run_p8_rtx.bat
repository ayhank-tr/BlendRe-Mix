@echo off
rem BGE-Remix P8's RTX run on this PC (RTX graphics card, the real RTX Remix
rem runtime installed by the phase 1 tests: Preferences > System > RTX Remix).
rem
rem Like run_local.bat: it unpacks the newest blendremix-win64 download from
rem C:\BGE-Remix\transfer, runs, and packs the results as
rem C:\BGE-Remix\transfer\p0-results.zip. It takes about 40 minutes. Game windows
rem and Blender open and close by themselves the whole time: leave the PC
rem alone until this window says it has finished.
python "%~dp0..\run_local.py" p8rtx
echo.
pause
