# Notice

This is an **unofficial**, non-commercial fan project for game preservation and
interoperability. It is **not affiliated with, endorsed by or sponsored by** The Walt Disney
Company, Disney Interactive or Media Station, Inc.

*Disney*, *The Lion King*, *Il Re Leone*, *Disney's Animated Storybook* and the related names and
characters are trademarks of their respective owners; *Media Station* and *MediaPlayback* are
trademarks of their respective owners. They are mentioned only to identify the game this port is
compatible with.

This repository contains only code written for this project. It contains **no game data** (no
`BOOT.STM`, `.CXT` files, executables, DLLs, images, sounds, music or texts of the original
game), **no original or decompiled code** and **no game logic**: the scripts of the game are
translated to C on the user's computer at build time, from the user's own copy of the game.
The original dissolve patterns, cursors and About box texts are read from the user's
`LIONKING.EXE` at run time and are not included. Do not commit or distribute `build\` together
with game files.

The engine was first written by studying ScummVM's reimplementation of the Media Station engine
(`engines/mediastation`; see [docs/REFERENCES.md](docs/REFERENCES.md)), then checked function by
function against the original executable.

Third-party tools downloaded by `tools\setup_tools.bat` (not included): w64devkit (Unlicense;
GCC runtime exceptions), SDL2 (zlib license); SDL2.dll is copied into `build\` next to the game.

To use this software you must own the original game.
