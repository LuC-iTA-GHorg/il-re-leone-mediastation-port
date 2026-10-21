# The minigames in the original executable

Function-by-function comparison of the two native minigame classes of `LIONKING.EXE` with
`engine\actors_zazu.c` (Stalking Zazu, type 0x0f) and `engine\actors_dotgame.c` (constellations,
type 0x10). The factory and the vtables are in [ORIGINAL.md](ORIGINAL.md). The port's starting
point was ScummVM (`minigames/stalkingzazu.cpp`, `minigames/dotgame.cpp`): the differences found
are listed below and fixed in the engine (comments with the address in the code).

## Constellations (Dot game, `1190:b236`, vtable `1190:beb6`)

Fields: +0x30 number of stars, +0x32 star vector, +0x36 click hotspot, +0x38 sprite of the star
to reach, +0x3a sprite that runs along the line, +0x3c current star, +0x3e active (it is also
the answer to "visible", vtable +0x20 = `1190:b518`), +0x40 speed, +0x42 sprite position, +0x46
progress, +0x48 thickness, +0x49..+0x4b color. Initial values (`1190:b236`): speed 1, progress
100, thickness 3, color white.

| Address | What | Result |
|---|---|---|
| `1190:b328` | parameters 0x1d (stars), 0x3eb, 0x514–0x519 | same |
| `1190:b4c8` | load complete: if active, clears the flag and calls its own method 0xe4, which at that point does nothing | same (stays inactive, helpers untouched) |
| `1190:b6f6` | methods 0xe2 reset (only with 1 argument), 0xe3 activate, 0xe4 deactivate, 0xe5 hit, others → `1190:93a4` | same |
| `1190:be36` | reset: `_ftol` of the number (0 if it is not a float); **ignored if < 0 or ≥ number of stars** | **fixed**: previously clamped to the range and recomputed the color |
| `1190:b944` / `1190:ba1e` | helpers: 0xd2 on the hotspot, 0xca + 0xce on the star (and 0xd3, 0xcb, 0xcf) | same |
| `1190:b794` | helpers centered on the next star (0 after the last), method 0xe6 | same |
| `1190:baf8` | hit: advances, running sprite (0xca, then 0xe6 + 0xdd(true) at each step, redraw with `1190:8e76`), 0xcb; if `total − current == 1` own method 0xe4, current 0, event 0x18 | same (32-bit truncated integer computation) |
| `1190:b526` | drawing of the segments | same point order |
| `1188:43d6` | **drawing of a line** | **fixed** (below) |

**Lines** (`1188:43d6`): the function receives thickness and color but does not read them (cdecl
convention, the extra arguments stay on the stack). It always draws with **palette index 1**,
**two pixels** thick, directly into the back buffer and without clipping: Bresenham from the
leftmost point (on equal x the second point), with the pixel to the left for steep lines and
the one above for flat lines; a vertical line covers |dy| pixels from the starting point, end
point excluded; a single point draws nothing. The port used to search the palette for the
color closest to the actor's RGB and draw a 3×3 square per pixel.

## Stalking Zazu (`1188:6986`, vtable `1188:708a`)

Main fields: +0x34 all the rocks, +0x3c rocks in play, +0x44 grid, +0x48 timer, +0x4c period
(default 0.1 s, `DS:2E4E`), +0x54 Simba, +0x56 clip per (direction, crouched), +0x5e Simba's
position, +0x62/+0x64 direction and crouched clip shown, +0x66/+0x68 sounds, +0x6a audio, +0x6c
step (10), +0x72 goal, +0x76 victory radius (20), +0x78 rectangle in which Simba's center can
be, +0x80 timer machine, +0x86 game machine, +0x8c Zazu is looking, +0x8e active, +0x90 "only
Simba takes the mouse", +0x92 cursor, +0x94/+0x96 rocks to show at load and "parameter
present", +0x98 "crouched" event already sent.

### Parameters (`1188:6cd2`)

