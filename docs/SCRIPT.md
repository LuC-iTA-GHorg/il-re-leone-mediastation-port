# Media Script (bytecode)

Scripts are the actors' "script responses" (`On MouseDown ... End`) and the title's functions
(command 0x31). Interpreter: `engine\script.c` (`script_run`); constants in
`engine\constants.h`; decompiler: `script_decompile`, used by `msdump scripts`.

Scripts are also translated to C (`msdump transpile`; in the public port the generated sources
are produced at build time from your own game data into `build\gamesrc\`) and normally the game
runs those; the interpreter is used when a block has no C code and with `--interpret`.
See [RECOMPILATION.md](RECOMPILATION.md). The operations common to both paths (variables,
conditions, operators, calls) are in `engine\script_rt.c`.

## Structure

The bytecode is made of typed data (see `DATA_FORMAT.md`).

- **Block**: uint32 size datum, then expressions until the type read is 0 (empty).
  As in `LIONKING.EXE`, the size is used only to skip a block; the type of each datum is
  read and ignored (no check). The exact behavior of the original, with addresses, is in
  [ORIGINAL_SCRIPT.md](ORIGINAL_SCRIPT.md).
- **Expression**: uint16 type datum:

| Type | Meaning |
|---|---|
| 0x0000 | empty (end of block) |
| 0x0065 | variable: uint16 id, uint16 scope |
| 0x0066 | value: uint16 operand type + value |
| 0x0067 | operation: uint16 opcode + operands |

Variable scopes: 1 local (index from 1), 2 parameter (index from 1), 3 indirect parameter
(base index + expression), 4 global (id of the variable created by command 0x14).
A variable that does not exist (id 0, index out of bounds, unknown global, unknown scope)
is not an error: read, it gives empty; assigned, it changes nothing.

Operand types:

| Type | Value |
|---|---|
| 0x97 | bool (uint8) |
| 0x98 | float (double) |
| 0x99 | int (int32, becomes float) |
| 0x9A | string: uint16 length + raw bytes |
| 0x9B | param token (uint16) |
| 0x9C | actor id (uint16) |
| 0x9D | time (time) |
| 0x9E | serialized value (ScriptValue) — not in `LIONKING.EXE` |
| 0x9F | function id — not in `LIONKING.EXE` |
| 0xA0 | method id — not in `LIONKING.EXE` |

A bool is TRUE for any byte other than 0; a string ends at the first 0 byte.

## Opcodes

| Opcode | Name | Operands |
|---|---|---|
| 0xC8 | no operation | none (empty result) |
| 0xC9 | if | condition, block (non-bool condition: skips two blocks) |
| 0xCA | if-else | condition, true block, false block (non-bool condition: fatal error) |
| 0xCB | assign | variable, expression |
| 0xCC / 0xCD / 0xCE | or / xor / and | two bools |
| 0xCF..0xD4 | == != < > <= >= | two expressions |
| 0xD5..0xD9 | + - * / % | two expressions |
| 0xDA | negation | one expression |
| 0xDB | call function | uint16 id, uint16 n, n arguments |
| 0xDC | call method | uint16 method, uint16 n, receiver, n arguments |
| 0xDD | declare locals | uint16 n |
| 0xDE | return | expression |
| 0xDF | return without value | |
| 0xE0 | while | condition, block (the condition is re-evaluated on every iteration) |
| 0xE1 | indirect function call | uint16 n, expression (function id), n arguments |
| 0xE2 | indirect method call | uint16 n, expression (method id), receiver, n arguments |

`LIONKING.EXE` knows only 0xC8..0xDF (switch `1198:e158`); 0xE0..0xE2, operands
0x9E..0xA0, collections and the predefined functions of later engines come from ScummVM and
work only with `script_extensions = 1` (`script.h`); otherwise they are a fatal error, like the
original's exception. The Lion King does not use them.

A method's receiver is evaluated before the arguments (with the extensions, a variable
holding a collection is modified by the collection methods).

## Values and operations

Types of `LIONKING.EXE`: empty, float, bool, time, param token, actor id, string (0..6). In
addition, only with the extensions: collection, function id, method id (`value.h`).

Summary (details and addresses in [ORIGINAL_SCRIPT.md](ORIGINAL_SCRIPT.md)):

- Comparisons: different types are compared as floats, and every non-float value counts as 0.0
  (even a time); empty == empty is true and the other operators are false; bool and actor only
  == and !=; token ==, != and <; float, time and strings all six.
- Arithmetic depends on the type of the first operand: float∘float (a non-float second operand
  counts as 0.0), float × time = time, time × float and time / float = time, time + − time = time,
  time / time = float, string + string concatenates; the other combinations give empty or time 0.
  Division and modulo by zero: 0 (time 0 for times), no error.
- Logical: a non-bool operand counts as false. Negation: float and time, otherwise empty.
- Assignment: an empty value is assigned too.
- Title functions: at most 15 running at once; beyond that, the call does nothing (empty).
- `while` (extension) beyond 1000 iterations: warning and exit from the loop.

## Predefined functions

`LIONKING.EXE` registers only seven functions (`1198:8038`, `1190:a37e`), looked up before the
title's functions:

| Id | Function | Behavior (`LIONKING.EXE`) |
|---|---|---|
| 0x64 | Random(min, max) | exactly 2 arguments; float: integers (truncated) between min and max; bool; time (non-integers); generator 1188:d6c2 |
| 0x65 | TimeOfDay | with 1 argument: engine time (time); otherwise seconds since midnight (time) |
| 0x66 | EffectTransition | passes the arguments to the palette handler (`video.c`) |
| 0x67 | EffectTransitionOnSync | same, executed at the next screen update |
| 0x68 | Platform | float 3.0 |
| 0x69 | SquareRoot | square root of the float (0 if negative or not a float, empty with no arguments) |
| 0x6A | GetUniqueRandom(min, max, excluded...) | all floats; random integer between min and max that is not excluded |

An id that is neither one of these nor a title function gives empty (`??` warning from the port).
With the extensions, also valid are 0x6B CurrentRunTime, 0xAA..0xAC gamma, 0xB4 DebugPrint,
0xBE/0xBF volume, 0xC8 SystemLanguagePreference and the numbers 0x0A..0x1C of later engines.
Transitions (first argument, token): 300 FadeToBlack, 301 FadeToPalette, 302 SetToPalette;
303..310 come from later engines.

## Methods

Names as in `method_name` (`script.c`). Some numbers have different meanings depending on the
actor type.

| Id | Name | Actors |
|---|---|---|
| 0xC8 | cursorSet | Cursor |
| 0xC9 | branchToScreen | Document (id 1) |
| 0xCA / 0xCB | show / hide | Image, Sprite, Movie (Sound: show ignored) |
| 0xCC / 0xCD | moveTo / moveBy | spatial |
| 0xCE / 0xCF | play / stop | Sound, Movie, Sprite, Timer, Path |
| 0xD0 / 0xD1 | pause / resume | Sound, Timer, Path |
| 0xD2 / 0xD3 | activate / deactivate | Hotspot |
| 0xD8 | zMoveTo | spatial |
| 0xD9 | quit | Document |
| 0xDB | reset | Sprite (first frame of the clip) |
| 0xDC | setClip | Sprite |
| 0xDD / 0xDE | nextFrame / previousFrame | Sprite |
| 0xDF / 0xE0 / 0xE1 | zazuStartLooking / zazuStopLooking / zazuLookDirection | Stalking Zazu |
| 0xE2 / 0xE3 / 0xE4 | minigameReset / Activate / Deactivate | Dot game, Stalking Zazu |
| 0xE5 | dotHit | Dot game |
| 0xE6 | centerMoveTo | spatial |
| 0xE7 / 0xE8 | zazuDisableAudio / zazuEnableAudio | Stalking Zazu |
| 0xE9..0xEF | leftX, topY, width, height, centerX, centerY, z | spatial (leftX/topY also Path) |
| 0xF0 | clipId | Sprite |
| 0xF1 | setDissolve | spatial |
| 0xF2..0xF5, 0x106, 0x107 | setStartPoint, setEndPoint, setTotalSteps, setStepRate, setDuration, percentComplete | Path |
| 0xF6 | isPointInside | spatial, Hotspot (with polygon) |
| 0xF7..0x105, 0x10A | append, apply, count, deleteFirst, deleteLast, empty, getAt, isEmpty, jumble, seek, send, deleteAt, insertAt, replaceAt, prependList, sort | collections |
| 0x108 / 0x109 | mouseXOffset / mouseYOffset | spatial |
| 0x10D | isVisible | spatial |
| 0x141 / 0x142 | mouseAbsX / mouseAbsY | Hotspot |
| 0x169 | isLoading | Document |
| 0x173 | isActive | Hotspot |
| 0x174 | isPlaying | Sound, Movie, Sprite, Timer, Path (Document: setMultipleStreams) |
| 0x175 | isPaused | Sound, Path (Document: setMultipleSounds) |
| 0x176 / 0x177 / 0x178 | loadContext / releaseContext / contextIsLoaded | Document |

A method on actor id 0, on a value that is not an actor, or on an actor that is not loaded does
nothing and gives empty (`1198:e72e`; for the not-loaded actor the port prints a warning).
Methods accepted by each class in `LIONKING.EXE`: [ORIGINAL_SCRIPT.md](ORIGINAL_SCRIPT.md).

## Events (script response types)

| Id | Event | Id | Event |
|---|---|---|---|
| 0x05 | Time (argument: seconds) | 0x17 | SpriteMovieEnd (argument: clip) |
| 0x06 | MouseDown | 0x18 | MinigameSuccess |
| 0x07 | MouseUp | 0x19 | MinigameDefeat |
| 0x08 | MouseMoved | 0x1A | MinigameCrouched |
| 0x09 | MouseEntered | 0x1B | ScreenExit |
| 0x0A | MouseExited | 0x1C | PathStep |
| 0x0D | KeyDown (argument: code, 0 = any) | 0x1D | SoundStopped |
| 0x0E | SoundEnd | 0x1E | SoundBegin |
| 0x0F | MovieEnd | 0x1F | MovieStopped |
| 0x10 | PathEnd | 0x20 | MovieBegin |
| 0x11 | ScreenEntry | 0x21 | PathStopped |
| 0x13 | SoundAbort | 0x2C | ContextLoadComplete |
| 0x14 | SoundFailure | 0x2D | ContextAlreadyLoaded |
| 0x15 | MovieAbort | 0x2E | ContextReleaseComplete |
| 0x16 | MovieFailure | 0x2F | ContextAlreadyReleased |

The Time events of sounds, movies and timers fire at those seconds after the actor starts.

## Reading `msdump scripts`

```bat
build\msdump.exe scripts ..\DATA 169 > script169.txt
```

Real example (context 1100, the bottom-left hotspot of the pages):

```
== actor 107 Hotspot  box 11,410 71x55 z1000
On MouseDown(0x6)
    actor_108.stop()
    actor_110.activate()
    actor_111.deactivate()
    EffectTransition(FadeToBlack)
    actor_1.branchToScreen(var_103)
End
```

`actor_N` is an actor, `var_N` a global variable, `local_N`/`arg_N` locals and parameters,
`function_N` a title function, uppercase names are predefined functions,
`token_N` a param token. `actor_1` is the document.

**Names.** The public port prints raw ids only (`var_127`, `actor_239`, `function_10249`);
readable names are not available publicly. In the development tree (private) `msdump` can replace
raw ids with names from a names table (for example `var 127 language`, `actor 239
p01_narration_it`, `context 169 page01`, `function 10249 p01_words_image`), and the same example
above becomes `corners_timer.stop()`, `input_blocker.activate()`, `busy_blocker.deactivate()`,
`document.branchToScreen(prev_screen)`. Here, `actor_108` / `actor_110` / `actor_111` /
`var_103` stand for those readable names. Options before the command: `--no-names` (raw ids)
and `--names FILE`. `msdump vars ..\DATA [CONTEXT]` lists the variables of each context with
initial value (and name, when a names table is available).

## What The Lion King uses

Methods (calls in the bytecode): play 2154, deactivate 881, show 594, activate 526, hide 432,
stop 339, branchToScreen 152, reset 54, moveBy 29, setClip 23, quit 11, moveTo 8,
minigameActivate 8, zazuStartLooking 6, nextFrame 5, zazuStopLooking 4, minigameReset 4,
width 3, leftX 3, centerMoveTo 3, previousFrame 2, zazuLookDirection 1, dotHit 1, clipId 1.
Predefined functions: EffectTransition 173, Random 30, EffectTransitionOnSync 28.
All implemented.
