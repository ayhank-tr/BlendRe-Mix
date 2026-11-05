@echo off
rem BGE-Remix: Blender for testing by hand. Start Blender with this instead of
rem blender.exe: it runs the build the tests unpacked, under Windows' debugger,
rem and keeps every run's logs in C:\BGE-Remix\crash-reports (never touched by
rem the tests). If Blender crashes, the crash's report is there: tell Claude
rem the time this window shows.
rem   blender_remix.bat                 (Blender with no file)
rem   blender_remix.bat "D:\path\file.blend"
python "%~dp0blender_remix.py" %*
echo.
pause
