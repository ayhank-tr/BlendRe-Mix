@echo off
rem BGE-Remix P9.0: Remix's own menu, tried by hand. The six spheres open in the
rem player with RTX Remix; after two seconds Remix's menu comes up. Try clicking
rem a tab at the top of the menu, dragging a slider, ticking a check box and
rem typing a number into a box. Close the game window (or press Esc) when done,
rem then tell Claude what worked.
python "%~dp0..\run_local.py" p9menu
echo.
pause
