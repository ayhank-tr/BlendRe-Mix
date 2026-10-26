@echo off
rem BGE-Remix phase 1 tests on this PC (RTX graphics card, real RTX Remix runtime).
rem Needs Python 3 (python.org, "Add python.exe to PATH").
rem Expects the CI build extracted to C:\BGE-Remix\builds\blendremix-win64.
rem Downloads the RTX Remix runtime (557 MB) the first time.
rem A window with a lit triangle appears for about 15 seconds: leave the PC alone meanwhile.

setlocal
set ROOT=C:\BGE-Remix
set OURS=%ROOT%\builds\blendremix-win64
set OUT=%ROOT%\results\p1

if not exist "%OURS%\blender.exe" (
  echo BGE-Remix build not found in %OURS%
  pause
  exit /b 1
)
python -m pip install --quiet pillow numpy
python "%~dp0run_p1.py" --ours "%OURS%" --out "%OUT%" --rtx
echo.
echo Results: %OUT%\summary_p1_rtx.txt
pause
