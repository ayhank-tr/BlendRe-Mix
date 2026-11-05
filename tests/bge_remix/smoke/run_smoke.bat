@echo off
rem BGE-Remix crash check: our Blender in its window, with the real RTX Remix
rem runtime, doing by itself what a person does by hand (shading modes,
rem Rendered with Remix, edit mode, undo, playing, F12, the Remix View's
rem modes, saving). A few minutes; leave the PC alone until it ends.
rem   run_smoke.bat                     (our own test scene)
rem   run_smoke.bat "D:\path\file.blend" (the same steps on your own file)
set BGE_REMIX_SMOKE_FILE=%~1
python "%~dp0..\run_local.py" smoke
echo.
pause
