@echo off
rem BGE-Remix P10: the RTX run on this PC (RTX graphics card, the real RTX
rem Remix runtime): the texture remaster's pictures, the Remix View's Run
rem mode and the logic bricks against Remix. Game windows open and close by
rem themselves: leave the PC alone until this window says it has finished.
python "%~dp0..\run_local.py" p10rtx
echo.
pause
