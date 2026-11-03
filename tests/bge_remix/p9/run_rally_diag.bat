@echo off
rem BGE-Remix P9.0: Rally Cross's glitch diagnostics.
rem
rem Stock 2.79b and our build with the sun's shadow off and with the water's
rem surface hidden, RTX Remix with each suspect off, and the sun's shadow map
rem written out. About 10 minutes; leave the PC alone until this window says
rem it has finished.
python "%~dp0..\run_local.py" rallydiag
echo.
pause
