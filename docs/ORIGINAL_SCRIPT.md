# The script machine of LIONKING.EXE

Study of the script interpreter of the original executable (segment `1198`, values in `1190`)
and of how the port reproduces it (`engine\script.c`, `script_rt.c`, `value.c`, the C generated
by `msdump transpile`). Goal: 1:1 behavior, including in the cases The Lion King does not use.
Addresses as in [ORIGINAL.md](ORIGINAL.md) (Ghidra `segment:offset`). Updated 2026-10-21.

## Reading the bytecode

- Every typed read (`1188:e95a` u16, `1188:ea02` u32, `1188:e8ea` u8, `1188:eba4` int32,
  `1188:ecec` double, `1188:e4fc` time) reads the two type bytes of the datum and **ignores
  them**: no check, no limit (past the end it would read the memory that follows). The port does
  the same (`rd16`... in `script.c`); past the end it reads zeros (a block ends).
- Block (`1198:dda4`): u32 size **read and not used**, then expressions until the type is 0 or a
  `return` has been executed (flag +0x2C of the frame). After every expression the type of the
  next one is read, even after a `return`. No check on the bytes read (previously the port gave
  a fatal error if they did not add up).
- Skipping a block (`1198:dc48`): u32 size, position += size.
- Expression (`1198:de2c` → `1198:de5e`): 0x65 variable (`1198:defa`), 0x66 value
  (`1198:df20`), 0x67 operation (`1198:e158`); **any other type, 0 included, throws an
  exception**. Previously the port accepted 0 as an empty value: now a fatal error.

## Errors: the exceptions

