# The engine

Architecture of the port. The behavior follows the 1995 Media Station "Runtime Player" as
reconstructed by ScummVM (see [REFERENCES.md](REFERENCES.md)); this document describes how our C
code is organized.

## Modules (`engine\`)

| File | Role |
|---|---|
| `datafile.c/.h` | in-memory files, RIFF/IMTS subfiles, chunks, typed reads (`ms_t_*`), `ms_fatal`/`ms_warn` errors |
| `value.c/.h` | script values (`MsValue`): float, bool, time, token, actor, string, collection, function, method |
| `constants.h` | all the format numbers: sections, commands, actor types, parameters, opcodes, methods, events |
| `god.c/.h` | the title (`MsGod`, "ImtGod" in the original): BOOT.STM, file/stream/context/screen maps, loaded contexts, actors, global variables, functions, channels, stream reading |
| `actor.c/.h` | actor base: creation, common and "spatial" parameters, event responses, timed responses, alarm, spatial methods |
| `actors.c` | the types: screen, document, palette, cursor, timer, path, hotspot, image, sound, sprite, movie |
| `actors_dotgame.c` | constellations minigame (type 0x10) |
| `actors_zazu.c` | Stalking Zazu minigame (type 0x0f) |
| `bitmap.c/.h` | image header, reading, RLE8 decoding (also delta frames) |
| `audio_data.c` | parameters and chunks of the audio sequences |
| `script.c/.h` | bytecode interpreter, collections, predefined functions, decompiler |
| `runtime.c/.h` | the player: event loop, timers, document (screen changes and context loading), root stage, dirty areas, mouse, keyboard, sound device, timed input for tests |
| `video.c/.h` | SDL2: 640x480 8-bit framebuffer, palette, clipped drawing, lines, palette transitions, screenshots |
| `audio.c` | SDL2: 32-voice software mixer (44.1 kHz stereo), end of sounds |
| `cursors.c` | cursors from the NE resources of `LIONKING.EXE`, permanent/temporary cursor |
| `runtime_stub.c` | empty runtime for `msdump` (data only) |
| `main.c` | options, SDL startup, data lookup |

`runtime.h` is the boundary: actors call only `rt_*`/`audio_seq_*` functions, so the data layer
(`datafile`, `value`, `bitmap`, `god`, `actor*`, `audio_data`) also compiles inside `msdump` with
`runtime_stub.c`.

## Data and actors

A stream is a subfile of a data file. `god_read_stream(id)` reads it all at once (the original
read it in pieces from the CD) and dispatches the chunks: the `igod` ones to the loader (control
commands), the others to the actor registered on their channel (`god_register_channel`).
A movie has three channels (header, frames, sound), so `read_chunk` receives the sub-channel
(`CH_MAIN`, `CH_MOVIE_FRAMES`, `CH_MOVIE_SOUND`).

Each actor type is a `MsActorType` table (similar to a class): `read_param`,
`read_chunk`, `load_complete`, `call_method` (with `*handled = 0` it falls back to the common
spatial methods in `actor_call_method`), `on_event`, `timer`, `draw`, `destroy`. The type's own
data lives in `a->x` (`ext_size` bytes).

When the last piece of data of an actor has arrived, the stream contains the "load complete"
command: `god.c` calls the type's `load_complete` and then the runtime's `actor_loaded` hook,
which puts spatial actors on the root stage (id 2, the only one used by The Lion King).

"Streamed" movies and sounds are not in the context: they have a stream of their own (same id as
the actor) read at the first `play`.

## Main loop (`rt_run`)

Like `EventLoop::run` in the original, on each iteration:

1. expired actor alarms become queued events (`queue_expired_alarms`);
2. queued events are dispatched; those generated during dispatching wait for the next
   iteration (`dispatch_events`);
3. SDL events are read (mouse and window are queued, keyboard is handled immediately) together
   with the timed input;
