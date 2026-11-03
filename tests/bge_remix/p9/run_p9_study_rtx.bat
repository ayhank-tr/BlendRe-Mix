@echo off
rem BGE-Remix P9.0: the RTX study on this PC (RTX graphics card, the real RTX
rem Remix runtime): six spheres in the OpenGL fallback against Remix, Remix's
rem debug views 1 to 1,000 one after another, Remix's own menu switched on.
rem About 15 minutes; game windows open and close by themselves: leave the PC
rem alone until this window says it has finished.
python "%~dp0..\run_local.py" p9studyrtx
echo.
pause
