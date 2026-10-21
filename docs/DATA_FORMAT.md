# Media Station data format (BOOT.STM, *.CXT)

Reference for the data reader in `engine\datafile.c`, `god.c`, `actor.c`, `actors*.c`,
`bitmap.c`, `audio_data.c`; the numeric constants are in `engine\constants.h`. All numeric
values are little-endian, except chunk and channel identifiers (4 characters, big-endian, i.e.
readable as text).

The Italian release of The Lion King has no "version info" section in BOOT.STM: the version comes
out as 0.0.0, i.e. a **first-generation** engine (`ms_god.ver_major == 0`). This only changes the
format of the movie frames (see below) and the numbers of the predefined functions (see
`SCRIPT.md`).

## Files

- `BOOT.STM`: a single subfile, no header. It is implicitly file 1 and stream 1.
- `NNN.CXT`: 16-byte header, then one or more subfiles.

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | 4 bytes | `II\0\0` (Intel byte order; `MM\0\0` is not used) |
| 0x04 | u32 | unknown (`0xEEEE` in The Lion King) |
| 0x08 | u32 | number of subfiles |
| 0x0C | u32 | file size (may differ by a few bytes from the real one: in 100.CXT there are 42 extra bytes at the end) |

`1100.CXT` is empty (header only, 0 subfiles): context 1100 is created by BOOT.STM.

## Subfiles and chunks

```
"RIFF" u32 len  "IMTS"
  "rate" u32 4  u32 rate         (0 for contexts, 44100 for movie streams)
  "LIST" u32 len "data"
     chunk, chunk, ...           up to the end of the RIFF chunk
```

Each chunk: `id` (4 characters) + `u32 len` + payload; every chunk starts at an **even** offset
(one padding byte if needed). Chunks are delimited by the RIFF, not by the LIST.

The chunk id is the **channel** the content goes to:

- `igod`: the loader (document and control commands, see below);
- `aNNN` (e.g. `a001`, `a00f`): an actor. The channel is declared in the actor's parameters
  (`AH_CHANNEL_IDENT`, or the movie channels); `god_register_channel` maps channel -> actor.

A **stream** is a subfile identified by an id (= the id of an actor or of a context): the
stream map in BOOT.STM says in which file and at which offset it is located. The first subfile of
a .CXT contains the context; the following ones are movie and sound streams, read only when they
start playing (`god_read_stream`).

## Typed data (datum)

Almost all values in `igod` chunks and in actor parameters are preceded by a `u16` type
(`ms_t_*` in `datafile.c`); an unexpected type is a fatal error.

| Type | Name | Payload |
|---|---|---|
| 0x02 | uint8 | u8 |
| 0x03 | uint16 | u16 |
| 0x04 | uint32 | u32 |
| 0x05 | int8 | s8 |
| 0x06 | int16 | s16 |
| 0x07 | int32 | s32 |
| 0x08 | float | float32 |
| 0x09 | double | float64 |
| 0x0A | filename | uint32 datum length + bytes |
| 0x0D | rect | point datum + graphic size datum |
| 0x0E | point | 2 graphic unit datums (x, y) |
| 0x0F | graphic size | 2 graphic unit datums (w, h) |
| 0x10 | graphic unit | s16 |
| 0x11 | time | float64 (seconds) |
| 0x12 | string | uint32 datum length + bytes (no terminating zero) |
| 0x13 | version | 3 uint16 datums |
| 0x1B | channel ident | u32 **big-endian** |
| 0x1D | polygon | (not used by The Lion King) |

## `igod` chunk

It starts with a uint16 datum "stream type":

- `0x01` document definition: BOOT.STM sections up to section 0;
- `0x0D` control commands: commands up to command 0.

### BOOT.STM sections

| Id | Section | Content |
|---|---|---|
| 0x0000 | end | |
| 0x0002 | context references | repeated until the u16 flag is 0: `0x0004` context id, `0x0005` stream id, [`0x0BB8` name], `0x0006` parent ... |
| 0x0007 | screen references | while flag != 0: `0x0009` screen id, `0x0004` context id |
| 0x000A | file info | while flag != 0: `0x002B` file id, `0x002D` location (7 = CD, 8, 11 = disk) + filename |
| 0x000B | stream info | while flag != 0: `0x002A` actor/stream id, `0x002B` file id, `0x002C` offset u32 |
| 0x0190 | version info | title string, version, engine string, source string (absent in The Lion King) |
| 0x0191, 0x0193 | unknown | a uint16 |
| 0x0192 | function table size | uint16 |
| 0x0BBA / 0x0BBB | param token | name (string), separator 0x0BBB, value (string containing a number) |
| others | | control commands (table below), also inside BOOT.STM |

**Quirk of the context references** (reproduced as in the original): after the id, the stream and
the optional name, one section is read; if it is 0x0006 the parents are read and at the end the
reader steps back to before the section that closed the list (which becomes the flag of the next
reference); if there are no parents the section that was read stays consumed. In The Lion King
all contexts have parent 1100 (1100 has parent 0).

