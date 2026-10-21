# LIONKING.EXE: event loop, input, timers, audio

Results of the comparison with the original executable (2026-10-21, "events" agent). Addresses
use Ghidra notation (`segment:offset`, see [ORIGINAL.md](ORIGINAL.md)). In the code, every
behavior taken from the original carries its address in a comment.

## Event loop (`1188:fb70`)

Each iteration, on the platform object `DS:023A` (vtable `1188:1ab8`):

| Step | Call | What it does |
|---|---|---|
| 1 | +0x14 `1188:1302` → `1190:01a0` | expired timers → queued events (class 0x10) |
| 2 | `1188:fd14` (if the queue is not empty) | the whole queue goes to the dispatcher; new events wait for the next iteration |
| 3 | +4 `1188:11c4` PeekMessage, +8 `1188:11e4` | Windows messages until one of them queues an engine event (queue counter +0x38): **at most one input per iteration** |
| 4 | +0x18 `1188:f6b2` | registered services: result 0 (something changed), 1 (nothing), 2 (do not update the screen) |
| 5 | `1188:fd9c` | if steps 2/3 did something or a service answered 0, and none answered 2: screen event (class 8 subtype 2, `1190:8e2e`); otherwise MFC OnIdle (+0x20) with a counter |
| 6 | +0x1c `1188:12fe` | empty (`retf 4`) |
| 7 | if queue +0x3c = 1 | modal message loop (+0x0C `1188:1260`) until `1188:fcd0(0)` stops it |

- **Services**: only movies register (`1198:1f36` adds, `1198:1f62` removes, called by the movie
  in `1190:e0d5`, `f4aa`, `f666`, `f798`); the service is `1190:fbc4` (vtable `1190:ff2a` +4 of
  the movie's sub-object +0x34). It returns 2 when the next frame has already expired (the
  screen update is skipped), 1 if the movie has not changed, 0 otherwise. In the port
  `pre_display_sync` only returns "force the update": case 2 is to be carried over into the
  movie code (gfx agent).
- **Event classes** (`1188:f98e(event, subtype, class)`): 2 mouse (`1190:9090`), 4 keyboard
  (subtype 0xd), 8 screen, 0x10 timer (`1198:2368`, subtype 4), 0x20 timer call to the client
  (`1198:25a4`, subtype 5, does not go through the queue), **0x40 event for an actor**
  (`1190:29e2`: subtype = event number, actor id, value; handler `1190:2916` looks the actor up
  in the map `DS:0322` and calls its vtable +0x1c). This is our `Q_ACTOR`.
- The original never sleeps. In idle iterations the port now waits only until the next timer
  (at most 10 ms; previously a fixed 10 ms, i.e. timers late by up to 10 ms).

Fixed in the port (`runtime.c`): a single input per iteration (`pump_input`), keyboard queued,
wait until the next timer.

## Clock and timers

- Clock `DS:025A`: `1190:05a4` = `timeGetTime()*0.001 − base` (1 ms granularity); when paused it
  returns the instant of the pause. Pause `1190:04a2` (only if not already paused), resume
  `1190:04fa` (the base advances by the time spent paused). Added to the port (`clock_pause`).
- Timer list: entries of 0x18 bytes {expiry (double), period (double), client, serial number,
  1}. `1190:008e` registers: removes the client's entry (`1190:010a`), expiry = now + period,
  sorted insertion (`1198:2e6a` with `1188:fe82` → `1198:21e0`): **by expiry, ties broken by
  serial number** (global counter `DS:03AC`, `1198:204a`, incremented on every registration).
  The port used to break ties by actor id: it now uses registration order (`alarm_serial`,
  `actor.c`).
- `1190:01a0`: starting from the first entry, every entry with expiry ≤ now queues an event with
  the serial number; the first entry not expired stops the scan. The entry stays in the list
  until the event is dispatched (same iteration, step 2): `1190:02da` looks the entry up by
  serial number, calls the client (`1190:03a2`, vtable +4) and then `1190:03f6`: if the client
  is periodic (+8 ≠ 0) the expiry advances by one period until it exceeds now (no catching up),
  otherwise the entry is removed. The port has no periodic timers (one alarm per actor,
  rescheduled by type).

