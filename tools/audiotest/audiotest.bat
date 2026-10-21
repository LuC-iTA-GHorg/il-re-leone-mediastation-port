@echo off
rem Unit test of the PCM formats and sample decoding of the wave player (engine\audio_data.c).
rem Output in build\ (or BUILD_OUT).
setlocal
set "ROOT=%~dp0..\.."
set "OUT=%ROOT%\build"
if defined BUILD_OUT set "OUT=%BUILD_OUT%"
if not exist "%OUT%" mkdir "%OUT%"
set "PATH=%ROOT%\tools\w64devkit\bin;%PATH%"
set "E=%ROOT%\engine"
gcc -std=c11 -O2 -Wall -Wextra -I"%E%" -o "%OUT%\audiotest.exe" "%ROOT%\tools\audiotest\audiotest.c" "%E%\audio_data.c" "%E%\datafile.c" "%E%\value.c" || exit /b 1
"%OUT%\audiotest.exe"