Maps of The Lion King: 30 files (1 = BOOT.STM, 100 = 1100.cxt, 101..128 = the other .cxt),
505 streams, 29 contexts, 28 screens (screen N -> context N), initial screen 100,
cursors 100..103, no param tokens.

### Control commands

| Id | Command | Payload |
|---|---|---|
| 0x0C | cursor init | base u16, max u16 |
| 0x0E | create context | id |
| 0x0F | destroy context | id |
| 0x10 | context load complete | id (notifies the document) |
| 0x11 | create actor | context, type, id, then the actor's parameters up to 0 |
| 0x12 | destroy actor | id |
| 0x13 | actor load complete | id (calls the type's `load_complete`, then adds it to the stage) |
| 0x14 | create variable | context, id, ScriptValue |
| 0x15 | new cursor | type (0 = system + platform id; 1 = resource: ignored u16 + filename of the cursor group in the EXE), id |
| 0x16 | dispose cursor | id |
| 0x2E | startup information | section 0x2F + initial screen id |
| 0x31 | function | context, id, uint32 size, bytecode |
| 0x35 / 0x36 | allow multiple sounds / streams | uint8 (1 = yes) |
| 0x578 / 0x579 | update dirty / update all | none |
| 0x57A | effect transition | uint16 n + n ScriptValue |
| 0x57B | set default transition time | time |
| 0x5AA | load (register) palette | 768 raw RGB bytes |
| 0xBB9 | context name | id, string |

All ids are uint16 datums unless otherwise noted.

## Serialized ScriptValue

`val_read` in `value.c`: uint8 type datum, then:

| Type | Value | Payload |
|---|---|---|
| 0 | empty | |
| 1 | float | double |
| 2 | bool | uint8 (0/1) |
| 3 | time | time (read as a **float**, as in the original) |
| 4 | param token | uint16 |
| 5 | actor id | uint16 |
| 6 | string | uint16 length + raw bytes |
| 7 | collection | uint16 n + n ScriptValue |
| 8 | function id | uint16 |
| 9 | method id | uint16 |

## Actors

Command 0x11: context, type, id, then pairs of `parameter (uint16) + value` up to parameter 0.
Each parameter is looked up first by type, then (if spatial) among the spatial parameters, then
among the common ones; an unknown parameter is fatal.

| Type | Name | Used by The Lion King |
|---|---|---|
| 0x01 | Screen | 28 |
| 0x04 | Path | 0 |
| 0x05 | Sound | 166 |
| 0x06 | Timer | 130 |
| 0x07 | Image | 173 |
| 0x0B | Hotspot | 209 |
| 0x0C | Cursor | 0 |
| 0x0E | Sprite | 57 |
| 0x0F | Stalking Zazu | 1 |
| 0x10 | Dot game | 15 |
| 0x11 | Document | (created by the runtime, id 1) |
| 0x16 | Movie | 310 |
| 0x17 | Palette | 0 |

Stage (0x02), Text, Font, Camera, Disk image, Canvas, Printer, movie proxies: not used ("not
supported" error if they appeared).

### Common parameters

| Id | Parameter | Value |
|---|---|---|
| 0x0017 | script response | event uint16, argument ScriptValue, uint32 size, bytecode |
| 0x0BB8 | name | string (in The Lion King actors have no name) |

### Spatial parameters (images, sprites, movies, hotspots, minigames)

| Id | Parameter | Value |
|---|---|---|
| 0x001C | bounding box | rect |
| 0x001E | z | graphic unit (higher z = drawn first, i.e. behind) |
| 0x0019 | stage (parent) | uint16: **always 2** (the root stage) in The Lion King |
| 0x0020 | transparency | uint8 |
| 0x05DC | dissolve factor | double 0..1 |
| 0x077A / 0x077C / 0x077D | parallax x+y / x / y | double |

### Parameters by type

- **Screen**: `0x0022` cursor (registered as permanent).
- **Image**: `0x001B` channel; `0x001F` visible at start (uint8); `0x0032` load type (1 =
  decompress immediately); `0x05DE`/`0x05DF` x/y offset (uint16, added to the box); `0x077B`
  reference actor (shares the bitmap of another image). The channel's chunk contains ImageInfo +
  pixels.
- **Sprite**: `0x001B` channel; `0x001F` visible; `0x0024` frame rate (double, truncated to an
  integer); `0x0032` load type; `0x03E8` number of frames; `0x03E9` clip (token id, first,
  last); `0x03EA` default clip (otherwise 0x4B0); `0x077B` reference. Implicit clips:
  0x4B0 forward (0..n-1) and 0x4B1 backward (n-1..0). Each chunk of the channel is a frame:
  ImageInfo, uint16 index, origin point, pixels; frames are sorted by index.
- **Sound**: `0x001A` id (duplicate, checked); `0x001B` channel; `0x0021` discard after use;
  `0x0033` sound info (uint16 chunk count, uint32 frequency, uint16 channels, uint16 bits);
  `0x0034` caching, `0x0037` install type (uint8, ignored). The audio chunks arrive in the
  context or, more often, in the sound's stream, read at the first `play`.
- **Movie**: `0x001A` id; `0x001B` main channel (header); `0x06A4` frames channel; `0x06A5`
  sound channel; `0x0033` sound info; `0x001F` visible; `0x0021`, `0x0037` ignored; `0x0034`
  caching; `0x06AC` proxy (later engines only, not supported).
- **Hotspot**: `0x001D` active area: uint16 n + n points (polygon relative to the box; empty =
  the whole box); `0x001F` active at start; `0x0022` cursor; `0x05DD` receives events outside the
  stage.
- **Timer**: script responses only (Time events).
- **Path**: `0x060E`/`0x060F` start/end points; `0x0610` total steps; `0x0611` steps per
  second (double); `0x0612` duration (time).
- **Palette**: `0x05AA` 768 RGB bytes.
- **Cursor**: `0x0022` cursor id.
- **Dot game** (constellations): `0x001D` uint16 n + n points (the stars, relative to the box);
  `0x03EB` visible; `0x0514` starting hotspot; `0x0515` sprite of the arrival star; `0x0516`
  marker sprite; `0x0517` speed; `0x0518` line thickness (uint8); `0x0519` color (3 uint8 RGB).
- **Stalking Zazu**: `0x0022` cursor; `0x0024` timer per second (double); `0x03EB` active;
  `0x03EC` Simba's sprite; `0x03ED` directions: 5 triplets (direction token, standing clip,
  crouching clip) — tokens 0x44C..0x453 = W, SW, S, SE, E, NE, N, NW; clip index = direction
  (E=1, NE=2, N=3, NW=4, W=5, SW=6, S=7, SE=8), +9 if crouching; `0x03EE` field: rows,
  columns (the field covers the bounding box); `0x03EF` id of a rock (repeated); `0x03F0`
  step (graphic unit); `0x03F1` victory radius; `0x03F2`/`0x03F3` sounds; `0x03F4` audio
  active; `0x03F5` rocks to show (int16); `0x03F6` ignored point.

## Images (ImageInfo and RLE8)

ImageInfo: uint16 offset of the data from the start of the chunk payload, graphic size (w, h),
uint16 compression, uint16 stride. Pixels start at `chunk start + offset`.

| Compression | Meaning |
|---|---|
| 0 | raw, stride*h bytes, opaque |
| 1 | RLE8: color 0 is transparent |
| 5 / 6 | "CCC" (later engines, not used) |
| 7 | raw with color 0 transparent |

RLE8 (`bitmap_rle8_decode_delta`):

| Bytes | Operation |
|---|---|
| `n>0, c` | n pixels of color c |
| `0, 0` | end of line |
| `0, 1` | end of image |
| `0, 2, dx, dy` | copy dx pixels from the key frame (and dy rows below), then x += dx, y += dy |
| `0, 3, dx, dy` | skip (transparent): x += dx, y += dy |
| `0, n>=4, ...` | n literal pixels, then a padding byte if the offset (from the start of the compressed data) is odd |

Sequences may continue on the next row; pixels that are not written remain transparent.

## Movies

Main channel, sections (uint16):

- `0x06A8` header: uint16 number of chunks, double frame rate, n uint32 lengths;
- `0x06AB` chunk marker (the original's CD scheduling: ignored).

Frames channel:

- `0x06A9` image: ImageInfo, uint32 index, uint32 end of the keyframe (ms), pixels;
- `0x06AA` frames: uint16 n, then for each (**first generation**): uint16 blit type
  (1 normal, 2 delta, 3 compressed delta), uint32 start ms, uint32 end ms, uint16 x, uint16 y,
  uint32 image index, uint32 keyframe index, uint8 stays after the end. Later engines
  add layers (uint32 at the start), z and difference from the keyframe (3 int16 after x, y).

Sound channel: audio chunks of the soundtrack. The duration of the movie is the maximum end of the
frames; if a frame "stays after the end" the movie has a still image.

## Audio

All the audio of The Lion King (476 sequences) is **16-bit signed, mono, 22050 Hz** PCM; the
chunks (typically 22050 bytes, half a second) are played one after the other. 4 bits = IMA ADPCM
in later engines (not implemented).

## Numbers for the Italian Lion King

`msdump streams ..\DATA`: 28 contexts, 476 streams, 34,003 chunks read with no leftover bytes;
actors Screen 28, Sound 166, Timer 130, Image 173, Hotspot 209, Sprite 57, StalkingZazu 1,
DotGame 15, Movie 310; 29 functions, 193 global variables, 1,885 script responses, 253,151 bytes
of bytecode.
