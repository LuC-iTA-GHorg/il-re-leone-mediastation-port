# Static recompilation of scripts

The game's scripts (Media Script bytecode in the `.CXT` files and in `BOOT.STM`, see
[SCRIPT.md](SCRIPT.md)) are translated to C by `msdump transpile` and compiled into
`lionking.exe`. During the game the engine calls the C functions instead of interpreting the
bytecode; the interpreter stays as a fallback and for comparisons (`--interpret`).

## Pieces

| File | Role |
|---|---|
| `tools\msdump\transpile.c` | translator: walks the bytecode like the interpreter and writes C |
| `engine\script_rt.h`, `script_rt.c` | API used by the generated code (and by the interpreter itself) |
| `engine\script.c` | `script_run`: looks up the C function for the block, interprets if there is none |
| `scripts_<context>.c` | one C function per script, with the decompiled script in a comment |
| `scripts_index.c` | sorted table (hash, length, function) |

In the public port the generated files (`scripts_<context>.c`, `scripts_index.c`) are produced at
build time from the user's own game data into `build\gamesrc\`. They contain the game's
recompiled scripts, so they must not be published or committed; building needs the game data.
The build compiles every `*.c` it finds in `build\gamesrc\`.

## Regenerating

The build script runs the generation step itself. Manually, the equivalent is:

```bat
build.bat
build\msdump.exe transpile ..\DATA build\gamesrc
build.bat
```

Expected output: `29 contexts, 1885 responses, 29 functions -> 677 unique C functions, 0 refused`.
Many scripts are identical (for example the Time responses of the timers) and become a single
function. `msdump` already contains the old table: it does not matter, the translator reads the
data.

The comments of the generated code use readable names when a names table is available (see
[SCRIPT.md](SCRIPT.md)): in the script header (`actor 102 Hotspot hotspot_replay`), in the
decompiled text and next to `rs_var`, `val_actor`, `rs_call` (`/* corner_replay_overlay */`).
The public port has no names table, so these comments show raw ids. Only the comments differ:
the C code, the function names `ms_script_<hash>_<len>` and the hash index do not change,
so changing a name does not change behavior. `msdump --no-names transpile ..\DATA
build\gamesrc` gives the comments with raw ids.

## How a script is recognized

The key is the content: a 64-bit FNV-1a hash of the bytecode block plus its length
(`script_hash`). When an actor or a title function runs a script, `script_run`
computes the hash and looks it up in the table with a binary search; if nothing is found
(different data, modified script) it uses the interpreter. This way it is not necessary to know
which context, actor or event the script comes from (the callers in `runtime.c`/`actor.c` do not
change) and different data can never run the wrong code. The identity (context, actor, event,
argument or `function_N`) is written in the comment of each function and in the table;
duplicates are marked with `(+N)`.

## Shape of the generated code

Example (context 1100, hotspot 102, MouseDown):

```c
MsValue ms_script_7c8992c2d283a2d5_222(MsFrame *f) {
	MsValue rv = val_empty();
	(void)f;
	{
		MsValue t1 = val_actor(111);
		MsValue t2 = rs_method_value(f, 0xd2 /* activate */, t1, NULL, 0);
		val_free(&t2);
	}
	{
		MsValue *p3 = rs_var(f, 119, SCOPE_GLOBAL);
		MsValue t4 = rs_get(p3);
		MsValue t5 = val_actor(101);
		MsValue t6 = rs_compare(OP_NOT_EQUALS, t4, t5);
		if (rs_if_else(t6)) {
			...
```

- Each evaluation is a statement with its own temporary `tN`: the evaluation order is
  that of the bytecode (in C the order of a call's arguments is undefined).
- `if`, `if-else`, `while`, `return` become `if`, `for (;;)`, `goto done`.
- Variables are pointers `pN` obtained from `rs_var` / `rs_var_indirect`, which can be
  NULL (non-existent variable, as in `LIONKING.EXE`): `rs_get` reads (empty if NULL),
  `rs_assign` assigns and gives the value (empty if NULL). A method's receiver is read
  before the arguments (`rs_get`, then `rs_method_var`), as in `1198:e72e`.
- `if` with a non-boolean condition: the original skips two blocks (the second read from what
  follows, `1198:e2b4`). C cannot do this with its structured flow: `rs_if` returns
  -1 and the generated code continues in the interpreter with `rs_resume(f, position, "tb...")`
  (position where the second block is read; for each block containing the `if`, from the
  innermost: `t` true block of an if-else, which at the end skips the else, `b` the others). This is
  why `MsFrame` also contains the bytecode (`code`, `size`).
- `MsFrame` contains locals, parameters, the actor (for messages) and the bytecode, like the
  interpreter's `Exec`.
- The `rs_*` helpers are the same functions the interpreter uses: same behavior as
  the original ([ORIGINAL_SCRIPT.md](ORIGINAL_SCRIPT.md)), same warnings and same fatal
  errors. The constructs that `LIONKING.EXE` does not have (`while`, calls through variables,
  operands 0x9E..0xA0) begin with `rs_extension(...)`, a fatal error if `script_extensions` is 0.
- Literals: floats and times in hexadecimal (`%a`, exact value), strings with octal escapes,
  serialized values (operand 0x9E) as bytes read with `val_read` at run time.
- The translator reads the bytecode like the interpreter: the type of each datum is skipped
  without checking it.

The translator refuses (and leaves to the interpreter) a script with constructs that structured C
cannot render identically: `return`, `if`, `while` or a locals declaration inside
an expression, blocks whose size does not add up, unknown opcodes or expression types
(the interpreter then stops with the fatal error, like the original's exception), a variable with
id 0 (the original does not even read the index), an `if` inside a `while`. In The Lion King: none.

## Translator self-test

`tools\msdump\selftest.bat` (with `BUILD_OUT` as in `build.bat`): `tools\msdump\selftest.c`
hand-assembles 34 scripts that use every opcode (0xC8..0xDF and the extensions 0xE0..0xE2), every
operand type, every predefined function of `LIONKING.EXE`, non-existent variables, indirect
parameters, the limit of 15 running functions, an `if` with a non-boolean condition (also inside
an if-else, hence `rs_resume`), translates them, recompiles with the generated C and runs each
script with the interpreter and in C over an argument matrix (13 values of all
types: every binary operator on every pair, Random and GetUniqueRandom on every pair):
about 3,000 comparisons, the results must be identical. About thirty results are also
compared with those derived from `LIONKING.EXE` (e.g. `time + float` = time 0, `2.5 / 0` = 0,
`"ab" - "b"` = empty string, `Random(1, 9)` with generator 1188:d6c2).

## Options and checks

- `lionking.exe --interpret`: interpreter only, as before.
- Environment variable `MS_SCRIPT_STATS=1`: on exit prints to stderr how many scripts ran in C
  and how many were interpreted; `MS_SCRIPT_STATS=2` also prints every script without
  C code (actor, length, hash).

Verification done (2026-10-20), always comparing the same exe with and without `--interpret`:

- compilation with no warnings; `msdump streams ..\DATA` with no `??`; `msdump scripts` identical
  to before; `msdump transpile` reproducible (same output on every run);
- startup with `--click 27000:500:100`, screenshots at 20 and 40 s: byte-for-byte identical;
- reading the book ("Leggi" (Read), click on the page, two pages forward, back arrow; screenshots
  at 50, 68, 85, 110, 125 s): 5 identical screenshots, 38 scripts all run in C, 0 warnings;
- the 28 screens with `--screen` (as done by the development tree's screen-scan script): 0
  warnings and 0 errors in both modes, 0 scripts interpreted in normal mode; 26 screenshots out
  of 28 identical, the two different ones (213, 225) depend on the instant of the screenshot
  (6000 vs 6003 ms): redoing 213 the C path and the interpreter give the same image;
- Stalking Zazu (the README example): identical screenshot, same two expected warnings;
- `tools\msdump\selftest.bat`: equal results.

## Limits

- After a change to `script_rt.c` or to the functions of `script.c` there is no need to
  regenerate; after a change to the translator or to the `script_rt.h` API there is. If the
  generated C no longer compiles with the new API, `msdump` (which includes it) must first be
  compiled with an empty table (`ms_native_scripts = {{0, 0, NULL}}`, `ms_native_count = 0`)
  in place of `build\gamesrc\*.c`.
- There is no mode that runs both paths and compares the results (scripts have
  effects on the world: movies, sounds, screens): the comparison is done with two timed runs and
  screenshots.