## Windows messages → events

Message maps at `1008:0000` (main window) and `1008:00a0` (view).

| Message | Handler | Effect |
|---|---|---|
| WM_LBUTTONDOWN, WM_LBUTTONDBLCLK, WM_MBUTTONDOWN, WM_MBUTTONDBLCLK, WM_RBUTTONDOWN, WM_RBUTTONDBLCLK | `1188:09cc`, `099c`, `0a5c`, `0a2c`, `0b8c`, `0b5c` | MouseDown (class 2, subtype 6) at the message position |
| WM_LBUTTONUP, WM_MBUTTONUP, WM_RBUTTONUP | `1188:09fc`, `0a8c`, `0bbc` | MouseUp (7) |
| WM_MOUSEMOVE | `1188:0abc` | only if the position changes; inside the client area MouseMoved (8), outside (the view holds the capture) **a single** "out of focus" (0xc) until the mouse comes back in |
| WM_CHAR | `1188:03d4` | KeyDown (class 4, subtype 0xd) with the character code; with **Ctrl+V (0x16)** it also opens dialog box 1 (version) |
| WM_KEYDOWN | `1188:0474` | arrow keys only: ← 0x100, → 0x101, ↑ 0x102, ↓ 0x103 |
| WM_SYSKEYDOWN/SYSCHAR/SYSDEADCHAR | `1188:17fa`, `17da`, `17ea` | DefWindowProc |
| WM_ACTIVATEAPP | `1188:0260` | see "Loss of focus" |
| WM_SIZE | `1188:051a` | SIZE_RESTORED with the title running → resume (`1188:033c`) |
| `1188:0912` (platform +0x28) | | event 0xb (enter/exit only) at the current position: this is `rt_invalidate_mouse` |

All input goes through `1188:fb48` (queued): **keys too**, which the port used to dispatch
immediately. All three buttons click (the port only used the left one). Every message checks
`IsIconic`.

Fixed in the port: keys queued; WM_CHAR codes from `SDL_TEXTINPUT` (correct upper case and
Italian keyboard, Latin-1) plus the control characters (Backspace 8, Tab 9, Enter 13, Esc 27,
Ctrl+letter 1–26); three buttons; merged moves, and only if the position changes; leaving the
window = one "out of focus" (`SDL_WINDOWEVENT_LEAVE`). To do: the Ctrl+V window (text in dialog
1 of the executable's resources).

## Loss of focus

`1188:0260` (WM_ACTIVATEAPP): on activation system palette (`1188:4ade`) and `SetCapture`; on
deactivation, if the window is not iconic and the title is running (title object `DS:0398`,
vtable +0x24 = field +0x54, set to 1 by `1198:0529` right before the loop `1188:fb70` and to 0
by `1198:05bc`), `1188:02f6`: `waveOutPause` (`1188:219a`), clock pause, event loop in modal
mode (`1188:fcd0(1)`), `ReleaseCapture`, palette, `ShowWindow(SW_SHOWMINNOACTIVE)`: **the game
stops and is minimized**. On restore (`1188:033c`): `waveOutRestart` (`1188:2202`), resume of
the clock and of the loop, a WM_MOUSEMOVE at the cursor position, the cursor manager restores
the cursor (`DS:033A` +4), `SetCapture`.

In the port: `app_deactivate`/`app_reactivate` in `runtime.c`
(`SDL_WINDOWEVENT_FOCUS_LOST`, `RESTORED`/`FOCUS_GAINED`), `audio_device_pause`,
`cursors_refresh`. Disabled in timed tests (`--quit-after` or input from the command line):
they must not depend on window focus. Previously the port only sent an "out of focus" to the
mouse.

## Mouse and keyboard dispatch, order of the stage's children

- The stage's children list (+0x2a) is in **ascending** order of (z, object address):
  comparator `1198:9784` (−1 if z is lower; on equal z the lower address comes first),
  reordering in `1198:9a24`/`9a8d` when z changes.
- Drawing (`1198:9d0e`): from last to first (`1198:f836`) → high z is behind.
- Mouse (`1198:9f52`) and keyboard (`1198:a006`): from the first (`1198:f7f0`) → **frontmost
  first**. The port did this for the mouse but looked the keyboard up in drawing order (from the
  rearmost): fixed (`handle_key`).
- On equal z the address in the heap (SmartHeap) matters: not reproducible; the port uses
  arrival order (same direction for drawing and mouse, see also the gfx document).
- Dispatcher `1190:9d66`: class 2 → `1190:9dc8` (6 down, 7 up, 8 moved, 0xb enter/exit only,
  0xc exit + out of focus), class 4 → `1190:a0a0` (target with +0x30, KeyDown +0x4c). Already
  as in the port.

## Sounds (Sound class, type 5, vtable `1190:d694`)

- Methods (`1190:d348`): **only if the actor is loaded** (+0x1a); `timePlay` (0xce),
  `timeStop` (0xcf), `timePause` (0xd0), `timeResume` (0xd1); all the others go to the base
  `1190:27f8`, which does nothing. The port's `isPlaying`/`isPaused`/`show` come from ScummVM:
  left in place (if The Lion King calls them, the interpreter handles them: to be verified with
  the script agent).
- `timePlay` (`1190:d3a6`): if playing or paused → stop with **abort**; then it requests the
  audio device (2) from the device owner; a streamed sound (+0x32) also requests the "CD
  stream" device (1) and registers with the stream player (`DS:0272`+0x18, `1190:d4d4`, `d4fc`,
  `d572`); then the sequence starts (`1198:a6fa`).
