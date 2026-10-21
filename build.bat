@echo off
rem Builds the SDL2 port of Disney's Animated Storybook: The Lion King (Media Station engine)
rem with the portable GCC in tools\w64devkit and SDL2 in tools\SDL2. Nothing is installed.
rem   build.bat [debug^|clean]
rem Output in build\: lionking.exe (the game), msdump.exe (data inspector), SDL2.dll.
setlocal EnableDelayedExpansion
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "OUT=%ROOT%\build"
rem BUILD_OUT=<absolute folder> builds somewhere else (parallel builds)
if defined BUILD_OUT set "OUT=%BUILD_OUT%"

if /i "%~1"=="clean" (
	if exist "%OUT%" rmdir /s /q "%OUT%"
	echo build\ deleted
	exit /b 0
)

set "TOOLS=%ROOT%\tools"
set "MISSING="
if not exist "%TOOLS%\w64devkit\bin\gcc.exe" set "MISSING=1"
if not exist "%TOOLS%\SDL2\x86_64-w64-mingw32\bin\SDL2.dll" set "MISSING=1"
if defined MISSING (
	rem portable GCC and/or SDL2 missing: tools\setup_tools.bat lists what it would download
	rem ^(fixed versions, SHA-256 checked, everything stays in tools\^) and asks first
	echo Portable GCC ^(tools\w64devkit^) or SDL2 ^(tools\SDL2^) missing.
	call "%TOOLS%\setup_tools.bat" build
	if not exist "%TOOLS%\w64devkit\bin\gcc.exe" (
		echo Missing tools\w64devkit ^(portable GCC^). See README.md, "Strumenti".
		goto :error
	)
	if not exist "%TOOLS%\SDL2\x86_64-w64-mingw32\bin\SDL2.dll" (
		echo Missing tools\SDL2 ^(SDL2 for MinGW^). See README.md, "Strumenti".
		goto :error
	)
)
set "SDL2_DIR=%TOOLS%\SDL2\x86_64-w64-mingw32"
set "PATH=%TOOLS%\w64devkit\bin;%SDL2_DIR%\bin;%PATH%"

set "CFLAGS=-std=c11 -O2 -Wall -Wextra"
if /i "%~1"=="debug" set "CFLAGS=-std=c11 -O0 -g -Wall -Wextra"
if not exist "%OUT%" mkdir "%OUT%"
set "E=%ROOT%\engine"

rem data layer, shared by the game and the tools
set "DATA_SRC="%E%\datafile.c" "%E%\value.c" "%E%\bitmap.c" "%E%\god.c" "%E%\actor.c" "%E%\actors.c" "%E%\actors_zazu.c" "%E%\actors_dotgame.c" "%E%\audio_data.c""

rem script VM; the recompiled scripts are chosen after msdump is built (see below)
set "SCRIPT_SRC="%E%\script.c" "%E%\script_rt.c""

rem Windows resources (tools\package\lionking.rc for the game, tools.rc for the tools): icon
rem and version information on every exe, so antivirus heuristics do not flag bare unsigned
rem executables. windres fails on include paths with spaces, so it compiles copies in the
rem build folder.
if not exist "%OUT%\res" mkdir "%OUT%\res"
copy /y "%ROOT%\tools\package\lionking.rc" "%OUT%\res\" >nul || goto :error
copy /y "%ROOT%\tools\package\tools.rc" "%OUT%\res\" >nul || goto :error
copy /y "%ROOT%\tools\package\lionking.ico" "%OUT%\res\" >nul || goto :error
pushd "%OUT%\res"
windres -o ..\lionking_res.o lionking.rc || (popd & goto :error)
windres -o ..\tools_res.o tools.rc || (popd & goto :error)
popd

echo [1/3] msdump
gcc %CFLAGS% -I"%E%" -o "%OUT%\msdump.exe" "%ROOT%\tools\msdump\msdump.c" "%ROOT%\tools\msdump\transpile.c" %SCRIPT_SRC% "%E%\script_native_none.c" %DATA_SRC% "%E%\runtime_stub.c" "%OUT%\tools_res.o" || goto :error

rem Recompiled game scripts:
rem  - generated\ (development tree, committed there);
rem  - otherwise translated now from the user's own game data into build\gamesrc\ (public port:
rem    the game logic is never in that repository). Data folder: GAME_DATA, else ..\DATA, else DATA;
rem  - with no data: none, every script runs in the interpreter.
set "GAME_SRC="
if exist "%ROOT%\generated\scripts_index.c" (
	for %%f in ("%ROOT%\generated\*.c") do set "GAME_SRC=!GAME_SRC! "%%~ff""
	goto :have_scripts
)
set "DATA_DIR=%GAME_DATA%"
if "%DATA_DIR%"=="" if exist "%ROOT%\..\DATA\BOOT.STM" set "DATA_DIR=%ROOT%\..\DATA"
if "%DATA_DIR%"=="" if exist "%ROOT%\DATA\BOOT.STM" set "DATA_DIR=%ROOT%\DATA"
if "%DATA_DIR%"=="" (
	echo No game data found ^(set GAME_DATA to the folder with BOOT.STM^): scripts will be interpreted.
	set "GAME_SRC="%E%\script_native_none.c""
	goto :have_scripts
)
echo      translating the game scripts from "%DATA_DIR%" into build\gamesrc\ ^(do not publish them^)
if exist "%OUT%\gamesrc" rmdir /s /q "%OUT%\gamesrc"
mkdir "%OUT%\gamesrc" || goto :error
"%OUT%\msdump.exe" transpile "%DATA_DIR%" "%OUT%\gamesrc" >nul || goto :error
for %%f in ("%OUT%\gamesrc\*.c") do set "GAME_SRC=!GAME_SRC! "%%~ff""
:have_scripts

echo [2/3] lionking (SDL2)
gcc %CFLAGS% -I"%E%" -I"%SDL2_DIR%\include\SDL2" -o "%OUT%\lionking.exe" ^
	"%E%\main.c" "%E%\runtime.c" %SCRIPT_SRC% %GAME_SRC% "%E%\video.c" "%E%\audio.c" "%E%\cursors.c" ^
	%DATA_SRC% "%OUT%\lionking_res.o" -L"%SDL2_DIR%\lib" -lmingw32 -lSDL2main -lSDL2 -lm || goto :error

echo [3/3] SDL2.dll
copy /y "%SDL2_DIR%\bin\SDL2.dll" "%OUT%\" >nul || goto :error

echo.
echo OK: %OUT%\lionking.exe
echo Run: build\lionking.exe --data "..\DATA" (folder with BOOT.STM and the .CXT files)
exit /b 0

:error
echo.
echo BUILD FAILED
exit /b 1