Every "impossible" case does a `new` of an exception object (vtable `1198:eac2`) and
`1150:00de` (MFC's AfxThrow). The only `CATCH` around the engine is in `1198:024a`, around the
event loop `1188:fb70`: it closes the player (`1198:05b4`) and rethrows; `1188:0fac` shows a
message (`1188:118c`) and the program ends. So a script exception = end of the program with a
message: in the port `ms_fatal` (`!!`), same consequence. These throw an exception: unknown
expression type, operand type outside 0x97..0x9D, opcode outside 0xC8..0xDF, non-bool condition
of an `if-else`. Everything else (nonexistent variables, wrong types, divisions by zero,
nonexistent functions and actors) is **not** an error.

## Values (`1190:4a68`, 24 bytes)

+0 type, +2 "locked" flag, +4 time (double), +0x0C float (double) or word (bool, token, actor),
+0x14 pointer to the string. Types: 0 empty, 1 float, 2 bool, 3 time, 4 param token, 5 actor id,
6 string. **There are no collections, function ids or method ids**: they come from later
engines (in the port `VAL_COLLECTION`, `VAL_FUNCTION`, `VAL_METHOD`, only with the extensions).

- Every setter (`1190:4c5c` float, `4c80` bool, `4c9e` time, `4cce` token, `4cec` actor, `4d0a`
  string) and the copy `1190:4bb4` write only if flag +2 is 0. The flag is set only by
  `1190:4c4a`, which **has no callers**: in practice every assignment takes place.
- Copying an empty only changes the type: assigning an empty makes the variable empty.
- Accessors (port: `val_float_of`... in `value.c`): `1190:4dc0` float (0.0 for **every** other
  type, time included), `4e06` bool (0), `4e26` time (pointer to a static time 0), `4e80` token
  (0), `4ea0` actor (0), `4ec0` string (`DS:032A`, the empty string). No warnings.
- `_ftol` (`1180:1202`): FISTP with rounding toward zero (port: `ms_ftol`, 0x80000000 out of
  range like the x87).

## Variables (`1198:e970`)

u16 id, u16 scope. **Id 0: no variable and nothing else is read** (not even the index
expression of scope 3). Scope 1 local `id <= number of locals`, 2 parameter `id <= number of
parameters`, 3 indirect parameter: index = `_ftol(float(expression)) + id` (16 bit), valid if
`<= number of parameters` (index 0 would read before the array: in the port no variable), 4
global (lookup in the globals container, vtable +4), other scopes: no variable. No variable is
not an error: when read (`1198:defa`) the result stays empty, when assigned nothing happens.
Previously the port gave a fatal error.

## Operands (`1198:df20`, table `1198:e144`)

| Type | Read | Value |
|---|---|---|
| 0x97 bool | u8 | `CMP b,1; SBB; INC`: TRUE if the byte is not 0 (previously: error if > 1) |
| 0x98 float | double | float |
| 0x99 int | int32 | `FILD dword` → float |
| 0x9A string | u16 n, n raw bytes | `strcpy`: ends at the first 0 byte |
| 0x9B token | u16 | param token |
| 0x9C actor | u16 | actor id |
| 0x9D time | time (double) | time |
| other | — | exception |

## Opcodes (`1198:e158`, table `1198:e280`, 0xC8..0xDF)

| Op | Handler | Behavior |
|---|---|---|
| 0xC8 | `1198:e2b0` | no operation: no operands, empty result (new in the port) |
| 0xC9 if | `1198:e2b4` | cond; TRUE (bool == 1): executes the block; FALSE: skips it; **non-bool: skips twice** (the block, then whatever follows, read as the size of a block) |
| 0xCA if-else | `1198:e31a` | non-bool cond: exception; true: executes the first, skips the second (even after a `return`); false: skips the first, executes the second |
| 0xCB assign | `1198:e3e0` | first the variable, then the value; if the variable exists (and is not locked) it copies it and the result is the value, otherwise the result is empty |
| 0xCC..0xD9 | `1198:e454` | evaluates the first and the second operand, then the operator (below) |
| 0xDA | `1198:e602` | negation |
| 0xDB call | `1198:e642` | u16 id, u16 n; array of n+1 values (the last stays empty), n arguments evaluated in order, then `1198:814c` |
| 0xDC method | `1198:e72e` | u16 method, u16 n; receiver evaluated, then the arguments; if it is an actor: actor map (`DS:0326`, vtable +4) and the actor's method (vtable +4: arguments, n, result, method); otherwise nothing |
| 0xDD locals | `1198:e87c` | u16 n: frees the locals and creates n empty ones (n = 0: none; previously fatal error) |
| 0xDE return | `1198:e93c` | value directly into the frame's result (+8), flag +0x2C |
| 0xDF return | `1198:e960` | the flag only |
| other | | exception |

0xE0 while, 0xE1/0xE2 calls through variables: do not exist (port extensions).

### The frame and recursion (`1198:dc7c`, `1198:86a4`)

- Title function (`1198:f3ba` → `1198:dc7c`): if `DS:03B6 < 15` it increments it, copies the
  arguments into the frame, executes, decrements it; **otherwise it does nothing** (empty
  result, no error). So at most 15 title functions can run at the same time. Previously the
  port: 32 and a fatal error.
- Actor response (`1190:3766` → `1198:86a4`): frame without parameters, executed directly with
  `1198:dda4`, not counted.

### Operators (`1198:e454`), first operand `a`, second `b`

Logical (`1190:4ee8` or, `4f36` xor, `4f84` and): `bool(x) == 1`, a non-bool counts as false, no
error (previously: warning).

Comparisons (`1190:513c` → `1190:516a`): if the types differ they are compared **as floats**
(and `float()` of a non-float is 0.0: `time 5 == 5` is false, `empty == 0` is true):

| Type | Handler | Operators |
|---|---|---|
| empty | `1190:52c4` | == true, all the others false |
| float | `1190:52ec` | all; `FCOM b,a` + jump: with NaN `==`, `>`, `>=` are true, `!=`, `<`, `<=` false |
| bool | `1190:5394` | == and !=, the others false |
| time | `1190:53cc` | all |
| token | `1190:54de` | ==, != and < (unsigned); >, <=, >= always false |
| actor | `1190:5524` | == and != |
| string | `1190:555c` | all, inline `strcmp` (unsigned bytes) |

Previously the port warned and compared according to the type of the first operand (float and
time between themselves as numbers). The same `==` comparison is used to choose an event's
response (`1190:345c`, `actor.c`): now identical.

Arithmetic (`1190:5760`, according to the type of `a`):

| a | b | Handler | Result |
|---|---|---|---|
| float | time | `1190:5acc` | time: `a × b` for ×, **time 0** for the others |
| float | other | `1190:588c` | float: `b` is 0.0 if not a float; `/` and `%` by 0: **0.0**; `%` = `fmod(a, b)` (FPREM) |
| time | float | `1190:5b54` | time: × and / (no zero check), time 0 for + − % |
| time | other | `1190:5926` | `b` time (0 if not a time): + and − time, `/` **float** (time 0 if b = 0), `%` time `fmod` (0 if b = 0), × time 0 |
| string | string | `1190:5bf0` | + concatenates; the other operators give a string made of the uninitialized buffer (garbage): in the port an empty string |
| other | | | empty |

Previously the port: result always float for float/time, empty for float∘other.

Negation (`1190:56ec`): float → −x, time → time −x (`1190:6086`), other → empty (previously a
fatal error).

## Functions (`1198:814c`)

Functions are objects in a list (`DS:03B2`, `1198:8038`): the **first** one with the sought id
(+6) is taken; if there is none, nothing happens and the result stays empty. The built-in ones
are registered first (`1198:81fa`), the title functions (`1198:824c`, command 0x31) after: a
built-in function wins over a title function with the same id (previously the port did the
opposite; it does not happen in The Lion King). Registered by `LIONKING.EXE`:

| Id | Function | Address | Behavior |
|---|---|---|---|
| 0x64 | Random | `1198:eb9a` | only with 2 arguments; draws a number immediately; type of the 1st argument: float → `modf` (integer part truncated) of the two floats, bool → `bool == 1`, time → the two times, other → empty; if max < min they are swapped; integers: max += 0.99 (`DS:2FE6`); `r = (max − min) × n / 714024 + min`, integers: `floor`; result float, bool (`r == 1.0`) or time |
| 0x65 | TimeOfDay | `1198:f11c` | with exactly 1 argument: the engine clock time (`1190:05a4`, `DS:025A`) as a time; otherwise `(hours × 60 + minutes) × 60 + seconds` of the local time (`1188:e440`) as a time |
| 0x66 | EffectTransition | `1190:aef8` | arguments to the palette handler `DS:033E`, vtable +8 (`1190:a6dc`) |
| 0x67 | EffectTransitionOnSync | `1190:af24` | `1190:a5d4` |
| 0x68 | Platform | `1198:f1f4` | **float 3.0** (`DS:2FFE`) whatever the arguments (previously: token 0x76D) |
| 0x69 | SquareRoot | `1198:f19c` | without arguments empty; `x = float(arg1)`: `sqrt(x)` if x ≥ 0, otherwise 0.0 |
| 0x6A | GetUniqueRandom | `1198:ee58` | all arguments float (otherwise empty, no number drawn); draws a number; min/max truncated and sorted; candidates min, min+1... ≤ max+0.99 not equal (as floats) to any argument from the 3rd on; `i = floor(count × n / 714024)`, result `float(candidates[i])`; no candidates: empty |

With n = 714024 GetUniqueRandom reads one slot past the list (uninitialized value): in the port
the last one. With fewer than two arguments it reads the extra empty argument and then random
memory: in the port 0.0.

**Do not exist** in The Lion King: CurrentRunTime 0x6B, gamma 0xAA..0xAC, DebugPrint 0xB4,
volume 0xBE/0xBF, SystemLanguagePreference 0xC8 and the numbers 0x0A..0x1C: calling them gives
empty. In the port they remain only with `script_extensions`.

### The random number generator

`1188:d6c2`: `state = (state × 4096 + 150889) mod 714025` (`DS:01EA`, `DS:01E2`, `DS:01E6`,
`DS:01DE`), returns the state; `1188:d69c` gives 714024. Seeded only once from bits of the local
time (`1188:d6aa`, called near `1188:ddee`). The same generator serves Stalking Zazu and the
minigames of segment `1188` (for example `1188:a130` and the function that precedes it: `d6c2`
then the modulo `1180:0e6a`). Port: `script_random()` in `script.c` (seeded from `rt_random()`,
so `--seed` always gives the same sequence; `script_random_seed`).

## Methods: who accepts what

An actor's method = vtable +4 of its class (arguments, n, result, id). An unhandled method goes
up to the base class; the base `Actor` (`1190:27f8`) **does nothing** (empty result, no error).
Numeric arguments are read with `float()` (0.0 for a non-float, even for a time) and `_ftol`; no
handler checks the number of arguments except where stated: a missing argument is the extra
empty value of the array (`1198:e72e` allocates n + 1 of them), hence 0.

| Class | Handler | Own methods | Then |
|---|---|---|---|
| spatial | `1190:93a4` | 0xF6 isPointInside(x, y) → bool (`1190:6c02` on the stored box +0x1C, parameter 0x1C read by `1190:988c`: `left ≤ x < left + width`, same for y; for Image, Sprite and Movie it is the same box as the getters, not the drawn image: the original Image does not even read the offset parameters 0x5DE/0x5DF, `1190:c7a4`); 0xCC moveTo(x, y) (`1190:9672`, nothing if the position does not change); 0xCD moveBy(dx, dy) (`1188:8f9a`); 0xD8 zMoveTo(z) (`1190:982a`, only if it changes); 0xE6 centerMoveTo(x, y) (`1190:9734`: `x − w/2`, `y − h/2`); 0xE9..0xEF leftX, topY, width, height, centerX (`x + w/2` integer), centerY, z → float | base |
| Document (id 1) | `1198:7f92` | 0xC9 branchToScreen (`1198:7ec4`: nothing without arguments or if the first is not an actor ≠ 0; event 0x12 with the screen and "disable updating" = 1, 0 if the second argument is the token 0x708); 0xD9 quit (`1188:fd04`) | base |
| Screen 0x01 | `1190:cd84` | 0xC8 cursorSet (`1190:6110`, `67be`) | base |
| Cursor 0x0C | `1190:b1a0` | 0xC8 cursorSet if +0x1C ≠ 0 | base |
| Sound 0x05 | `1190:d348` | **nothing if not yet loaded** (+0x1A, before looking at the method); 0xCE play, 0xCF stop, 0xD0 pause, 0xD1 resume, without arguments | base |
| Timer 0x06 | `1190:d82e` | 0xCE, 0xCF, 0xD0, 0xD1, without arguments | base |
| Path 0x04 | `10d8:0260` | 0xE9 / 0xEA: x / y of the current point (+0x32 / +0x34) → float; 0xCE play (`10d8:0520`), 0xCF stop (`10d8:0812`); 0xF2 setStartPoint(x, y) and 0xF3 setEndPoint(x, y): `_ftol(float())`, the 2nd argument is converted first, then the 1st; +0x20 = x (1st), +0x22 = y (2nd) (`10d8:02f7..035e`); 0xF4 setTotalSteps: `_ftol(float())`; 0xF5 setStepRate: the double from `float()` | base |
| Image 0x07 | `1190:c9a0` | 0xF1 setDissolve (with at least one argument), 0xCA show, 0xCB hide (nothing if already so) | spatial |
| Hotspot 0x0B | `1190:c126` | 0xF6 isPointInside with the polygon (`1190:c00c`); 0xD2 activate; 0xD3 deactivate (clears the "entered" hotspot `DS:0342` and the "pressed" one `DS:0346`) | spatial |
| Sprite 0x0E | `1068:0566` | 0xDB reset (`1068:0cb6`, first frame of the clip); 0xCA show, 0xCB hide (`1068:0d1e`); 0xCE play, 0xCF stop; 0xF1 setDissolve (`1068:0d44`, at least one argument, 0..1); 0xDC setClip: token only with exactly 1 argument, otherwise 1200 (0x4B0), nothing if it is already that clip (`1068:0bf6`); 0xDD nextFrame(bool, only with 1 argument): if it cannot advance and bool = 1 it returns to the first (`1068:0b7c`); 0xDE previousFrame(bool): if it cannot go back and bool = 1 it goes to the last (`1068:0bcc`, `0cda`); 0xF0 clipId → token (+0x68). **No isPlaying (0x174)** | spatial |
| Movie 0x16 | `1190:f998` | **nothing if not yet loaded** (+0x1A); 0xCA, 0xCB; 0xCE, 0xCF, 0xD0, 0xD1 (`1190:f2a6`); 0xF1 setDissolve (0..1, `DS:2F4A`/`DS:2F42`); 0x140 ignored | spatial |
| Dot game 0x10 | `1190:b6f6` | 0xE2 reset (`1190:be36`) only with exactly 1 argument; 0xE3 activate (if not active: flag +0x3E, `1190:b944`, `b794`, redraw); 0xE4 deactivate (if active: flag, `1190:ba1e`, redraw); 0xE5 dotHit (`1190:baf8`) only if active | spatial |
| Stalking Zazu 0x0F | `1188:70f2` (table `1188:71b5`, 0xDF..0xE8) | 0xDF startLooking / 0xE0 stopLooking (`1188:735c` 1 / 0); 0xE1 lookDirection → token 0x44C if the direction (`1188:7cc2`) is 5, otherwise 0x450; 0xE2 reset (`1188:869e(_ftol(float(arg1)))` then `1188:86d2`, no argument checks); 0xE3 activate / 0xE4 deactivate (`1188:7398` 1 / 0); 0xE7 / 0xE8 (`1188:72d8` 0 / 1, audio); 0xE5 and 0xE6 go to spatial (0xE6 = centerMoveTo) | spatial |

The document actor is not in the actor factory: it is the object created by `1198:8038` (the
same one that registers the built-in functions), with the method handler at `1198:7f92`. No
loadContext, releaseContext, contextIsLoaded, isLoading, setMultipleStreams/Sounds (later
engines).

In the port (`actor.c` `actor_call_method`, `actors.c`): an unhandled method is silent (with
`--trace` an "ignored" line); the spatial methods are only those of `1190:93a4`; setDissolve
only for Image, Sprite and Movie; 0x108/0x109 (mouse offset), 0x10D isVisible, isPlaying of
Sprite/Movie/Sound, isPaused and show of Sound, 0x141/0x142/0x173 of Hotspot only with
`script_extensions`; Movie and Sound ignore everything before loading; arguments with `float()`
+ `_ftol` and without a count check.

## Event responses

A response (`1190:3766` → `1198:86a4`) runs with a frame **without parameters** and does not
count toward the limit of 15 functions. The port already does this: `actor_run_response` and the
timed responses call `rt_run_code(..., NULL, 0)`.

## What changed in the port (2026-10-21)

- `script.c`: interpreter rewritten on the model above (reads without type checks, block sizes
  not checked, a non-bool `if` that skips two blocks, opcode 0xC8, literal bool ≠ 0, exceptions
  = fatal error only where the original throws them), exact built-in functions, generator
  1188:d6c2, limit of 15 functions, built-ins looked up before title functions, methods only on
  actors.
- `script_rt.c`: nonexistent variables = NULL without error, assignment of empty too,
  `rs_if`/`rs_if_else`/`rs_resume`, `rs_extension`.
- `value.c`: exact accessors, comparisons (`1190:516a`), arithmetic (`1190:5760`), negation.
- Extensions (0xE0..0xE2, operands 0x9E..0xA0, collections, other built-in functions, methods
  of later engines): only with `script_extensions = 1`, otherwise a fatal error (an exception in
  the original) or, for methods, nothing.
- Translator (`tools\msdump\transpile.c`) aligned: all 1,914 scripts of the game translate (677
  functions, no script rejected). In the public port the recompiled scripts are produced at
  build time from the user's own game data into `build\gamesrc\` and must not be published.
- Self-test (`tools\msdump\selftest.bat`) extended to every opcode and built-in function.
- Method dispatch as above (`actor.c`, per-type handlers in `actors.c`).

## For the other modules

- **Graphics** (`video.c`): `EffectTransition`/`OnSync` receive the arguments as they are: the
  original reads them with the exact accessors (a non-float argument is 0.0, the type is a token
  read with `1190:4e80`). `video.c` still uses `val_as_float`, which also accepts times and
  warns: to be switched to `val_float_of`/`val_token_of` (it changes nothing in The Lion King).
  Image show/hide: the original redraws only if the visibility changes.
- **Minigames**: Stalking Zazu uses the same generator as the scripts (`1188:d6c2` and then a
  modulo): `rt_random()` in `actors_zazu.c` should be replaced by `script_random() % n`. The
  Zazu and Dot game handlers in `actors_zazu.c` / `actors_dotgame.c` already match the table
  above.

## To do

- Time comparisons with NaN (`1190:53cc`) not verified in assembly.
- The values read from the data (`val_read`, variable declarations) have not been compared with
  the original's reader.
