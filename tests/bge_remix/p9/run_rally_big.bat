@echo off
rem BGE-Remix P9.0: Rally Cross with RTX Remix at 2560 x 1440 and for a minute.
rem
rem The player with Remix at 2560 x 1440 for 60 seconds, the same at 960 x 540,
rem and Blender with a large window: the game in OpenGL for 15 seconds, then in
rem Remix for 60 seconds (as when Remix crashed inside Blender). Each under the
rem debugger. About 10 minutes; leave the PC alone until this window says it
rem has finished.
python "%~dp0..\run_local.py" rallybig
echo.
pause
