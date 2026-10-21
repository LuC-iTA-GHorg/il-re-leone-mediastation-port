# Graphics of LIONKING.EXE (blit, palette, sprites, movies, stage)

Second pass on the graphics engine (21/10/2026), compared with `engine\video.c`,
`engine\bitmap.c`, `engine\actors.c` (image, sprite, movie) and `engine\runtime.c` (stage and
dirty areas). Addresses as in [ORIGINAL.md](ORIGINAL.md). Commits (development tree): `c7b8de6`, `9fe6741` (movie), `c5a15bc` (CCC).

## Image blit (`10a0:0000`)

Arguments: drawing context, corner (`+0x1c` of the actor), bitmap, an unused word
(`+0x48` of the image, parameter 0x20), dissolve factor (double). Selection bits: compression
1 -> 1, 5 -> 0x40, 6 -> 0x80, 7 -> 8, **any other value -> 0** (uncompressed);
then `p = _ftol(f*100+0.5)`: `p != 100` adds 0x10 (0x20 if `p == 0`); always `| 4`.

| Case | Routine |
|---|---|
| uncompressed (0, 2-4, 8...) at 100% | `1188:3da4` -> `10e8:0000` (opaque) |
| RLE8 at 100% | `1188:40be` -> `10e8:09ee` -> `1118:0024` |
| 7 at 100% | `1188:3f30` (color 0 transparent) |
| uncompressed or 7 partial | `10b8:030e` -> `10b8:160e` |
| RLE8 partial | `10b8:03c0` -> `10b8:0ecc` |
| CCC 5 at 100% / CCC 6 at 100% | `10b8:1840` / `10b8:010e` (+ `10b8:19f6`) |
| CCC partial, any image at 0% | nothing |

- **Row of uncompressed data**: it is a top-down DIB; the row length is
  `((w*8+31) & ~31) / 8` computed from the width (`10e8:0000`, `10e8:09ee`), the header's
  "stride" field is not used. In The Lion King the 42 uncompressed images have
  `stride == w`, a multiple of 4 (11,795 RLE, none 7, none CCC). **Corrected** (`dib_stride`).
- Unknown compressions: before, a warning and no drawing; now opaque like 0. **Corrected**.
- Each routine scans the rectangles of the dirty region and calls the blit for each one, like our
  `draw_rect` per rectangle.
- CCC (5, 6): present in the executable, **not implemented** (warning). No data uses them.

## Decompression at load time (`1070:00c6`)

Bitmap constructor `1070:0000` (vtable `1198:0918`): if the load type is 1 and the
compression is 1 it calls `1070:00c6`: it allocates `DIB_stride * h`, **fills it with 0**
(`10e0:0000` -> `10f8:0024`, `rep stos`), draws the RLE into it with the same blitter as the screen
(`1188:425e`), then the bitmap becomes compression **0, opaque**, with the DIB row. So the skipped
areas become *opaque* color 0. Before: compression 7 (color 0 transparent) and decoding
with `bitmap_rle8_decode`. **Corrected** (`bitmap_read` uses `bitmap_rle8_blit`, the same routine
as the screen blit). In The Lion King no image or sprite uses decompression at load time.

## Palette

