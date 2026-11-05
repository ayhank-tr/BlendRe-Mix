@echo off
rem BGE-Remix P9.1: the RTX run on this PC (RTX graphics card, the real RTX
rem Remix runtime): the six spheres and the room in the OpenGL fallback
rem against Remix, with ACES (Remix's tone mappers in turn) and with the
rem view transform. About 10 minutes; game windows open and close by
rem themselves: leave the PC alone until this window says it has finished.
python "%~dp0..\run_local.py" p9rtx
echo.
pause
