@echo off
rem BGE-Remix P9.0: Rally Cross's glitch diagnostics 2.
rem
rem Our build with each of P8's techniques off in turn, and stock and ours
rem with groups of the level's objects hidden. About 15 minutes; leave the PC
rem alone until this window says it has finished.
python "%~dp0..\run_local.py" rallydiag2
echo.
pause