- `timeStop` → `1190:d442(0)`; the owner yielding (`1190:d630`) → `1190:d442(1)`. Only a sound
  that is playing or paused stops; the flag +0x42 means "abort".
- `timePause` (`1190:d46c`): only while playing; the device stays with it. `timeResume`
  (`1190:d4a0`): only while paused, **no arguments**, no new device request. The port used to
  release the device on pause and accept `timeResume(restart)`: removed.
- States (`1190:d20a`): stopped with reason 1 → SoundFailure (0x14), 2 → SoundEnd (0x0e), 3 →
  **SoundAbort (0x13) if abort, otherwise SoundStopped (0x1d)**; the device is returned
  (`1188:eee2`). Playing after stopped → SoundBegin (0x1e); nothing on pause. The port used to
  send SoundStopped when a sound was interrupted by another or restarted: the game's scripts
  have **160 SoundAbort responses and no SoundStopped** (e.g. `busy_blocker` deactivated,
  transitions to the end credits). Fixed.
- Event response (`1190:d30e`, vtable +0x1c): runs the event's response with an empty value. On
  SoundEnd the port first runs the remaining "timed" responses (from ScummVM): not found in the
  original, to be verified (The Lion King seems to have no timed responses on sounds).

### Device owner (`DS:0236`)

Three slots (1 CD stream, 2 audio, 3 ?). `1188:ee8c(type, client)`: if the slot is occupied it
asks the owner to yield (vtable +0); if it yields (0) the new client takes it, otherwise the
request fails. `1188:eee2` frees the slot if the client is the owner. `1188:ef10` asks everyone
to yield (WM_ENDSESSION `1188:01c0`, shutdown `1198:05b4`). Movies request device 2
(`1190:f4ca`) and the stream (`1190:f1a4`). **Exclusivity is always on**: the command "allow
multiple sounds" (0x35) is not read by the executable (not found among the parameter clients)
and The Lion King's BOOT.STM does not contain it; in the port it stays disabled as the default.

### End of sounds

`IMTWIN.DLL` `WAVEPLAYCB` writes `timeGetTime()` into the finished buffer and does a
`PostMessage` to the window: the end arrives as a Windows message in step 3 of the loop, hence
at most one iteration after the real end of the last buffer. In the port `audio_service` checks
finished voices on every iteration (same effect; the delay depends on SDL's buffer, 1024
samples ≈ 23 ms).

