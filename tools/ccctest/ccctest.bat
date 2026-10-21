@echo off
rem Unit test of the CCC bitmap decoders (compressions 5 and 6), engine\bitmap.c.
rem Output in build\ (or BUILD_OUT).
setlocal
set "ROOT=%~dp0..\.."
set "OUT=%ROOT%\build"
if defined BUILD_OUT set "OUT=%BUILD_OUT%"
if not exist "%OUT%" mkdir "%OUT%"
set "PATH=%ROOT%\tools\w64devkit\bin;%PATH%"
set "E=%ROOT%\engine"
gcc -std=c11 -O2 -Wall -Wextra -I"%E%" -o "%OUT%\ccctest.exe" "%ROOT%\tools\ccctest\ccctest.c" "%E%\bitmap.c" "%E%\datafile.c" || exit /b 1
"%OUT%\ccctest.exe"
