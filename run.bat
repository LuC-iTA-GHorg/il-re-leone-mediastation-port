@echo off
rem Runs the port with the game data in ..\DATA (cursors from ..\LIONKING.EXE).
"%~dp0build\lionking.exe" --data "%~dp0..\DATA" --exe "%~dp0..\LIONKING.EXE" %*
