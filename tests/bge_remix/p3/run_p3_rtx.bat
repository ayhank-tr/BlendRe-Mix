@echo off
rem BGE-Remix phase 3 tests on this PC (RTX graphics card, real RTX Remix runtime).
rem Needs Python 3 (python.org, "Add python.exe to PATH") and the runtime
rem installed by the phase 1 tests (Preferences > System > RTX Remix).
rem Expects the CI build extracted to C:\BGE-Remix\builds\blendremix-win64.
rem
rem Takes about 15 minutes (the Cycles render of the grid is most of it).
rem Game windows open and close by themselves. Leave the PC alone meanwhile,
rem and close any picture viewer showing the results folder.

setlocal
set ROOT=C:\BGE-Remix
set OURS=%ROOT%\builds\blendremix-win64
set OUT=%ROOT%\results\p3

if not exist "%OURS%\blenderplayer.exe" (
  echo BGE-Remix build not found in %OURS%
  pause
  exit /b 1
)
python -m pip install --quiet pillow numpy
python "%~dp0run_p3.py" --ours "%OURS%" --out "%OUT%" --rtx
echo.
echo Results: %OUT%\summary_p3_rtx.txt
pause
