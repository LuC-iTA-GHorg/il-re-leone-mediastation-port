@echo off
rem Downloads (after confirmation) the portable tools into tools\, checking SHA-256; nothing is
rem installed on the system.
rem   tools\setup_tools.bat [build]    w64devkit (GCC) + SDL2, ~78 MB
rem   tools\setup_tools.bat decompile  JDK + Ghidra for re\decompile.bat, ~740 MB
rem   tools\setup_tools.bat all        both
rem Add -Yes as second argument to skip the confirmation. Details in tools\setup_tools.ps1.
setlocal
set "ONLY=%~1"
if "%ONLY%"=="" set "ONLY=build"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_tools.ps1" -Only %ONLY% %2
exit /b %ERRORLEVEL%
