@echo off
rem BGE-Remix P7 tests on this PC: the Game View (RTX graphics card, real RTX
rem Remix runtime). Needs Python 3 (python.org, "Add python.exe to PATH") and
rem the runtime installed by the phase 1 tests (Preferences > System > RTX Remix).
rem Expects the CI build extracted to C:\BGE-Remix\builds\blendremix-win64.
rem
rem Takes about 15 minutes. Blender windows open and close by themselves and
rem one game window opens briefly. Leave the PC alone meanwhile.

setlocal
set ROOT=C:\BGE-Remix
set OURS=%ROOT%\builds\blendremix-win64
set OUT=%ROOT%\results\p7

if not exist "%OURS%\blender.exe" (
  echo BGE-Remix build not found in %OURS%
  pause
  exit /b 1
)
python -m pip install --quiet pillow
python "%~dp0run_p7.py" --ours "%OURS%" --out "%OUT%" --rtx
echo.
echo Results: %OUT%\summary_p7_rtx.txt
echo Picture sheet: %OUT%\sheet_p7.png
pause
