@echo off
rem Self-test of the script VM (tools\msdump\selftest.c): hand-assembled bytecode for every
rem opcode, operand type and built-in function of LIONKING.EXE (and the extensions); interpreter
rem and recompiled C must give the same results. Output in build\ (or BUILD_OUT).
setlocal
set "ROOT=%~dp0..\.."
set "OUT=%ROOT%\build"
if defined BUILD_OUT set "OUT=%BUILD_OUT%"
if not exist "%OUT%" mkdir "%OUT%"
set "PATH=%ROOT%\tools\w64devkit\bin;%PATH%"
set "E=%ROOT%\engine"
set "SRC="%E%\script.c" "%E%\script_rt.c" "%E%\datafile.c" "%E%\value.c" "%E%\bitmap.c" "%E%\god.c" "%E%\actor.c" "%E%\actors.c" "%E%\actors_zazu.c" "%E%\actors_dotgame.c" "%E%\audio_data.c" "%E%\runtime_stub.c" "%ROOT%\tools\msdump\transpile.c" "%ROOT%\tools\msdump\selftest.c""
set "CFLAGS=-std=c11 -O2 -Wall -Wextra -I"%E%""
rem icon and version information (tools_res.o, made by build.bat) when available
set "RES="
if exist "%OUT%\tools_res.o" set "RES="%OUT%\tools_res.o""
gcc %CFLAGS% -o "%OUT%\selftest_gen.exe" %SRC% %RES% || exit /b 1
"%OUT%\selftest_gen.exe" gen "%OUT%\selftest_script.c" || exit /b 1
gcc %CFLAGS% -DSELFTEST_NATIVE -o "%OUT%\selftest.exe" %SRC% "%OUT%\selftest_script.c" %RES% || exit /b 1
"%OUT%\selftest.exe"