0x22 cursor, 0x24 frame rate (period = 1/fps), 0x3eb active, 0x3ec Simba, 0x3ed five triples
(direction, standing clip, crouched clip), 0x3ee rows and columns (the grid covers the bounding
box read earlier; goal = (width/2, 0)), 0x3ef a rock, 0x3f0 step, 0x3f1 radius, **0x3f2 → +0x66
(step sound), 0x3f3 → +0x68 (crouching/standing-up sound)**, 0x3f4 audio, 0x3f5 rocks to show
(+ flag), 0x3f6 a point (+0x6e) and then back to the base class.

### The two state machines (`1188:8bd6`, `1188:8e22`, executor `1188:c448`)

[state][event] tables with -2 = "event ignored": the executor (`1188:c690`) **skips them
silently**; -1 (never used here) would call the error handler that throws an exception
(`1188:8a5c`, `1188:8962`). Hence the two warnings the port printed ("timer machine: event 0 in
state 0" at activation, "game machine: event 3 in state 5" after the defeat) have no
counterpart in the original: removed.

Game machine (states start, reset, crouched, can move, moving, defeat, victory; events reset,
crouch, stand up, move, lose, win): from start/crouched/defeat/victory only reset (and stand up
from crouched); **from reset only crouch** (a second reset is ignored: the port accepted it);
from "can move" and "moving" everything except stand up.

Entry handlers (table `DS:018C` filled by `1188:8cbe`):

| State | Address | What it does | Differences fixed |
|---|---|---|---|
| reset | `1188:8704` | `1188:7dc8(argument)`, Zazu stops looking, +0x90 = 1, +0x98 = 0 | the argument travels with the event |
| crouched | `1188:8740` | the first time: mouse to be rechecked + event 0x1a; crouched clip; **sound +0x68**; +0x90 = 0 | wrong sound (was +0x66) |
| can move | `1188:87de` | standing clip; **sound +0x68**; if Zazu is looking and Simba is not behind a rock → lose, **otherwise** timer; +0x90 = 0 | the port started the timer even after the defeat (hence the "event 3 in state 5" warning) |
| moving | `1188:8852` | looking → lose; within the radius → win; otherwise one step toward the mouse, **sound +0x66**, timer | wrong sound |
| defeat / victory | `1188:88ee` / `1188:8928` | event 0x19 / 0x18 | same |

Timer machine (states expired, queued, expired-inactive, expired-active, queued-inactive; events
activate, deactivate, expire, queue): same as the port; handlers `1188:8a12` (expired: "move"
event to the game), `1188:8a30` (queued: timer of one period), the others empty.

### Rocks (`1188:7dc8`)

With argument ≥ 0: `1188:7eac` chooses min(n, total) rocks, `1188:7e3e` empties the grid and
`1188:9fe6` arranges them; then (always) it hides all the rocks (`1188:7f40`), shows those in
the grid at the center of the cell (`1188:7fba`), puts Simba back at bottom center + (3, 0),
facing north and crouched (`1188:807e`), and rechecks the mouse.

- **Choice** (`1188:7eac`, new): n − k rocks are removed at step `n / (n − k)` from a random
  starting point; the positions refer to the whole list while the list shrinks: past the end
  the original removes the first one (invalid iterator, `1188:9ae4`). The port used to show all
  of them with a warning.
- **Arrangement** (`1188:9fe6` + `1188:a0e2`): for each inner row a random limit
  `random % (rocks + 1)`, sorted; rock k goes into the current row while k < limit, moving on to
  the next row after each limit or when the rock does not fit (retried in the next row).
- **One row** (`1188:a130`): free cells from left to right, one chosen at random; if it is not
  acceptable it is removed **preserving the order** of the others (`1188:a7ec`); ScummVM
  replaced it with the last one, which with the same random number picks a different cell.
  Fixed.
- **Constraints** (`1188:a2b6`): no edges, no rock directly above or below another, following
  the chains of neighboring rocks: same.
- Random numbers: three calls to `1188:d6c2` (`1188:7ee9`, `1188:a0f5`, `1188:a163`), always
  followed by the 32-bit unsigned remainder (`1180:0e6a`). This is the same generator as the
  scripts' `Random` function: `state = (state × 4096 + 150889) mod 714025`. The port uses
  `script_random() % n` (previously `rt_random`, a separate xorshift).

### Geometry

| Address | What | Difference fixed |
|---|---|---|
| `1188:9f8c` | cell size = W / columns, H / rows (unsigned integers) | — |
| `1188:9dc8` | cell center: `_ftol((col+1)·W/columns − cell_width·0.5)` | previously `col·width + width/2` (different if W is not a multiple) |
| `1188:9eb2` | start: (W/2, H − cell_height/2) with integer halves | previously floating-point halves truncated afterwards (one pixel of difference with odd height) |
| `1188:b31e` | cell of a point: `_ftol(rows/H·y)`, `_ftol(columns/W·x)`; an out-of-range index (even negative, unsigned comparison) becomes the last one | previously negatives stayed negative |
| `1188:8482` | z of the moved actor = bottom of the minigame's bounding box + minigame's z − y | **previously minigame's z − y**: Simba and the rocks had z 3000−y instead of 3454−y, hence in front of movie 904 (z 3020) instead of behind it |
| `1188:722a` | Simba's rectangle: the bounding box narrowed by half of Simba's width on each side | same |
| `1190:6c8c` | step limit in the rectangle: **x ≤ right − 1, y ≤ bottom − 1** | previously right and bottom inclusive |
| `1188:7684` | the step is free if the 4 corners of the rectangle (x − w/4, y − h/4, **w/2, h/4**) clipped to Simba's rectangle fall into free cells, **with right and bottom sides inclusive** | previously the rectangle was h/2 tall (up to y + h/4) and the right/bottom corners were shifted by −1 |
| `1188:79e8` | step: `_ftol` (truncated) components of step·v/|v|, never downward, not beyond the mouse | **previously rounded** (`round`) |
| `1188:7b88` | direction of the step: angle = atan2·57.29577950560105 (`DS:2EA6`, slightly less than 180/π) into 6 intervals | with the mouse exactly below Simba the angle is 89.999… → east (ScummVM: west) |
| `1188:8602` | behind a rock: rock in the cell to the north | same |
| `1188:8586` | victory: distance from the goal ≤ radius | same |

### Methods (`1188:70f2`, jump table `1188:71b5`)

- 0xdf/0xe0 Zazu looks/stops (`1188:735c`): only when it starts looking does it check Simba.
- 0xe1 gaze direction (`1188:7cc2`): **vector from the goal to Simba** (Simba − goal), west if
  the angle is in [−180, −90) or [90, 180), otherwise east. The port (like ScummVM) used goal −
  Simba and gave the opposite direction: with Simba to the right of the goal Zazu now looks
  right (`token 1104`), previously left.
- 0xe2 reset (`1188:713b`): the argument goes through `_ftol` (0 if it is not a float) without
  checking the number of arguments, and travels with the event. The value of parameter 0x3f5 is
  not touched.
- 0xe3/0xe4 activate/deactivate (`1188:7398`).
- **0xe7 disables the audio** and stops the two sounds, **0xe8 re-enables it** (`1188:72d8`):
  the port had them swapped (names in `constants.h`, shared code: reported). The Lion King does
  not use them.
- Load complete (`1188:722a`): reset with +0x94 **if parameter 0x3f5 is present** (previously:
  if the value is different from 0).

### Mouse (`1188:7414`, `1188:74c2`, `1188:750e`, `1188:7532`, `1188:7556`, `1188:7594`)

Same as the port: with +0x90 = 1 only Simba's box takes the mouse, otherwise the whole screen;
down = crouch, up = stand up; the cursor +0x92 on entry.

## To report (shared code, not changed)

- `constants.h`: `M_ZAZU_ENABLE_AUDIO` (0xe7) and `M_ZAZU_DISABLE_AUDIO` (0xe8) have their names
  swapped relative to the executable (the code of `actors_zazu.c` uses the numbers).
- `rt_draw_thick_line` and `rt_find_color` (`video.c`) are no longer used by the constellations
  minigame (which draws pixel by pixel with `rt_draw_thick_line(x, y, x, y, 1, 1)`).
