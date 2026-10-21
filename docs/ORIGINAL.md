# The original executable (LIONKING.EXE)

Goal: use `LIONKING.EXE` (Win16 NE, 363,200 bytes, 08/09/1995) and `IMTWIN.DLL` as the source of
truth instead of ScummVM's reconstruction. This file records what was found and what was
confirmed or corrected in the engine. First pass (20/10/2026): partial map.

## How to reproduce

The analysis tooling (Ghidra scripts and output) belongs to the development tree (private); it is
not part of the public repository. It is described here for completeness:

- `re\decompile.bat [game_folder]` imports `IMTWIN.DLL` and `LIONKING.EXE` into Ghidra (headless,
  portable tools in `src\tools`: `env.bat`, JDK, Ghidra 12.1, downloaded by
  `tools\setup_tools.bat decompile`) and writes to
  `re\out\<MODULE>\`: `.asm` (disassembly with references), `.c` (decompiled),
  `functions.csv`, `strings.txt`, `imports.txt`, `segments.txt`. About 4 minutes.
- `re\out\` is in `.gitignore`: it contains Disney code and **must never go into a repository**.
- `re\ExportListing.java`: Ghidra script. Before exporting it creates the functions that the
  automatic analysis does not find (code reached only through C++ vtables and far pointers): it
  looks for 16-bit MSC prologues (`ENTER n,0`, `INC BP; PUSH BP; MOV BP,SP`, ...) after a
  `RET/RETF` and re-runs the analysis. From 2164 to ~3050 functions; some bytes remain
  undisassembled (unresolved jump tables, see below).
- `re\ne_tool.py`: reads the executable by Ghidra address (`segs`, `jt` jump tables, `ops` opcode
  handlers, `refs` who points to an address according to the NE relocations, `vt` vtables with
  resolved segments, `bytes`). It prints addresses only.
- Addresses: Ghidra maps NE segment *n* to selector `0x1000 + (n-1)*8`
  (segment 1 = `1000:`, 50 = `1188:`, 64 = DGROUP = `11f8:`). The segment words of the `CALLF`s in
  the file are relocation chains: finding the destination requires Ghidra or the relocation
  table, whereas the offset is correct.

## Structure of the executable

| Segments | Contents |
|---|---|
| 1-13 (`1000`-`1060`) | exception class names (`EX_EventLoopStateCantHappen`, `EX_CdRomStreamFailure`, `EX_ActorMapBadActorClassId`, ...) |
| 14-48 (`1068`-`1178`) | runtime: MFC 2.x (`CWinApp`, `CPalette`, ...), SmartHeap (`1128`), blit (`10a0`, `10b8`) |
| 49 (`1180`) | C library (MSC 7/8: `_ftol` at `1180:1202`, `fmod` at `1180:119a`, `findfirst`...) |
| 50 (`1188`) | Windows platform layer: window, message loop, palette, WinG/DIB, waveOut audio, CD, `imtplay.ini` profile |
| 51-52 (`1190`, `1198`) | Media Station engine: values, events, timers, actors, script interpreter |
| 53 (`11a0`) | SmartHeap error handling, cursors (`LoadCursor`/`SetCursor`) |
| 54-63 | small data segments of the exceptions |
| 64 (`11f8`) | DGROUP: constants, dissolve tables, C library strings |

`IMTWIN.DLL` (2,976 bytes) exports only `WAVEPLAYCB` (`1000:07b0`): the `waveOutOpen` callback;
on `MM_WOM_DONE` (0x3bd) it writes `timeGetTime()` into the completed buffer and does a
`PostMessage` to the player window. So the end of a sound arrives as a message in the main loop.

## Identified functions <-> port engine

| Original | What it does | In the port | Status |
|---|---|---|---|
| `1188:fb70` | engine event loop (see below) | `rt_run` | **confirmed**, alarm order corrected |
| `1188:1260` | modal message loop in the style of `CWinApp::Run` (platform vtable +0x0C), used while the engine is suspended | — | found |
| `1188:12ec` | stops the loop (`app+0x100 = 0`) | `running = 0` | confirmed |
| `1188:11e4` | drains messages while the object in `[DS:0242]+0x38` changes (used during blocking waits) | `present_pumping` | probable |
| `1188:1332` | current time in seconds: `timeGetTime() * 0.001` (constant at DS:2E40) | `SDL_GetTicks()/1000.0` | confirmed |
| `1188:66fc`, `1188:68a6` | is it a CD-ROM? `GetDriveType` + MSCDEX check (`1188:687c`), returns 4 | (not needed) | confirmed |
| `1188:6718` | volume label: `SetErrorMode`, search `X:\*.*` with the label attribute | (not needed) | confirmed |
| `1190:a37e` | palette manager (vtable `1190:b0b2`, global pointer `DS:033E`): registered palette at +0x1E, "on sync" transition at +0x322 | `video.c` | confirmed |
| `1190:a6dc` | `effectTransition` (vtable +8): types 300, 301, 302 | `video_effect_transition` | **confirmed and corrected** (below) |
| `1190:a9be` | palette fade toward a destination palette | `fade_to` | **confirmed and corrected** |
| `1190:addc` | sets the registered palette (type 302) | case 302 | confirmed |
| `1190:aec2` | clamps the range: `start <= 255`, `start + count <= 256` | `limit_range` | confirmed |
| `1190:a5d4` | `setTransitionOnSync`: copies the arguments (+0x322 number, +0x324 values) | `video_set_transition_on_sync` | confirmed |
| `1190:af3c` -> `1190:a6b6` | after the screen update, runs the "on sync" transition (vtable +8) | `video_do_transition_on_sync` | confirmed |
| `1188:4c60` | `AnimatePalette` | `video_set_palette` | found, not compared |
| `1188:4a02`, `1188:4d2c` | `SetPaletteEntries` + `RealizePalette` | `video_set_palette` | found, not compared |
| `1188:4ade`, `1188:4b52` | `SetSystemPaletteUse` (static/non-static palette at startup and exit) | — | found |
| `1188:32fa`, `1188:3608` | copy to screen with `StretchDIBits`/`SetDIBitsToDevice` (or WinG, `BlitMethod` in `imtplay.ini`) | `video_present` | found |
| `1188:25a8` | `waveOutWrite` | `audio.c` | found |
| `11a0:0a86`...`11a0:0b16` | `LoadCursor`/`SetCursor` | `cursors.c` | found |
| `10a0:0000` | image blit: chooses the routine by compression (1->RLE, 5/6->CCC, 7->transparent), clipping and dissolve | `rt_draw_bitmap` | **confirmed and corrected** (below) |
| `10b8:030e`, `10b8:03c0` | choice of the dissolve pattern and loop over the dirty rectangles | `dissolve_index` | **confirmed and corrected** |
| `10b8:0ecc` | dissolve of an RLE8 image | `dissolve_rle` (video.c) | **reproduced** |
| `10b8:160e` | dissolve of an uncompressed image | `dissolve_raw` (video.c) | **reproduced** |
| `10b8:1472` | transparent blit (color 0 skipped) | `blit` | confirmed |
| `1198:de2c` | reads the expression type and evaluates it | `eval` (script.c) | confirmed |
| `1198:de5e` | evaluates: 0x65 variable (`1198:defa`), 0x66 value (`1198:df20`), 0x67 operation (`1198:e158`), otherwise -> exception | `eval` | confirmed |
| `1198:e158` | switch on opcodes 0xc8-0xdf (table `1198:e280`) | `eval_operation` | see differences |
| `1198:e2b4` / `e31a` / `e3e0` | if / if-else / assignment | `OP_IF`... | see differences |
| `1198:e454` | binary operators 0xcc-0xd9 (a single handler) | `OP_OR`...`OP_MODULO` | see differences |
| `1198:e602`, `e642`, `e72e`, `e87c`, `e93c`, `e960` | negation, function call, method call, local variables, return, return without value | same | found, not compared |
| `1190:4a68` / `1190:4b88` | constructor / destructor of a value (24 bytes; type at +0: 0 empty, 1 float, 2 bool, 3 time, 4-6 others) | `MsValue` | partly confirmed |
| `1190:513c` -> `1190:516a` | comparisons (`==` 0, `<` 2, ...) | `val_compare` (value.c) | see differences |
| `1190:5760` -> `588c`/`5926`/`5acc`/`5b54`/`5bf0` | arithmetic (+ 6, - 7, * 8, / 9, mod 10) for float∘float, time∘float, float∘time, time∘time, type 6∘type 6 | `val_math` (value.c) | see differences |
| `1190:4ee8`, `4f36`, `4f84` | or / xor / and | `OP_OR`... | see differences |

Not yet found: the event queue (`"<event queue: "` at `1198:1ef2`) and the palette transition
loop. The actor type table is in "Actor factory".

## Partial dissolves (confirmed, corrected in the engine)

Data in DGROUP (file `0x51080` + offset):

- `DS:03C0`: 12 tables of 25x25 bytes of 0/1, 626 bytes apart (625 + one 0 byte);
- `DS:2118`: a 24x24 checkerboard;
- `DS:2358`: 25 descriptors of 8 bytes `{size, threshold, offset, segment}`: 0-11 the tables
  with threshold 1, 12-23 the same in reverse order with threshold 0 (the complement), 24 the
  checkerboard.

The bytes match `dissolvepatterns.h` from ScummVM. The engine reads them from `LIONKING.EXE` at
startup (`engine\dissolve_tables.h`, `video_load_dissolve_tables`): the repository contains no
bytes of the tables; without the executable the old Bayer matrix is used.

Code (`10a0:0035`, `10b8:0314`, `10b8:0ecc`, `10b8:160e`):

1. percentage `p = _ftol(f * 100.0 + 0.5)` (constants at `DS:2FC6`); `p == 0` draws
   nothing, `p == 100` is a normal blit (before, we used `f <= 0` / `f >= 1`);
2. pattern: `p == 50` -> 24 (checkerboard), otherwise `clamp((p + 2) / 4, 1, 24) - 1`.
   **ScummVM does not subtract 1** and always takes the next, denser pattern (25% -> 28% of the
   pixels instead of 24%);
3. uncompressed images: the pattern starts from the clipped corner *relative to the image*; the
   column returns to 0 at the pattern size but is **not reset at every row** (it continues from
   the previous row); source color 0 is never drawn, even for opaque images;
4. RLE images: the pattern is aligned to the *screen*; every RLE run restarts from column
   `screen_x % size` (even if the first pixels are clipped) and the column returns to 0 only
   after exceeding the size (comparison `>` instead of `>=`: the "size" column is
   read once per period, i.e. the first byte of the next row or the byte after the table).
   Copies from the key frame (0,2) are skipped like transparent areas.

ScummVM uses screen coordinates for both cases and resets the column at every row.
The Lion King, in its scripts, never calls `setDissolve` (no use in `msdump scripts`):
the fix changes no screen of this title, but makes the engine exact.
Color 0 in RLE runs is not checked, neither here nor in the normal blit: we now
draw it too (see "Color 0 in RLE images").

## Palette transitions (confirmed, corrected in the engine)

- The Lion King executable handles **only types 300, 301 and 302** (`1190:a6dc`): the others
  (303-328) do nothing. The game scripts use only 300 and 301 (FadeToBlack 169 + 1
  "on sync", FadeToPalette 4 + 27 "on sync"). Our engine also implements 303-310 (from
  ScummVM, later titles): they stay, they do no harm.
- Arguments: `(type [, duration [, start, count]])`; default duration 0.5 s (`DS:2F12`),
  start 1 and count 254 if not all four are present (302: if there are not three arguments).
  Start and count go through `_ftol` (truncated). Same as ours.
- Maximum duration **5 s** (`DS:2F0A`, copied into the manager by the constructor `1190:a37e`): a
  longer duration is shortened. Added (`MAX_FADE_TIME`).
- Loop (`1190:a9be`): `end = now + duration`; while `now < end`:
  `k = _ftol(1024.0 * elapsed / duration)` (maximum 1024), each component
  `src + (dst - src) * k / 1024` with truncated integer division, clamped to 0..255; then it sets
  the palette (vtable +0x1C; if it returns true the loop stops). No wait between
  steps. At the end it sets the exact destination palette. Before, we used floating-point
  interpolation and, with duration <= 0, a 0.1 s fade: now duration <= 0 sets the
  destination immediately, like the original. We keep 5 ms between steps (invisible, avoids 100% CPU).
- **Every `EffectTransition` cancels the pending "on sync" transition** (`1190:a6dc` always ends
  with `1190:a680`, which frees the saved arguments), even the one just run by the
  sync. Before, our engine kept it and ran it at the next update. Fixed
  (`clear_on_sync` in `video.c`).
- The time comes from a global clock object (`DS:0230`, method +4) based on `1188:1332`.

## Event loop (confirmed; one point corrected)

`1188:fb70` is the engine event loop (the equivalent of ScummVM's `EventLoop::run`).
It works with three global objects:

- `DS:0242` event queue (`1188:fad2`): two lists (+0 incoming, +0x1A being dispatched),
  counter of dispatched events at +0x38; `1188:fb48` puts an event in the queue;
- `DS:023A` platform: it is sub-object +4 of the application, vtable `1188:1ab8`
  (+4 `11c4` is there a Windows message?, +8 `11e4` dispatches messages, +0x0C `1260` modal loop, +0x10 `12ec` stops it, +0x14 `1302` timers, +0x18 `f6b2` polls the services, +0x20 `1310` OnIdle);
- `DS:023E` dispatcher (`1188:f886`): list of handlers with a class mask; the event goes
  to the **first** handler (from the last registered) whose mask contains the class.
  Classes found: 0x02/0x04 mouse and keyboard (`1190:9d66`, subtypes 6, 7, 8, 0xB, 0xC),
  0x08 screen (`1190:8de8`), 0x10 timer (`1190:02aa`, subtype 4), 0x40 (`1190:2916`).

Each iteration of `1188:fb70`:

1. **timers** (`1188:1302` -> `1190:01a0`): timers are in a list sorted by expiry; each
   timer with `expiry <= now` queues an event (class 0x10), nearest first; the
   first one not expired stops the scan. The time is a suspendable clock in seconds
   (`1190:05a4`, `timeGetTime()*0.001`): 1 ms granularity;
2. if the queue is not empty: the whole queue moves to the dispatch list and each event goes
   to the dispatcher; events created during dispatch wait for the next iteration;
3. if there is a Windows message (`PeekMessage`): messages are dispatched until a new
   engine event is queued (at most one input per iteration); mouse and keys become
   events (class 2) dispatched at the next iteration; the end of sounds arrives here (IMTWIN.DLL);
4. the registered services are polled (`1188:f6b2`, result 0/1/2);
5. if something happened (messages, events, or a service answered 0) and no service
   answered 2: a "screen" event (class 8, subtype 2) is dispatched immediately -> `1190:8e2e`:
   if automatic update is on it redraws the dirty areas (`1190:a0e8`, everything with
   `1190:a10a` after a reactivation), copies to the screen (`1190:a12c`) and runs the
   "on sync" transition (`1190:af3c`); otherwise it calls MFC's OnIdle.

Subtype 3 of class 8 with the right token re-enables automatic update and
forces a full redraw: like `rt_enable_auto_update`.

Comparison with `rt_run`: same order (alarms -> dispatch -> input -> movies/sounds -> screen),
same "new events wait for the next iteration" rule. Difference corrected: we queued
expired alarms in actor id order; now in order of expiry
(`queue_expired_alarms`, ties broken by id). Not yet reproduced: at most one
input per iteration (we read all SDL events), but inputs are queued anyway and
the order among them does not change.

## Color 0 in RLE images (confirmed, corrected in the engine)

The normal blit of an RLE image is `10a0:0000` -> `1188:40be`: for each dirty rectangle that
intersects the image it calls `10e8:09ee`, which prepares the clipping and jumps to `1118:0024`
(a 32-bit assembly routine: in 16-bit mode it reads badly, but the structure is visible).
Repeated runs are written with `rep stosd/stosb` and literal ones with `rep movsw/movsb`,
**without checking color 0**: transparency comes only from the skips (0,3), the copies (0,2)
and what is left of the row after an end of row. The same holds for the RLE dissolve
(`10b8:0ecc`).

Corrected: `blit_rle` (video.c) draws RLE directly from the compressed data with color 0
opaque; `dissolve_rle` now draws it too. Images decompressed at load time stay transparent at
color 0 (`decompress_in_place`, saved as uncompressed with transparent color 0)
as do the movies' delta frames (`rt_draw_delta`): not verified against the original.
In the test screenshots (113, 206, 210, 225) no pixel changes: The Lion King's data
does not seem to have 0 pixels in its runs.

## Actor factory (found)

There is no switch: the type -> creation function map is filled at startup by `1000:0000`
(segment 1), which calls `1190:481c(map [DS:0322]+0x38, type, far pointer)` 11 times.
Each creation function does `new(size)` (`1120:1446`) and calls the constructor, which
sets the main vtable (+0) and a secondary one (+4, events/timers interface).
Next to it there is a method `mov ax, type; retf 4` (vtable +8: actor type).

| Type | Class | Creation | Size | Constructor | Vtable | Base |
|---|---|---|---|---|---|---|
| 0x01 | Screen | `1190:cc7c` | 0x1e | `1190:ccac` | `1190:ce4c` | Actor |
| 0x04 | Path | `10d8:0234` | 0x4e | `10d8:0000` | `11a0:0274` | Actor |
| 0x05 | Sound | `1190:ce70` | 0x44 | `1190:cea0` | `1190:d694` | Actor |
| 0x06 | Timer | `1190:d6d4` | 0x3c | `1190:d704` | `1190:de2c` (+4: `de4c`) | Actor |
| 0x07 | Image | `1190:c6da` | 0x54 | `1190:c64c` | `1190:cc0c` | Spatial |
| 0x0b | Hotspot | `1190:bf16` | 0x38 | `1190:bf46` | `1190:c5e8` | Spatial |
| 0x0c | Cursor | `1190:b13c` | 0x1e | `1190:b0d6` | `1190:b212` | Actor |
| 0x0e | Sprite | `1068:027e` | 0x82 | `1068:0000` | `1198:0000` (timer `1198:0070`) | Spatial |
| 0x0f | Stalking Zazu | `1188:6ca0` | 0x9a | `1188:6986` | `1188:708a` | Spatial |
| 0x10 | Dot game | `1190:b2b8` | 0x4c | `1190:b236` | `1190:beb6` | Spatial |
| 0x16 | Movie | `1190:e1a8` | 0x10c | `1190:de58` | `1190:fec6` | Spatial |

No other type is registered: a different type gives `EX_ActorMapBadActorClassId`. The Lion King
data uses 1, 5, 6, 7, 0x0b, 0x0e, 0x0f, 0x10, 0x16 (`msdump load`); Path and Cursor are
registered but do not appear as actors. Our engine additionally has Stage, Document, Palette,
Text, Font, ...: they come from ScummVM and stay.

In the file, the segment words of these `mov word ptr es:[bx+2], ...` are links in
relocation chains: the vtable offset is correct, the segment is read with `ne_tool.py refs`.

Hierarchy: Actor `1190:25d0` (id at +0x14, "load complete" at +0x1a, set to 1 by
`1190:27e8` = vtable +0x18) -> spatial entity `1190:92d8` (vtable `990e`, bounding box at +0x1c,
z at +0x24, parent at +0x26) -> `1190:996e` (vtable `9bd0`, event mask at +0x2a).
The stage (container) has vtable `1198:a27c`, constructor at `1198:97f4`, children at +0x2a sorted
by the comparison `1198:9784` (z at +0x24, ties by address).

Common vtable entries of the spatial actors: +0x08 type, +0x10 parameter read, +0x18 load
complete, +0x20 visible, +0x28 draw, +0x2c mouse target lookup, +0x30 keyboard target lookup,
+0x34 MouseDown, +0x38 MouseUp, +0x3c MouseEntered, +0x40 MouseExited, +0x44 MouseMoved, +0x48
out of focus, +0x4c KeyDown, +0x54 invalidate, +0x5c invalidate a rectangle in the parent.
Tool: `re\ne_tool.py dis` (capstone) for the code that Ghidra did not disassemble (reached only
through vtables and pointers).

## Hotspots and mouse (verified; two corrections)

Mouse dispatcher `1190:9d66`/`9dc8`: for each event it asks the stage (vtable +0x2c,
`1198:9eb8` -> `1198:9f52`) for the targets with a mask (1 down, 2 up, 4 moved, 8 entered,
0x10 exited, 0x40 out of focus; keyboard 0x80 with +0x30). The stage scans the children and removes from the
mask the bits already taken; then: down (6) -> +0x34, up (7) -> +0x38, moved (8) -> exit/enter
(`1190:a002`: if entered != exited, first MouseExited (10) to the exited one, then MouseEntered (9)
to the new one) and +0x44; subtype 0xb: only exit/enter; 0xc: exit and out of focus.

Hotspot (`1190:c264`, vtable +0x2c): no target if it is not active (+0x32) **or if its
loading is not complete** (+0x1a). If the point is inside (`1190:c00c`: bounding box, then the
polygon relative to the corner of the box if present) it takes down/entered/moved; it is a target for
exited if it is the hotspot the mouse entered (`DS:0342`) and for up if it is the one
pressed (`DS:0346`), even with the mouse outside. MouseDown sets `DS:0346`, MouseUp clears it,
Entered sets `DS:0342` and the cursor (+0x30, otherwise the default one), Exited clears it;
MouseMoved only if active. All run the response with an empty argument (`1190:3766`).
Everything as in `runtime.c`, except the loading check (added).

Keyboard (`1190:c490`): active, loaded, and a KeyDown response with an argument **exactly**
equal to the key code (`1190:36d0` -> `3630` -> `345c`: equal event and equal value); then
`1190:c520` runs it with the code as a float. The "any key" response (code
0) that our engine tried as a second attempt does not exist: removed (The Lion King has none: KeyDown
responses 32, 76, 108, 256, 257, 259).

Order: the stage scans the children from the first (`1198:f7f0`) for both mouse and keyboard;
our engine looks for the mouse from the nearest (lowest z) and the keyboard in drawing order
(like ScummVM). The sort direction of the original's list is not yet certain (it depends on
the argument order with which the sorted list calls `1198:9784`): unchanged, to be
confirmed with the stage's draw loop.

## Sprites: frame timing (verified; one correction)

The sprite has a timer object at +0x3c (vtable `1198:0070`, callback `1068:0dc6`), period at
+0x44 = `1 / frame rate` (double, `1068:03c4`), next instant at +0x4c, current clip
[+0x6a, +0x6c], current frame +0x76, state +0x80 (1 stopped, 2 playing).

- `timePlay` (`1068:0f50`): state 2 and scheduling (`1068:0e52`): if the current frame is
  already the last of the clip it stops immediately, otherwise `1068:0e82`.
- `1068:0e82`: next += period; if it is already past it becomes "now"; the timer fires after
  `next - now`. So no skipped frames and no catching up on delays.
- Callback: advances one frame (`1068:0ba2`, forward only); if it has reached the last one it
  queues SpriteMovieEnd (0x17) with the clip id, then the scheduling stops it.
- `timeStop` (`1068:0f6c`) only removes the timer. **The next instant is never reset**
  (it starts at 0 in the constructor): the first `timePlay` after a pause immediately shows the next
  frame, not after one period. Our engine reset it to "now" on every play and to 0 on
  stop: corrected (`sprite_schedule_timer`). All 57 sprites of the game have an
  integer frame rate (54 at 10, 2 at 4, 1 at 5): the integer computation in milliseconds gives the same timing.
- Backward clips (first > last): the original does not handle them (it advances only if current <
  last); our engine handles them like ScummVM. Unchanged.

## Timers (structure verified)

Callback `1190:dcb8` (secondary vtable `1190:de4c` +8): looks for the first time response after
the one executed (`1190:3702`, event 5), saves its time, removes the timer (`1190:dab6`),
schedules the next one (`1190:d8fe`; if there is none it stops the actor, `1190:da9c`) and **then** runs
the response. One response per firing, the next scheduled before running: like
`timer_alarm`.

## Script interpreter: differences from `engine\script_rt.c` / `value.c`

The original code was compared with the interpreter and the common runtime (`script_rt.c`,
`value.c`, used by both the interpreter and the recompiled C). **Corrected** (20/10/2026): points
4, 5 and 6 — now like the original, with a `??` warning instead of the fatal error (they do not
occur in The Lion King: 28 screens without warnings). Not changed: 1 (extra opcodes, useful for other titles),
2 (the original skips two blocks: an error of the original, we keep the fatal error), 3 (same
effect), 7 (not verified how non-numeric types are converted; we already compare float
and time with each other).

1. **Opcodes 0xe0-0xe2** (`while`, function/method call on a variable): the table of
   `1198:e158` covers only 0xc8-0xdf; the others throw an exception. The Lion King does not use them;
   our interpreter accepts them (they come from later titles, which is fine).
2. **`if` with a non-boolean condition** (`1198:e2b4`): the original skips the block *twice*
   (it skips the block and also what follows: probably an error of the original); `if-else` with
   a non-boolean condition throws an exception. Ours: fatal error in both cases.
3. **`if-else` true**: the original runs the block and always skips the else, even after a
   `return` (we skip only if there was no return: same practical effect).
4. **or/xor/and** (`1190:4ee8`...): a non-boolean operand is *false*, no error
   (ours: fatal error). The result is written only if the destination is still empty.
5. **Division and modulo by zero** (`1190:588c`): result 0.0 (constant `DS:2F02`), no
   error (ours: fatal error). Modulo uses `fmod`.
6. **Arithmetic on unexpected types** (`1190:5760`): empty result, no error
   (ours: fatal error). Allowed combinations: float∘float, float∘time, time∘float,
   time∘time and type 6∘type 6 (maybe strings: to be verified). With time∘time the result
   of + and - goes through `1190:4c9e` (not `4c5c` as for floats): perhaps a *time*, not a float
   as `val_math` does (to be verified).
7. **Comparisons between different types** (`1190:516a`): if the types differ they are compared as
   floats (case 1), without a warning; we warn and compare according to the type of the first.
8. Operand order: both evaluated first (`1198:e454`), then `a op b` with `a` the first one
   read: subtraction `a - b`, division `a / b` (like ours).

## To do

- Sort direction of the stage's children (see "Hotspots and mouse") and drawing order.
- Movies: frame timing and audio sync (Movie class, vtable `1190:fec6`,
  constructor `1190:de58`): not yet looked at.
- Services polled by `1188:f6b2` (who answers 0/1/2: streams, movies, audio?).
- The transition "abort" function (vtable +0x1C of the manager returns true): when?
- Color 0 in images decompressed at load time and in the movies' delta frames.
- Jump tables not resolved by Ghidra (such as `1198:e280`) are read with `re\ne_tool.py jt`.

## Done afterwards

- `engine\main.c`: the dissolve tables are read right after the cursors, from the same
  `--exe`; the temporary call from `rt_run` was removed.
