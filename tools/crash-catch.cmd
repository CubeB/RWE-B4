@echo off
REM Runs the debug build under gdb and writes a backtrace to
REM D:\RWE\crash.txt if it dies. Play normally; reproduce the crash; the
REM window closing is the end of the run. Then tell Claude it is done.
D:\msys64\usr\bin\bash.exe -lc "export MSYSTEM=MINGW64; export PATH=/mingw64/bin:/usr/bin:$PATH; export USERPROFILE='%USERPROFILE%'; export LOCALAPPDATA='%LOCALAPPDATA%'; export APPDATA='%APPDATA%'; cd /d/RWE/build && gdb -batch -ex run -ex 'echo \n===== BACKTRACE =====\n' -ex 'bt 40' -ex 'echo \n===== ALL THREADS =====\n' -ex 'thread apply all bt 15' -ex 'echo \n===== LOCALS =====\n' -ex 'info locals' --args ./rwe.exe > /d/RWE/crash.txt 2>&1"
echo.
echo Done. Backtrace (if any) is in D:\RWE\crash.txt