- **Entries 0 and 255 are never written**: `1188:4c60` (AnimatePalette) and `1188:4d2c` (SetPaletteEntries)
  use the range from `max(start, 1)` to `min(start+count, 255)`; `4d2c` takes entries 0 and
  255 from the system palette (Windows' static black and white). **Corrected**
  (`video_set_palette`; entry 255 starts white). The game always uses `1, 254`: no test
  screen changes.
- **Transitions**: `1190:a6dc` handles only 300, 301, 302; the other types do nothing (and
  cancel the "on sync" transition anyway). Types 303-310 taken from ScummVM removed.
  **Corrected**.
- Arguments read with the exact accessors (`1190:a6dc`): type as a token (`4e80`), duration
  as *time* (`4e26`: a value of another type is 0, immediate change), start and count as
  truncated floats (`4dc0`). The Lion King scripts never pass the duration.
- The real palette class is `1188:487c` (subclass of the manager `1190:b0b2`): +0x18 reads
  the palette (`1188:359c`), +0x1c sets it during the fade (`1188:35b2` -> `4c60`),
  +0x20 sets the final one (`1188:35ce` -> `4d2c`). The loop `1190:a9be` stops if
  +0x1c returns 1, i.e. when the screen is **not** palettized (`[obj+0x0A] == 0`): on a
  16/24-bit screen the original's fades jump straight to the final palette. The port
  reproduces the 256-color case (the one the game was made for).
- Duration, steps, 5 s maximum, no wait: already verified (ORIGINAL.md).
- The stream's "load palette" command saves only the registered palette; it reaches the screen
  only with 301/302 (like the port).

## Image (vtable `1190:cc0c`)

- Own parameters (`1190:c7a4`): 0x1b channel, 0x1f visible (+0x4a), 0x20 (+0x48, passed to the
  blit but unused), 0x32 load type (+0x50), 0x5dc dissolve (`1190:c8fe`:
  clamped to 0..1, invalidates if it changes). X/Y (0x5de/0x5df) and references to other images are **not**
  handled by the original's image (they go to the spatial base): the port accepts them
  (ScummVM); The Lion King does not use them.
- show/hide invalidate the box only if the result of the visibility test changes
  (before, the port always invalidated); sprites already did this (`1068:0d1e`). Same for movies
  (`1190:f998`, test `1190:fb46`: flag, dissolve, at least one frame on screen); the
  show/hide of a movie no longer updates the frames.
- X/Y (0x5de/0x5df) and the reference to another image (0x77b) are not parameters
  of the image in the original (`1190:c7a4` passes them to the base, which gives the fatal error
  "Attempt to read unrecognized parameter", `1190:272e`): removed, no Lion King image
  uses them (`msdump load` of all contexts without errors). The image's box is its
  box (+0x1c), not the size of the bitmap.
- Visible (vtable +0x20, `1190:c8cc`): start flag **and** dissolve != 0.0. Drawing
  (`1190:c96a`): at the corner of the box `+0x1c`.

## Sprite (vtable `1198:0000`, methods `1068:0566`)

**Corrected** (`actors.c`):

- the only default clip is 0x4b0 = `[0, frame_count - 1]`, added and made current
  when parameter 1000 is read (`1068:02b2` to `1068:0462`); the backward clip
  0x4b1 **does not exist** (it came from ScummVM);
- clips (0x3e9) are appended and the first one with that id wins;
- the default clip (0x3ea) becomes current immediately, while the parameters are being read;
- `setCurrentClip` with an unknown id leaves the clip and its id, but resets the frame
  to the first of the clip (`1068:0bf6`);
- forward only if `current < last`, backward only if `first < current` (unsigned 16-bit
  comparisons, `1068:0ba2`, `1068:0bcc`); playback scheduled only if
  `current < last` (`1068:0e52`). A clip with first > last stays stopped on the first
  frame.

Methods of the original: 0xca/0xcb show/hide (invalidates only if it changes), 0xce/0xcf play/stop,
0xdb reset, 0xdc setCurrentClip (without argument 0x4b0), 0xdd/0xde increment/decrement
(optional boolean argument, only `== 1`), 0xf0 clip id, 0xf1 setDissolve (0..1);
the rest goes to the spatial base (`1190:93a4`). Note: **there is no `isPlaying` (0x174)** for the
sprite: the port still accepts it (to be verified with `1190:93a4`, which belongs to the methods
part). Visible (`1068:0728`): flag and dissolve != 0. Invalidation: the actor's box
(`+0x1c`), once per frame change (`1068:0cfe`).

## Stage and dirty areas

- Children list in **ascending** order of (z, offset of the actor object)
  (`1198:9784`; insertion `1198:fac0`; re-sorting `qsort` `1198:fa9c` via
  `1198:976c`); drawing from last to first (`1198:9d0e`, iterator `1198:f836`): higher z
  behind, like the port. **With equal z** the original puts the object with the lowest offset
  in front (address in the SmartHeap heap, low word only): not reproducible; the port puts
  the last-added actor in front. Unchanged.
- Mouse and keyboard ask for the children from the first (`1198:f7f0`), i.e. from the nearest.
- Drawing a child: visible (+0x20) and box +0x1c intersecting the dirty region
  (`10a8:0090` -> `10c0:07c4`); the region is a true region of rectangles (`10c0`).
- Adding a child (`1198:9c36`): sorted insertion, then invalidates **only that child**
  if visible (before, the port invalidated the whole screen). Removal (`1198:9c8e`): invalidates the
  child if visible, then removes it (before, no invalidation). **Corrected**.
- z change (`1198:99aa`): re-sorts; invalidates only if the child is visible and changes place
  relative to a visible neighbor that intersects it. The port (in `actor.c`, not mine) always
  invalidates: same result on screen.
- Screen event (`1190:8e2e`): with automatic update just re-enabled it redraws
  **the whole stage** (`1190:8ec8` -> `1190:a10a`), otherwise only the dirty region
  (`1190:8e76` -> `1190:a0e8`); then copies to the window (+0x14 of the platform, `1188:3608`)
  and the "on sync" transition. **Corrected** (`update_all` after re-enabling).

## Movie (class `1190:fec6`, constructor `1190:de58`)

**State machine** (table built by `1190:e3a0` with `1188:f04e`/`1198:bfb0`: 9 states x
5 events; -2 = event ignored, -1 = error; on entering a state its action is run,
table `DS:034a`: `f2e8`, `f4ca`, `f5f6`, `f6f6`, `f728`, `f75a`, `f7ce`, `f886`, `f90c`).
Events (`1190:f2a6`, high word = event, low word = argument): 0 stop (argument = reason),
1 start, 2 data ready (from loading), 3 pause, 4 resume.

| State | Entry action | ev0 | ev1 | ev2 | ev3 | ev4 |
|---|---|---|---|---|---|---|
| 0 stopped | `f2e8`: stops sound and stream, reason event (1 MovieFailure 0x16, 2 MovieAbort 0x15, 3 MovieStopped 0x1f, 4 MovieEnd 0x0f), shows the frames at time 0 (`1198:b732(0)`) | – | 1 | err | – | – |
| 1 start | `f4ca`: reads the stream; if it fails, stop with reason 1 | 0 | – | 2 | 3 | – |
| 2 playing | `f5f6`: MovieBegin (0x20), clock started | 0 | – | err | 7 | – |
| 3 pause during start | `f6f6` | 0 | – | 5 | – | 4 |
| 4 resume during start | `f728` | 0 | – | 2 | 3 | – |
| 5 ready but paused | `f75a` | 0 | – | err | – | 6 |
| 6 resume | `f7ce`: MovieBegin, clock started | 0 | – | err | 7 | – |
| 7 paused | `f886`: clock and sound paused | 0 | – | err | – | 8 |
| 8 resumed | `f90c`: clock restarted, sound resumed | 0 | – | err | 7 | – |

Methods (`1190:f998`, only when loading is complete): 0xce play = stop with reason 2 **then**
start (a movie already playing sends MovieAbort and restarts), 0xcf stop reason 3, 0xd0 pause (ev3),
0xd1 resume (ev4), 0xca/0xcb show/hide, 0xf1 setDissolve, 0x140 ignored. **Reproduced**
(commit `9fe6741`): replay with MovieAbort, pause/resume without events, 0x140, a sound error
stops the movie with MovieFailure, queued stop events no longer change the state.
In the port loading is synchronous: states 1, 3, 4, 5, 6 take zero time.

**Frames** (`1198:b140` reads: type, start, end, x, y, image, second image,
"keep"). `1198:b732(t)` (at every update, at stop and at the end of loading with t = 0):
a frame is on screen if `start <= t < end`; when `t >= end` it is hidden and, if it has no
"keep", it is **deleted** (not seen again until the stream is re-read); images
are kept only while some on-screen frame uses them, the others are freed. Every change
invalidates the image's box (and that of the second image). Drawing (`1198:bb28`): only
frames of **type 1**, with the generic blit and the **movie's dissolve**, in the order of the
list filled from the last frame to the first (a frame read earlier is on top). Types
2/3 (the "delta" of later engines) and the second image are **never drawn**: no
composition of deltas in the original, and so the question of color 0 in deltas does not exist.
In The Lion King's data: 9,834 frames, all of type 1, none with a second image
(89 with "keep"). **Reproduced** (type 1 only, reverse order, dissolve).

**Timing and audio** (`1190:ef04`, `1190:f024`): with the sound started (`+0xdc == 1`) the movie
time is that of the audio object (`1198:a902` on the object `+0xa8`), otherwise the movie's
clock (`1198:19be` on `+0xac`, frozen in `+0xb2` while paused, restarted from `+0xba`); the movie
schedules a timer for the difference between the time of the next block and the current time.
**Reproduced**: while the movie's sound plays the time is `audio_seq_position_ms`,
otherwise the movie's clock (with pause).
- Consequence verified on the executable for the title (screen 112, `boot_menu`): the
  end-of-read command 0x10 queues ScreenEntry and **immediately re-enables**
  automatic update (`1190:a24a` writes the flags directly, no event), so the
  forced full redraw happens at the end of the same iteration, before ScreenEntry. On the
  next iteration ScreenEntry shows the movie (which does not invalidate: the test `1190:fb46` requires a
  frame on screen, `+0x88` = number of entries in the draw list, 0 before play
  because the movie is streamed) and registers the "on sync" FadeToPalette. The screen event redraws
  and copies only if the dirty list is not empty (`1190:8f1a`: `[DS:0336]+0x40 >= 1`), so the
  fade starts only with the first frame of the movie, after play; during the blocking
  fade (0.5 s) the movie's clock and sound keep running. The movie therefore ends 0.5 s
  after its MovieBegin earlier than the port did: the `boot_menu` baseline of the regression
  suite (development tree) was updated. The frames shown are those at time
`_ftol(t*1000) - 1` ms (`1190:fbc4` -> `1198:b732`).
- Service before drawing (`1190:fbc4`, also for hidden movies): 2 = skip the update
  if the stream block that would be needed now has not been read yet (in the port the
  stream is read entirely at play: it does not happen), 0 = something changed, 1 = nothing.

Not reproduced: deletion of frames without "keep" after the end (the port keeps them all
and shows at time 0 only if there is at least one "keep" frame), deactivation of
automatic update during start (`1190:fe64` on `DS:0332`, re-enabled in
`f5f6`/at stop): with synchronous loading it has no visible effects.

## CCC (compressions 5 and 6): implemented

- 5 (`10b8:1840`): 4x4 cells of 4 bytes (color 0, color 1, 16-bit mask, high byte
  first; pixel (r, c) = color 1 if bit 15 - (4r + c) is 1), in order of cell rows,
  clipped at the edges. Drawn all at once, **without** the dirty rectangles (the port
  clips only to the screen; if another actor above is redrawn in a rectangle first,
  the port, drawing per rectangle, may cover it: a case not reproduced, no data uses it).
- 6 (`10b8:010e` -> `19f6`, per dirty rectangle): 4-byte records: `ff 00 00 00` end,
  `fe k ..` transparent key = k (0 at the start), otherwise header: n = byte0*256 +
  byte1 consecutive cells; if byte2 != 0 the row goes down byte2 cells and the column becomes
  byte3, otherwise the column advances byte3 cells. A cell whose color 0 equals the key
  draws only the 1 bits. It stops at the first row below the rectangle.
- Test: `tools\ccctest\ccctest.bat` (hand-written images, whole and clipped).

## Dirty region (reproduced, commit `149d6be`)

- The root stage keeps the invalidated rectangles **unclipped**, in order of arrival
  (`1198:961a` -> `1190:7c76`): an empty rectangle is ignored; the list is scanned from the first: if
  an element contains it, it is not added; if it contains an element, it replaces it (the first one,
  the other contained ones remain); otherwise it is appended. No merging and no limit (before:
  64 clipped rectangles, merged into one when full). The list is emptied after drawing
  (`1190:a12c`).
- On redraw the region of the drawing context (`10c0`) is emptied (`10c0:0438`) and receives the
  rectangles one by one clipped to the screen with the same rule (`10c0:0756` ->
  `10c0:07fc`), then it is intersected with the stage's box (`10c0:0b6a` -> `1190:7f10`). The
  row structure (x intervals per row) that the region keeps alongside serves membership tests;
  blits use the rectangle list (`region+0x0c`).
- The stage draws **each child once** if its box touches a rectangle of the
  region (`1198:9d0e`, `10c0:07c4`); each blit routine scans the rectangles from the first
  (`1188:3da4`, `40be`, `3f30`, `10b8:030e`, `03c0`, `010e`): so the phase of the dissolve
  of uncompressed images restarts in every rectangle as in the original, and CCC 5
  (`10b8:1840`, once only, without rectangles) no longer covers the actors above it.
  Before, the port drew all actors rectangle by rectangle.
- Full redraw (`1190:a10a` -> `1198:965e`): a single rectangle, the stage's box.

## Order at equal z (measured: no visible effect in The Lion King)

The original compares only the low word of the far address of the actor object (`1198:9784`).

**Allocator.** Objects come from `operator new` (`1120:1446`) -> `1120:13b2` ->
`1120:09a6` on SmartHeap's default pool (`DS:242e`, created by `1120:127a`): blocks up to
the pool's fixed size (`pool+0x18`) from a per-page LIFO free list (`1120:07fc`),
medium blocks (up to `pool+0x1c`) with a "next fit" search on the pages starting from the last
one used (`1120:0a7e` -> `0b90`), large ones directly from global memory (`1120:05e2`). Actors
(from 0x1e to 0x10c bytes) are small or medium blocks: a block freed when a
context is unloaded is reused first. The address, and so the order, depends on all the
previous allocations and frees of the session (contexts loaded and unloaded, the player's
path, streams, bitmaps); objects in different segments are compared by offset only.
It is not a function of the load order: it cannot be reproduced. The port keeps
"last added in front".

**Measurement (21/10/2026).** Static listing with `msdump load ..\DATA` (now also lists the
common actors of context 1100): 1,448 pairs of spatial actors in the same context (or in 1100) with
the same z and touching boxes (1,369 among actors that draw, almost all movies of the same
character or of different languages, which the scripts do not show together; 40 among hotspots,
almost all the Italian word and the English one, never active together; the other 39 are a
hotspot and a drawn actor, which do not interact). Then a temporary probe
in the stage drawing (not in the repository: for each pass, a pixel written by one actor and
rewritten with another value by an actor with the same z) and in the mouse target lookup
(two active and loaded hotspots with the same z that contain the point), with all 40
cases of the regression suite (compiled):

- **drawing: a single situation**, in the Timon and Pumbaa game (225): sprite 1085 (z 2000,
  box 181,159 191x105) and the jump movie 1086 or 1091 (z 2000, 158,178 279x294; by
  symmetry also 1087/1088/1092/1093). The click on a bug (hotspot 1066, 1082, ...) starts
  timer 1055 (time 0: hides the sprite) and immediately the movie; the port, which reads the stream
  synchronously, shows the first frame already in the same iteration, and the sprite disappears
  on the next one: **a single screen update** (~33 ms) with the two overlapped, the movie in front
  (added later); with the opposite order the sprite's still pose would be seen on top of the first
  frame of the jump. In the original the movie starts from state 1 (reading the stream)
  and the frames arrive after the timer fires, so the overlap probably does not
  exist. Elsewhere no pair: the scripts avoid the overlap (on page 1
  `p01_words_image_*`, z 100, is hidden before starting `p01_narration_*`, z 100, same
  box); the constellations of the star game (z 10) do not overlap in pixels;
- **mouse: a single pair**, `busy_blocker` (111, context 1100, z 0, full screen) and
  `credits_click_quit` (1112, credits 157, z 0, full screen), only between the loading of
  context 157 and its ScreenEntry, which immediately deactivates `busy_blocker`. In that
  window only who becomes the "entered" hotspot (`DS:0342`, and its cursor) matters: neither has
  a MouseEntered response, and a MouseDown always arrives after the ScreenEntry already queued.

Limit of the measurement: the page cases turn pages (narration, ambience, corners) but do not
click the characters. The click animations cannot start together anyway: every click hotspot
activates `busy_blocker` (z 0, in front of everything, takes every click) until the animation
ends. The only case left unmeasured is a "keep" frame left by an animation
under another animation with the same z and an overlapping box.

Conclusion: in The Lion King the order at equal z has no visible effects in the measured cases (apart
from the single update above, due to the synchronous loading of movies); the port does not
change (`stage_before` in `runtime.c`). The probe and the measurement can be redone in a few minutes
(see above) if checking the clicks on the characters is needed.

## For the owner of `script.c` / `actor.c`

- Common spatial methods: base `1190:93a4`. Visibility of image and sprite = flag **and**
  dissolve != 0 (an image with dissolve 0 is not "visible" even for the mouse).
- Sprite: no `isPlaying` (0x174): it stays in the port.
- z change: conditional invalidation (above).

## To do

- Movie: deletion of frames without "keep" after the end; end of the movie decided
  by the clock (`1190:fda4`) or by the sound instead of the last frame.
