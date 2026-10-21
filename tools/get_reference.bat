@echo off
rem Downloads the ScummVM Media Station engine (engines/mediastation only, sparse checkout) into
rem reference\scummvm-sparse, for consultation. GPLv3 code: it is not part of this repository.
rem   tools\get_reference.bat [COMMIT]   (default: the commit studied for the port)
setlocal
set "REF=%~dp0..\reference"
set "COMMIT=%~1"
if "%COMMIT%"=="" set "COMMIT=ed949213a23440447ea402e263daa871394b6bf9"
if not exist "%REF%" mkdir "%REF%"
if not exist "%REF%\scummvm-sparse\.git" (
	git clone --filter=blob:none --sparse --no-checkout https://github.com/scummvm/scummvm.git "%REF%\scummvm-sparse" || exit /b 1
)
git -C "%REF%\scummvm-sparse" sparse-checkout set engines/mediastation || exit /b 1
git -C "%REF%\scummvm-sparse" fetch --depth 1 origin %COMMIT% || exit /b 1
git -C "%REF%\scummvm-sparse" checkout %COMMIT% || exit /b 1
echo OK: %REF%\scummvm-sparse\engines\mediastation
