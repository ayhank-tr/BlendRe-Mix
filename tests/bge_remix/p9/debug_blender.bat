@echo off
rem BGE-Remix P9.0: Blender under Windows' console debugger, to catch a crash
rem you can make happen by hand (Rally Cross: clicking RTX Remix in Blender).
rem
rem It starts the build that run_local.bat unpacked (C:\BGE-Remix\tests-local\ours)
rem with the file you name, or Rally Cross. Do exactly what made it crash. When
rem Blender crashes, the debugger writes where, and this window closes; the
rem report is in C:\BGE-Remix\tests-local\results\debug_blender. If it does not
rem crash, close Blender as usual.
setlocal
set CDB=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe
set OURS=C:\BGE-Remix\tests-local\ours
set OUT=C:\BGE-Remix\tests-local\results\debug_blender
set FILE=%~1
if "%FILE%"=="" set FILE=C:\Users\ayhan\Downloads\Rally Cross BGE\RG_update_blend\RG_update.blend
if not exist "%CDB%" (
  echo The Windows debugger is not installed: %CDB%
  pause
  exit /b 1
)
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%\tmp"
rem Blender's and Remix's own logs and crash reports go into the results too
set TEMP=%OUT%\tmp
set TMP=%OUT%\tmp
set BGE_REMIX_TRACE=1
set BGE_REMIX_GL_DEBUG=1
echo Starting Blender under the debugger with:
echo   %FILE%
"%CDB%" -G -sins -logo "%OUT%\debugger.log" -cf "%~dp0debug_blender.cdb" "%OURS%\blender.exe" "%FILE%" > "%OUT%\console.log" 2>&1
echo.
echo Finished. The report is in %OUT%
echo Say "ready" to Claude.
pause