`audio_seq_position_ms` (new): samples actually played by the mixer, frozen while paused;
requested by gfx for movie timing, which in the original follows the sound.

## Data layer: parameter clients

Stream control commands go to a list of "clients"; each returns 0 if it read the command, 1
otherwise. Found so far:

| Client | Function | Commands |
|---|---|---|
| document | `1190:2f08` | 0x10 context load complete, 0x2e startup info |
| display | `1190:a482` | 0x578 update dirty, 0x579 update all, 0x57a effect transition, 0x57b set time (into +0x328), 0x5aa palette (0x300 bytes into +0x1e) |
| cursors | `1190:659a` | 0x0c init, 0x15 new cursor, 0x16 dispose |

Still to find: the client of 0x0e–0x14 (contexts, actors, variables), 0x31 (functions), and
what happens with a command that nobody reads (probably the exception
`EX_ChunkParserStateCantHappen`).

## Document (second pass, confirmed and corrected)

The context loader is the object `DS:031A` (constructor `1190:2d40`, parameter client
`1190:2f08`, stream client `1190:2e68`): +0x10 incoming screen, +0x12 current screen, +0x14
context being read, +0x16 initial screen, +0x18 startup stream, +0x1a stream being read. The
scripts' document actor is a different class (`1198:8038`, methods `1198:7f92`):

- `branchToScreen` (0xc9, `1198:7ec4`): nothing without arguments or if the first is not an
  actor; otherwise a ScreenBranch event (0x12) queued to the document with "disable updating"
  (1, or 0 if the second argument is the token 0x708). `quit` (0xd9): the loop stops at the end
  of the iteration (`1188:fd04`). **No other method** (loadContext, releaseContext,
  contextIsLoaded, isLoading, multiple sounds/streams: later engines, removed from the port).
- ScreenBranch (`1198:7f40`): ignored if a context is being read; `1190:3112`: ignored if
  another switch is in progress or if the screen has no context (no error: the port raised a
  fatal error); then incoming screen, observer `DS:031E` (`1190:a1f8`: disables automatic
  updating with a token), ScreenExit **immediately** to the current screen (vtable +0x1c, not
  queued) and destruction of its context (`1190:317c`), reading of the screen's context alone
  if it is not loaded (`1190:32fc`, "CD stream" device). **No parent contexts, queue or
  blocks** (these came from ScummVM; in The Lion King the only parent is 1100, always loaded).
- Command 0x10 (`1190:2f4a`): end of reading (`1190:3366`); if it is the incoming screen's
  context: it becomes current, ScreenEntry (0x11) queued, the observer re-enables updating
  (`1190:a24a`), end of the switch (`1190:324e`). No ContextLoadComplete & co. events.
- Startup: at the end of the BOOT.STM stream (`1190:2e68`) switch to the initial screen **with
  updating disabled** (the port passed 0).

## Timer, Path, Cursor

- Timer (`1190:d82e`): timePlay `d88a`, timeStop `da9c`, timePause `db40` (only if started and
  not paused), timeResume `dbd4` (only while paused, no arguments); no isPlaying. Fixed.
- Path (`10d8:0260`, not used by The Lion King): parameters 0x60e/0x60f points, 0x610 steps
  (u16), 0x611 steps per second (double); no duration. timePlay (`10d8:0520`): point = start,
  step 0, period 1/speed, next = now, scheduling (`10d8:0738`: next += period, if already past
  then now). Timer (`10d8:06b8`): step++; if < steps: point = start + step·(end−start)/steps +
  0.5 truncated (`10d8:05ae`) and PathStep (0x1c); otherwise PathEnd (0x10) without moving the
  point on arrival. timeStop (`10d8:0812`) only removes the timer (no event). Methods 0xe9/0xea
  x/y, 0xf2/0xf3 points, 0xf4 steps, 0xf5 speed, with no check on the number of arguments.
  Rewritten accordingly.
