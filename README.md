# Il Re Leone (The Lion King Animated Storybook) – unofficial SDL2 port

*Il Re Leone – Libro Animato Interattivo* (Disney Interactive, 1995) is the Italian edition of
*Disney's Animated Storybook: The Lion King*, a Windows 3.1 game that runs on the **Media
Station "MediaPlayback"** engine. This repository builds a native 64-bit Windows version of it:
the engine is reimplemented from scratch in C11 + SDL2 and verified, function by function,
against the original executable. No emulator, no virtual machine, no OTVDM.

> **You need your own copy of the original game.** This repository contains **no game data, no
> original or decompiled code and no game logic**. See [NOTICE.md](NOTICE.md).

## Status

The whole game plays from start to end: intro, title, menu, reading the book with narration
and highlighted words, page selection, the 18 pages, the two minigames (Stalking Zazu and the
constellations), the Timon and Pumbaa activity, credits and exit. The engine reproduces
`LIONKING.EXE` including the paths this game never uses: every script opcode and built-in
function, the method sets of every actor class, all bitmap compressions (RLE, CCC), the
original dissolve patterns, palette transitions, the movie state machine with audio-driven
timing, the event loop, input and sound device rules. Details and the exe addresses are in
[docs/](docs/ORIGINAL.md).

Tested with the Italian version 2.0 (`LIONKING.EXE`, 363,200 bytes). Other language versions
use the same engine and may work, but have not been tested.

## Requirements

- Windows 10 or 11, 64-bit, and git.
- From the original CD: the `DATA` folder (`BOOT.STM` and the `.CXT` files) and `LIONKING.EXE`
  (read at start-up for the cursors, the dissolve patterns and the About box; nothing is copied).
- About 80 MB of free space for the portable build tools (downloaded on request).

## Building

```bat
git clone https://github.com/LuC-iTA-GHorg/il-re-leone-mediastation-port.git
cd il-re-leone-mediastation-port
rem put DATA\ and LIONKING.EXE of your CD next to this folder (..\DATA, ..\LIONKING.EXE),
rem or set GAME_DATA to the folder with BOOT.STM, then:
build.bat
```

The first time, `build.bat` shows what it is going to download (w64devkit GCC 16.2 and SDL2
2.32.10 from their official GitHub releases), asks for confirmation and checks the SHA-256 of
every file. Everything stays in `tools\`; nothing is installed. `tools\setup_tools.bat build`
does the same by hand.

The scripts of the game are translated to C from **your** game data into `build\gamesrc\` and
compiled into the executable (`msdump transpile`, see [docs/RECOMPILATION.md](docs/RECOMPILATION.md)).
Do not publish those generated files. Without game data at build time the port still builds:
the scripts then run in the bytecode interpreter.

Output: `build\lionking.exe`, `build\SDL2.dll` and `build\msdump.exe` (data inspector).

## Running

```bat
run.bat                                    rem = build\lionking.exe --data ..\DATA
build\lionking.exe --data FOLDER           rem FOLDER: the one with BOOT.STM
```

Options: `--fullscreen`, `--scale N` (window N times 640x480, default 2), `--exe FILE` (the
original `LIONKING.EXE`, default `FOLDER\..\LIONKING.EXE`), `--screen ID` (start from a screen),
`--interpret` (run the scripts in the interpreter instead of the recompiled C), `--trace`
(events, loads and scripts on stderr). Timed input for repeatable tests: `--click MS:X:Y`,
`--mouse MS:down|up|move:X:Y`, `--key MS:CODE`, `--shot MS:FILE.bmp`, `--quit-after MS`,
`--seed N`.

Keys of the port: Alt+Enter full screen, F12 screenshot. Keys of the original game (not in the
manual): right/left arrow next/previous page, Space back to the menu, L switch between Italian
and English, Ctrl+V the About box.

## Tests

`tools\msdump\selftest.bat` (every opcode, operand and built-in: interpreter vs recompiled C),
`tools\ccctest\ccctest.bat` (CCC bitmaps), `tools\audiotest\audiotest.bat` (audio formats),
`build\msdump.exe streams FOLDER` (every data chunk read to the last byte, no warnings).

## Documentation

| File | Content |
|---|---|
| [docs/ENGINE.md](docs/ENGINE.md) | architecture of the port |
| [docs/DATA_FORMAT.md](docs/DATA_FORMAT.md) | BOOT.STM and CXT files, chunks, actors, images, movies, audio |
| [docs/SCRIPT.md](docs/SCRIPT.md) | script bytecode |
| [docs/RECOMPILATION.md](docs/RECOMPILATION.md) | static recompilation of the scripts to C |
| [docs/ORIGINAL.md](docs/ORIGINAL.md) and `docs/ORIGINAL_*.md` | how the original executable works, with addresses |
| [docs/REFERENCES.md](docs/REFERENCES.md) | ScummVM as the starting reference |

## License

No license has been chosen yet for the code of this project: until then, all rights are reserved
by its author. The game, its names and its data belong to their respective owners.
