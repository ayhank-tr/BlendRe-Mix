@echo off
rem BGE-Remix phase 0 tests on this PC (real GPU, no Mesa).
rem Needs Python 3 (python.org, "Add python.exe to PATH").
rem Expects the CI build extracted to C:\BGE-Remix\builds\blendremix-win64.
rem Downloads official Blender 2.79b once into C:\BGE-Remix\official.

setlocal
set ROOT=C:\BGE-Remix
set OURS=%ROOT%\builds\blendremix-win64
set OFFICIAL=%ROOT%\official\blender-2.79b-windows64
set OUT=%ROOT%\results\p0

if not exist "%OURS%\blender.exe" (
  echo BGE-Remix build not found in %OURS%
  pause
  exit /b 1
)
if not exist "%OFFICIAL%\blender.exe" (
  echo Downloading official Blender 2.79b...
  powershell -NoProfile -Command "New-Item -ItemType Directory -Force '%ROOT%\official' | Out-Null; Invoke-WebRequest 'https://download.blender.org/release/Blender2.79/blender-2.79b-windows64.zip' -OutFile '%ROOT%\official\official.zip'; Expand-Archive '%ROOT%\official\official.zip' -DestinationPath '%ROOT%\official' -Force"
)
python -m pip install --quiet pillow numpy
python "%~dp0run_p0.py" --ours "%OURS%" --official "%OFFICIAL%" --out "%OUT%"
echo.
echo Results: %OUT%\summary.txt
pause
