@echo off
rem BGE-Remix P9.0: Rally Cross diagnostic run on this PC.
rem
rem Rally Cross is driven by simulated keys for 15 seconds in stock 2.79b, in
rem our build (OpenGL) and in our build with RTX Remix, each of ours under the
rem debugger, then again with one suspect switched off at a time (the water,
rem the 2D filters, the damage; with Remix also its live bge.texture pictures).
rem The game is read from C:\BGE-Remix\rallycross or from the Downloads folder
rem it came in; test copies are made beside it and removed afterwards.
rem
rem Like run_local.bat: it unpacks the newest blendremix-win64 download from
rem C:\BGE-Remix\transfer and writes the results to
rem C:\BGE-Remix\tests-local\results\rally. About 15 minutes. Game windows open
rem and close by themselves: leave the PC alone until this window says it has
rem finished.
python "%~dp0..\run_local.py" rally
echo.
pause
