@echo off
rem BGE-Remix P7.0 study on this PC (RTX graphics card, real RTX Remix runtime).
rem Needs Python 3 (python.org, "Add python.exe to PATH") and the runtime
rem installed by the phase 1 tests (Preferences > System > RTX Remix).
rem Expects the CI build extracted to C:\BGE-Remix\builds\blendremix-win64.
rem
rem Takes about 10 minutes. Blender windows open and close by themselves.
rem Leave the PC alone meanwhile, and close any picture viewer showing the
rem results folder.

setlocal
set ROOT=C:\BGE-Remix
set OURS=%ROOT%\builds\blendremix-win64
set OUT=%ROOT%\results\p7_study

if not exist "%OURS%\blender.exe" (
  echo BGE-Remix build not found in %OURS%
  pause
  exit /b 1
)
python -m pip install --quiet pillow
python "%~dp0run_p7_study.py" --ours "%OURS%" --out "%OUT%" --rtx
echo.
echo Results: %OUT%\summary_p7_study_rtx.txt
pause