4. "pre-display sync": playing movies update their visible frames;
5. screen update (`auto_update_and_flush`): the dirty areas are redrawn;
6. `audio_service`: sounds that have finished generate their events (SoundEnd etc.).

Queued events (`QEvent`) are: event for an actor (with argument), screen change, re-enabling of
automatic update, alarm, mouse.

### Timers and alarms

Each actor has an alarm (`actor_start_alarm`/`actor_stop_alarm`, "TimerEntry"). It is used by
timers, sounds and movies ("On Time" responses), sprites (frames), paths (steps) and
Zazu (Simba's steps). Timed responses are in time order in the file; `last_processed`
avoids repeating them.

### Document: screens and contexts

`runtime.c` holds the logic of the original's `Document`:

- `branch_to_screen`: disables automatic update (if requested), sends ScreenExit to the current
  screen and destroys its context, queues the parent contexts that are not loaded
  (and locks them), then the context of the new screen;
- `start_context_load` reads the context's stream; the "context load complete" command at the
  end of the stream triggers ScreenEntry of the new screen and re-enables updating;
- the document actor (id 1) receives `branchToScreen`, `quit`, `loadContext`, `releaseContext`...
  Load/release requests go through the queue as events 0x30/0x31.

Context 1100 is declared by BOOT.STM itself (global variables, functions, the timers and
hotspots common to all pages) and always stays loaded.

### Stage, drawing, dirty areas

The root stage (`stage[]`) keeps the spatial actors sorted by decreasing z: they are drawn in that
order (high z = behind) and the mouse queries them in reverse (front ones first). With equal z the
order of arrival counts. `rt_invalidate` adds a rectangle to the dirty area (at most 64, then
they are merged); `update_dirty` redraws, rectangle by rectangle, the visible actors that
intersect it, then shows the framebuffer and runs any "on sync" transition. The background is
never cleared: the background image covers it, as in the original.

Transparency: in RLE images and in type 7 bitmaps color 0 is transparent; uncompressed ones
(type 0) are opaque. Movies use delta frames that copy pixels from the key frame (decoded once
and kept in `MsBitmap.decoded`).

### Mouse and keyboard

Like `StageDirector`: for each event it looks for who accepts down/up/move/enter/exit
(`find_mouse_targets`), starting from the topmost actor. Active hotspots (rectangle + optional
polygon) and an active Zazu accept the mouse; whatever is left is taken by the root stage
(which restores the permanent cursor). Enter/exit are sent only when the actor under the mouse
changes. Keys go immediately to the first active hotspot with a KeyDown response for that code
or for 0 (any key).

### Automatic screen update

`rt_disable_auto_update` returns a token; only `rt_enable_auto_update` with the same token
re-enables it and forces a redraw. It is used by screen changes so as not to show the screen
half-built.

### Transitions

Transitions are palette-only (fade to black, to the registered palette, to a color, to a
palette object, percentages, color rotation) and, as in the original, block the program for
their duration (`fade_to` updates the screen every 5 ms). The registered palette is the one from
command 0x5aa of BOOT.STM/context (`ms_god.palette`).

### Audio and sound device

Each sequence (`MsAudioSeq`) has its own PCM chunks; `audio_seq_start` takes a mixer voice.
With "multiple sounds" disabled (a title setting), starting a sound or a movie stops the
previous one (`rt_grant_sound_device`, "ImtDeviceOwner").

### Cursors

BOOT.STM declares the cursors (command 0x15) with the name of the resource group in the original
executable. Screens and cursor actors set the "permanent" cursor, hotspots a "temporary" one
while the mouse is over them. `NOCURSOR` (fully transparent) hides the pointer.

## Diagnostics

- `--trace`: loads, screen changes, events delivered to actors, functions called;
- `??` on stderr: warning (unexpected behavior but execution continues); `!!`: fatal error;
- `msdump scripts` to read what a response does before investigating.
