# References: the ScummVM Media Station engine

The port was written by studying ScummVM's reimplementation of the Media Station engine
(GPLv3), which reconstructs the original player from its disassembly.

- Repository: https://github.com/scummvm/scummvm, master branch, commit
  `ed949213a23440447ea402e263daa871394b6bf9` (20/10/2026), folder `engines/mediastation`.
- Local copy (sparse checkout, not in the repository): `reference\scummvm-sparse`, downloadable
  with `tools\get_reference.bat`.

## File correspondence

| ScummVM (`engines/mediastation/`) | This project (`engine\`) |
|---|---|
| `datafile.*` | `datafile.c/h` |
| `boot.*`, `context.*`, `mediastation.*` (ImtGod) | `god.c/h` |
| `actor.*` | `actor.c/h` |
| `actors/*` (screen, image, sprite, sound, movie, hotspot, timer, path, palette, cursor, document) | `actors.c` |
| `minigames/dotgame.*` | `actors_dotgame.c` |
| `minigames/stalkingzazu.*`, `statemachine.h` | `actors_zazu.c` |
| `mediascript/scriptvalue.*`, `collection.*` | `value.c/h` (collections also in `script.c`) |
| `mediascript/codechunk.*`, `function.*`, `scriptresponse.*`, `scriptconstants.*` | `script.c/h`, `constants.h` |
| `bitmap.*`, `graphics.*` | `bitmap.c/h`, `video.c/h` |
| `audio.*`, `actors/sound.*` | `audio.c`, `audio_data.c`, `actors.c` |
| `events.*`, `clients.*`, `actors/stage.*` | `runtime.c` |
| `cursors.*` | `cursors.c` |

## Not ported (The Lion King does not use it)

- Nested stages, cameras, canvases, text, fonts, disk images, printer: all of The Lion King's
  actors live on the root stage (id 2).
- Movie proxies (separate layers, later engines).
- CCC bitmaps (compression 5/6) and 4-bit IMA ADPCM audio.
- Mac cursors (only the NE cursors of `LIONKING.EXE` are read).
- Minigames of other titles (mazes, checkers, Sophie...).

## Known differences

- Partial sprite dissolve: the original uses 25 tables of 25x25 (`dissolvepatterns.h`); here a
  4x4 Bayer matrix that draws the same proportion of pixels.
- ScummVM's approximations are inherited: streams are read all at once instead of being streamed
  from the CD, no cache, gamma fixed at 1.
- Two deviations for robustness in the interpreter (see `SCRIPT.md`).

## Note on detection in ScummVM

ScummVM's detection tables list only the English GB version of The Lion King
(`LIONKING.EXE`, 363232 bytes); the Italian version (363200 bytes) is not recognized there.