- Cursor (`1190:b1a0`): parameter 0x22, method 0xc8 makes it permanent.
- Cursor manager (`1190:659a`): 0x0c range, 0x15 new (only within the range and if free,
  `1190:62b2`), 0x16 delete (never the current or the permanent one, `1190:6516`); permanent
  only within the range (`1190:67be`). **Platform cursors (type 0)**: `LoadCursor(NULL, id)`
  (`11a0:0ac4`), i.e. the IDC_* system cursors: now SDL's system cursors; if the id does not
  exist the cursor is not created.

## Sounds: no timed responses

The Sound class has no timer (`1190:d3a6` does not start one): the "On Time" responses of a
sound are never executed and SoundEnd runs only its own response (`1190:d30e`). What came from
ScummVM has been removed from the port (The Lion King has no timed responses on sounds).

## Services: "skip the update"

`pre_display_sync` now returns `RT_SYNC_IDLE` (0), `RT_SYNC_CHANGED` (1) or `RT_SYNC_SKIP` (2)
(`runtime.h`; 1, 0, 2 in the executable). With SKIP the iteration does not update the screen.
The movie (gfx) must return SKIP when the next frame has already expired (`1190:fbc4`).

## Ctrl+V

`1188:0442`: after the key, DialogBox of dialog 1 ("About ..."). The port shows an SDL message
with the title and the fixed texts of the dialog read from the executable; the "*" fields
filled by the procedure `1188:56be` (strings 0x10, 0xb1–0xb5 of the language and file version)
are not reproduced.

## Audio formats and volume (third pass)

- The wave player (`DS:34CE`) opens the device with a `WAVEFORMAT` of type **1 (PCM)**
  (`1188:2ace`); beforehand `1188:27ae` accepts only 11025, 22050 or 44100 Hz, 1 or 2 channels,
  8 or 16 bits, and the combination must be among the device's capabilities (`WAVE_FORMAT_*`
  table at `DS:008E`). **No ADPCM or other codec** in the executable (no IMA tables, no
  conversions before `waveOutWrite`); the data goes to the device as is. The movies' sound
  also goes through here. A different format does not open and the sound fails.
- In the port: `audio_format_supported` and `audio_pcm_sample` (`audio_data.c`); a format that
  is not allowed gives SoundFailure instead of playing; stereo is now mixed per channel (the
  samples of the two channels used to be read one after the other). Test:
  `tools\audiotest\audiotest.bat`. The Lion King uses only 22050 Hz, mono, 16 bits.
- Volume: the player has `waveOutGetVolume` (`1188:28b0`) and `waveOutSetVolume` (`1188:2906`,
  16-bit value, the right channel equal if the device has separate volume, otherwise 0), but
  **nobody calls them** (no reference in the code or in the vtables). The port's script volume
  functions come from later engines.

## Stream commands (third pass)

| Command | Client | Behavior |
|---|---|---|
| 0x0e create context | actor map `1190:3d14` | only if BOOT.STM has the context reference (`1190:42be`); the parent is looked up among the loaded contexts |
| 0x0f destroy context | `1190:3d7e` | `1190:45ae` |
| 0x11 create actor | `1190:3da4` → `3ed6` | context, type, id; type without a factory → `EX_ActorMapBadActorClassId`; then parameter reading (vtable +0xc) |
| 0x12 destroy actor | `1190:3f5e` | if it does not exist, nothing |
| 0x13 load complete | `1190:3fde` | unknown actor: nothing; vtable +0x18 even if already loaded |
| 0x14 create variable | `1190:3e40` | reads context, id and value; creates it only if the context exists |
| 0x31 function | function handler `1198:824c` | context, id, code; appended at the end of the list, the lookup (`1198:82dc`) finds the first one |
| others | `1190:48e8` | every client answers 1: **no error**, the command's data is read as subsequent commands |

0x35/0x36 (multiple sounds/streams) have no client. In the port (`god.c`): the fatal errors
(unknown actor at load, variable in a context that is not loaded or already defined, unknown
command) are now warnings and the command is skipped; a redefined function keeps the first
definition; 0x35/0x36 are no longer read.

## Not yet done

- Dynamic fields of the Ctrl+V dialog.
