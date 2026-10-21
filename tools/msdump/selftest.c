/* Self-test of the script VM: hand-assembled bytecode for every opcode, operand type and
 * built-in function of LIONKING.EXE (and the extensions of later engines), run with the
 * interpreter and with the recompiled C, which must give identical results; a few results are
 * also checked against the behaviour read in LIONKING.EXE (docs\ORIGINAL_SCRIPT.md).
 *   selftest gen FILE.c  writes the recompiled scripts (build without SELFTEST_NATIVE)
 *   selftest             runs everything and compares
 * See docs\RECOMPILATION.md for the commands. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "datafile.h"
#include "god.h"
#include "script.h"
#include "script_rt.h"
#include "transpile.h"

#ifndef SELFTEST_NATIVE
const MsNativeScript ms_native_scripts[] = {{0, 0, NULL}};
const int ms_native_count = 0;
#endif

/* ---- assembler ---- */

static uint8_t buf[1 << 16];
static size_t len;

static void raw(const void *p, size_t n) {
	memcpy(buf + len, p, n);
	len += n;
}
static void d16(uint16_t v) {
	uint16_t t = DATUM_UINT16;
	raw(&t, 2);
	raw(&v, 2);
}
static size_t block_begin(void) {
	uint16_t t = DATUM_UINT32;
	uint32_t z = 0;
	raw(&t, 2);
	size_t at = len;
	raw(&z, 4);
	return at;
}
static void block_end(size_t at) {
	d16(EXPR_EMPTY);
	uint32_t size = (uint32_t)(len - at - 4);
	memcpy(buf + at, &size, 4);
}
static void op(uint16_t o) {
	d16(EXPR_OPERATION);
	d16(o);
}
static void var(uint16_t id, uint16_t scope) {
	d16(EXPR_VARIABLE);
	d16(id);
	d16(scope);
}
static void num(int32_t v) {
	uint16_t t = DATUM_INT32;
	d16(EXPR_VALUE);
	d16(OPERAND_INT);
	raw(&t, 2);
	raw(&v, 4);
}
static void dbl(uint16_t operand, uint16_t datum, double v) {
	d16(EXPR_VALUE);
	d16(operand);
	raw(&datum, 2);
	raw(&v, 8);
}
static void boolean(uint8_t v) {
	uint16_t t = DATUM_UINT8;
	d16(EXPR_VALUE);
	d16(OPERAND_BOOL);
	raw(&t, 2);
	raw(&v, 1);
}
static void strn(const char *s, uint16_t n) {
	d16(EXPR_VALUE);
	d16(OPERAND_STRING);
	d16(n);
	raw(s, n);
}
static void str(const char *s) {
	strn(s, (uint16_t)strlen(s));
}
static void val16(uint16_t operand, uint16_t v) {
	d16(EXPR_VALUE);
	d16(operand);
	d16(v);
}
static void call(uint16_t id, uint16_t n) {
	op(OP_CALL_FUNCTION), d16(id), d16(n);
}
static void method(uint16_t m, uint16_t n) {
	op(OP_CALL_METHOD), d16(m), d16(n);
}
#define L(n) var(n, SCOPE_LOCAL)
#define A(n) var(n, SCOPE_PARAMETER)
#define T(n) d16(n), d16(SCOPE_LOCAL) /* assignment target: no expression type */
#define TA(n) d16(n), d16(SCOPE_PARAMETER)

typedef struct {
	const char *name;
	size_t off, size, top;
	int ext; /* needs script_extensions */
} Script;

static Script scripts[96];
static int nscripts;

static void begin(const char *name, int ext) {
	Script *s = &scripts[nscripts];
	s->name = name;
	s->off = len;
	s->ext = ext;
	s->top = block_begin();
}
static int end(void) {
	Script *s = &scripts[nscripts];
	block_end(s->top);
	s->size = len - s->off;
	return nscripts++;
}

/* ---- the scripts ---- */

static int s_binop[OP_MODULO - OP_OR + 1], s_negate, s_vars, s_if, s_if_nested, s_ifelse, s_return,
	s_builtin[8], s_calls, s_methods, s_literals, s_ext, f_add, f_rec;

#define F_ADD 500
#define F_REC 501

static void assemble(void) {
	/* return arg_1 OP arg_2, for every binary operator */
	for (int o = OP_OR; o <= OP_MODULO; o++) {
		begin("binop", 0);
		op(OP_RETURN), op((uint16_t)o), A(1), A(2);
		s_binop[o - OP_OR] = end();
	}
	begin("negate", 0);
	op(OP_RETURN), op(OP_NEGATE), A(1);
	s_negate = end();

	/* variables: nop, assignment value, empty assignment, variables that do not exist, locals 0,
	 * indirect parameters; returns local_2 + "|" + ... as a string where possible */
	begin("vars", 0);
	op(OP_DECLARE_LOCALS), d16(4);
	op(OP_ASSIGN), T(1), op(OP_NOP);                 /* local_1 = empty */
	op(OP_ASSIGN), T(2), op(OP_ASSIGN), T(3), A(1);  /* local_2 = (local_3 = arg_1) */
	op(OP_ASSIGN), T(4), op(OP_ASSIGN), T(9), str("lost"); /* local_9 does not exist: empty */
	op(OP_ASSIGN), TA(9), str("x");                  /* parameter 9 does not exist */
	op(OP_IF), op(OP_EQUALS), L(4), op(OP_NOP);      /* local_4 == empty */
	{
		size_t b = block_begin();
		op(OP_ASSIGN), T(2), op(OP_ADD), L(2), str("|e");
		block_end(b);
	}
	op(OP_IF), op(OP_EQUALS), var(1234, SCOPE_GLOBAL), op(OP_NOP); /* unknown global: empty */
	{
		size_t b = block_begin();
		op(OP_ASSIGN), T(2), op(OP_ADD), L(2), str("|g");
		block_end(b);
	}
	/* arg_[1 + 1 - 1] = arg_2 ; arg_[2 + 7] out of range */
	op(OP_ASSIGN), T(2), op(OP_ADD), L(2);
	d16(EXPR_VARIABLE), d16(2), d16(SCOPE_INDIRECT_PARAMETER), num(0);
	op(OP_ASSIGN), T(1);
	d16(EXPR_VARIABLE), d16(2), d16(SCOPE_INDIRECT_PARAMETER), dbl(OPERAND_FLOAT, DATUM_DOUBLE, 7.9);
	op(OP_IF), op(OP_EQUALS), L(1), op(OP_NOP);
	{
		size_t b = block_begin();
		op(OP_ASSIGN), T(2), op(OP_ADD), L(2), str("|i");
		block_end(b);
	}
	/* scope 7 does not exist */
	op(OP_ASSIGN), T(1), var(1, 7);
	op(OP_IF), op(OP_EQUALS), L(2), str("b|e|gab|i");
	{
		size_t b = block_begin();
		op(OP_DECLARE_LOCALS), d16(0); /* drops every local: local_1 no longer exists */
		op(OP_IF), op(OP_EQUALS), L(1), op(OP_NOP);
		{
			size_t c = block_begin();
			op(OP_RETURN), str("ok");
			block_end(c);
		}
		block_end(b);
	}
	op(OP_RETURN), L(2);
	s_vars = end();

	/* if: TRUE runs the block, FALSE skips it, anything else skips two blocks (the rest of the
	 * script here: the "second block" size is read from the next statement) */
	begin("if", 0);
	op(OP_DECLARE_LOCALS), d16(1);
	op(OP_ASSIGN), T(1), num(1);
	op(OP_IF), A(1);
	{
		size_t b = block_begin();
		op(OP_ASSIGN), T(1), num(2);
		block_end(b);
	}
	op(OP_RETURN), op(OP_ADD), L(1), num(10);
	s_if = end();

	/* the same inside the true block of an if-else (resume with "tb") */
	begin("if_nested", 0);
	op(OP_DECLARE_LOCALS), d16(1);
	op(OP_ASSIGN), T(1), num(1);
	op(OP_IF_ELSE), boolean(1);
	{
		size_t b = block_begin();
		op(OP_IF), A(1);
		{
			size_t c = block_begin();
			op(OP_ASSIGN), T(1), num(2);
			block_end(c);
		}
		block_end(b);
		b = block_begin();
		op(OP_ASSIGN), T(1), num(3);
		block_end(b);
	}
	op(OP_RETURN), L(1);
	s_if_nested = end();

	/* if-else with a bool (a non-bool ends the program, like the exception of the original) */
	begin("ifelse", 0);
	op(OP_IF_ELSE), A(1);
	{
		size_t b = block_begin();
		op(OP_RETURN), str("yes");
		block_end(b);
		b = block_begin();
		op(OP_RETURN), str("no");
		block_end(b);
	}
	s_ifelse = end();

	/* return from a nested block, dead code after it; return without value */
	begin("return", 0);
	op(OP_IF), op(OP_GREATER), A(1), num(0);
	{
		size_t b = block_begin();
		op(OP_IF), boolean(1);
		{
			size_t c = block_begin();
			op(OP_RETURN), str("deep");
			block_end(c);
		}
		op(OP_RETURN), str("dead");
		block_end(b);
	}
	op(OP_RETURN_NO_VALUE);
	op(OP_RETURN), str("dead too");
	s_return = end();

	/* built-ins: Random, TimeOfDay(1 arg), Platform, SquareRoot, GetUniqueRandom, missing ids */
	begin("Random", 0);
	op(OP_RETURN), call(FN_LEGACY_RANDOM, 2), A(1), A(2);
	s_builtin[0] = end();
	begin("TimeOfDay(x)", 0);
	op(OP_RETURN), call(FN_LEGACY_TIME_OF_DAY, 1), A(1);
	s_builtin[1] = end();
	begin("Platform", 0);
	op(OP_RETURN), call(FN_LEGACY_PLATFORM, 0);
	s_builtin[2] = end();
	begin("SquareRoot", 0);
	op(OP_RETURN), call(FN_LEGACY_SQUARE_ROOT, 1), A(1);
	s_builtin[3] = end();
	begin("SquareRoot()", 0);
	op(OP_RETURN), call(FN_LEGACY_SQUARE_ROOT, 0);
	s_builtin[4] = end();
	begin("GetUniqueRandom", 0);
	op(OP_RETURN), call(FN_LEGACY_GET_UNIQUE_RANDOM, 4), A(1), A(2), num(3), num(5);
	s_builtin[5] = end();
	begin("Random(3 args)", 0);
	op(OP_RETURN), call(FN_LEGACY_RANDOM, 3), A(1), A(2), A(1);
	s_builtin[6] = end();
	begin("missing function", 0);
	op(OP_RETURN), call(FN_LEGACY_CURRENT_RUN_TIME, 0);
	s_builtin[7] = end();

	/* title functions: arguments, result, the limit of 15 running functions */
	begin("function add", 0);
	op(OP_RETURN), op(OP_ADD), A(1), A(2);
	f_add = end();
	begin("function rec", 0); /* rec(n) = n > 0 ? rec(n - 1) + 1 : 0 */
	op(OP_IF_ELSE), op(OP_GREATER), A(1), num(0);
	{
		size_t b = block_begin();
		op(OP_RETURN), op(OP_ADD), call(F_REC, 1), op(OP_SUBTRACT), A(1), num(1);
		num(1);
		block_end(b);
		b = block_begin();
		op(OP_RETURN), num(0);
		block_end(b);
	}
	f_rec = end();
	begin("calls", 0);
	op(OP_RETURN), op(OP_ADD), call(F_ADD, 2), A(1), num(1), call(F_REC, 1), A(1);
	s_calls = end();

	/* methods on things that are not loaded actors: empty, no error */
	begin("methods", 0);
	op(OP_DECLARE_LOCALS), d16(1);
	op(OP_ASSIGN), T(1), str("s");
	op(OP_RETURN), op(OP_ADD), op(OP_ADD), method(0xe9, 0), A(1), method(0xce, 1), L(1), num(4);
	op(OP_ADD), method(0xe9, 0), val16(OPERAND_ACTOR, 0), method(0xf9, 0), num(3);
	s_methods = end();

	/* literals: bool byte 2 is TRUE, an embedded NUL ends a string, int is signed */
	begin("literals", 0);
	op(OP_IF), op(OP_AND), op(OP_EQUALS), num(-7), dbl(OPERAND_FLOAT, DATUM_DOUBLE, -7.0),
		boolean(2);
	{
		size_t b = block_begin();
		op(OP_RETURN), op(OP_ADD), strn("a\0b", 3), op(OP_ADD), val16(OPERAND_TOKEN, 3), num(0);
		block_end(b);
	}
	s_literals = end();

	/* extensions (later engines): while, loop limit, calls and methods through variables */
	begin("extensions", 1);
	op(OP_DECLARE_LOCALS), d16(3);
	op(OP_ASSIGN), T(1), num(0);
	op(OP_ASSIGN), T(2), str("x");
	op(OP_WHILE), op(OP_LESS), L(1), num(5);
	{
		size_t b = block_begin();
		op(OP_ASSIGN), T(1), op(OP_ADD), L(1), num(1);
		op(OP_ASSIGN), T(2), op(OP_ADD), L(2), str("?\?/\"\\");
		block_end(b);
	}
	op(OP_WHILE), boolean(1);
	block_end(block_begin());
	op(OP_ASSIGN), T(1), op(OP_MODULO), op(OP_MULTIPLY), L(1);
	op(OP_CALL_FUNCTION_IN_VARIABLE), d16(1), val16(OPERAND_FUNCTION, 0x69);
	dbl(OPERAND_FLOAT, DATUM_DOUBLE, 16.0);
	num(7);
	op(OP_ASSIGN), T(3), op(OP_CALL_METHOD_IN_VARIABLE), d16(0), val16(OPERAND_METHOD, 0xf9), val16(OPERAND_ACTOR, 0);
	op(OP_IF), op(OP_AND), op(OP_EQUALS), L(1), num(6), op(OP_EQUALS), L(3), op(OP_NOP);
	{
		size_t b = block_begin();
		op(OP_RETURN), L(2);
		block_end(b);
	}
	op(OP_RETURN), str("wrong");
	s_ext = end();
}

/* ---- running ---- */

static MsValue V[13];

static void make_values(void) {
	V[0] = val_empty();
	V[1] = val_float(2.5);
	V[2] = val_float(0);
	V[3] = val_float(-3);
	V[4] = val_bool(1);
	V[5] = val_bool(0);
	V[6] = val_time(1.5);
	V[7] = val_time(0);
	V[8] = val_token(5);
	V[9] = val_token(9);
	V[10] = val_actor(7);
	V[11] = val_string("ab");
	V[12] = val_string("b");
}

static int same(const MsValue *a, const MsValue *b) {
	if (a->type != b->type)
		return 0;
	switch (a->type) {
	case VAL_FLOAT:
	case VAL_TIME:
		return (isnan(a->u.d) && isnan(b->u.d)) || memcmp(&a->u.d, &b->u.d, sizeof(double)) == 0;
	case VAL_BOOL:
		return a->u.b == b->u.b;
	case VAL_STRING:
		return !strcmp(val_as_string(a), val_as_string(b));
	case VAL_EMPTY:
		return 1;
	}
	return a->u.u == b->u.u;
}

static int runs, fails;

/* runs a script interpreted and native with copies of the arguments; returns the interpreted
 * result (caller frees) */
static MsValue run2(int si, const MsValue *args, int nargs) {
	Script *s = &scripts[si];
	MsValue r[2];
	for (int mode = 0; mode < 2; mode++) {
		MsValue a[4];
		for (int i = 0; i < nargs; i++)
			a[i] = val_copy(&args[i]);
		script_interpret = mode == 0;
		script_extensions = s->ext;
		/* the same random sequence for both runs */
		script_random_seed(12345);
		r[mode] = script_run(0, buf + s->off, (uint32_t)s->size, a, nargs);
		for (int i = 0; i < nargs; i++)
			val_free(&a[i]);
	}
	runs++;
	if (!same(&r[0], &r[1])) {
		fails++;
		printf("DIFFERENT %s(", s->name);
		for (int i = 0; i < nargs; i++)
			printf("%s%s", i ? ", " : "", val_debug(&args[i]));
		printf("): interpreted %s, native %s\n", val_debug(&r[0]), val_debug(&r[1]));
	}
	val_free(&r[1]);
	script_extensions = 0;
	return r[0];
}

/* an expected result from LIONKING.EXE */
static void expect(const char *what, MsValue got, const MsValue want) {
	runs++;
	if (!same(&got, &want)) {
		fails++;
		printf("WRONG %s: %s, LIONKING.EXE gives %s\n", what, val_debug(&got), val_debug(&want));
	}
	val_free(&got);
}

static MsValue binop(int o, int x, int y) {
	MsValue a[2] = {V[x], V[y]};
	return run2(s_binop[o - OP_OR], a, 2);
}

static MsValue run_with(int si, MsValue x, MsValue y, int nargs) {
	MsValue a[2] = {x, y};
	MsValue r = run2(si, a, nargs);
	val_free(&a[0]);
	val_free(&a[1]);
	return r;
}

static MsFunction fn_add, fn_rec;

int main(int argc, char **argv) {
	assemble();
	if (argc > 2 && !strcmp(argv[1], "gen")) {
		const char *idents[96];
		const uint8_t *codes[96];
		uint32_t sizes[96];
		for (int i = 0; i < nscripts; i++) {
			idents[i] = scripts[i].name;
			codes[i] = buf + scripts[i].off;
			sizes[i] = (uint32_t)scripts[i].size;
		}
		int ok = transpile_scripts(argv[2], nscripts, idents, codes, sizes);
		printf("selftest: %d scripts, %d translated\n", nscripts, ok);
		return ok == nscripts ? 0 : 1;
	}
	make_values();
	fn_add.id = F_ADD;
	fn_add.code = buf + scripts[f_add].off;
	fn_add.size = (uint32_t)scripts[f_add].size;
	fn_rec.id = F_REC;
	fn_rec.code = buf + scripts[f_rec].off;
	fn_rec.size = (uint32_t)scripts[f_rec].size;
	ms_god.functions[F_ADD] = &fn_add;
	ms_god.functions[F_REC] = &fn_rec;

	/* every binary operator on every pair of value types */
	for (int o = OP_OR; o <= OP_MODULO; o++)
		for (int x = 0; x < 13; x++)
			for (int y = 0; y < 13; y++) {
				MsValue r = binop(o, x, y);
				val_free(&r);
			}
	for (int x = 0; x < 13; x++) {
		MsValue a[1] = {V[x]};
		MsValue r = run2(s_negate, a, 1);
		val_free(&r);
	}
	/* spot checks against the original */
	expect("2.5 + empty", binop(OP_ADD, 1, 0), val_float(2.5));
	expect("time 1.5 + 2.5", binop(OP_ADD, 6, 1), val_time(0));
	expect("2.5 * time 1.5", binop(OP_MULTIPLY, 1, 6), val_time(3.75));
	expect("time 1.5 + time 0", binop(OP_ADD, 6, 7), val_time(1.5));
	expect("time 1.5 / time 1.5", binop(OP_DIVIDE, 6, 6), val_float(1));
	expect("time 1.5 / time 0", binop(OP_DIVIDE, 6, 7), val_time(0));
	expect("2.5 / 0", binop(OP_DIVIDE, 1, 2), val_float(0));
	expect("-3 % 2.5", binop(OP_MODULO, 3, 1), val_float(fmod(-3, 2.5)));
	expect("\"ab\" + \"b\"", binop(OP_ADD, 11, 12), val_string("abb"));
	expect("\"ab\" - \"b\"", binop(OP_SUBTRACT, 11, 12), val_string(""));
	expect("TRUE + TRUE", binop(OP_ADD, 4, 4), val_empty());
	expect("\"ab\" < \"b\"", binop(OP_LESS, 11, 12), val_bool(1));
	expect("token 5 < token 9", binop(OP_LESS, 8, 9), val_bool(1));
	expect("token 9 > token 5", binop(OP_GREATER, 9, 8), val_bool(0));
	expect("time 1.5 == 2.5 (as floats: 0 == 2.5)", binop(OP_EQUALS, 6, 1), val_bool(0));
	expect("time 0 == 0", binop(OP_EQUALS, 7, 2), val_bool(1));
	expect("empty == empty", binop(OP_EQUALS, 0, 0), val_bool(1));
	expect("empty <= empty", binop(OP_LESS_EQUAL, 0, 0), val_bool(0));
	expect("actor 7 < actor 7", binop(OP_LESS, 10, 10), val_bool(0));
	expect("empty == 0 (as floats)", binop(OP_EQUALS, 0, 2), val_bool(1));
	expect("2.5 || TRUE", binop(OP_OR, 1, 4), val_bool(1));
	expect("TRUE && 2.5", binop(OP_AND, 4, 1), val_bool(0));
	expect("TRUE ^ FALSE", binop(OP_XOR, 4, 5), val_bool(1));
	{
		MsValue a[1] = {V[10]};
		expect("-actor", run2(s_negate, a, 1), val_empty());
		a[0] = V[6];
		expect("-time 1.5", run2(s_negate, a, 1), val_time(-1.5));
	}

	/* variables */
	for (int x = 0; x < 13; x++) {
		MsValue a[2] = {V[x], V[11]};
		MsValue r = run2(s_vars, a, 2);
		val_free(&r);
	}
	{
		MsValue a[2] = {V[12], V[11]};
		expect("vars", run2(s_vars, a, 2), val_string("ok"));
	}
	/* if / if-else / return */
	for (int x = 0; x < 13; x++) {
		MsValue a[1] = {V[x]};
		MsValue r = run2(s_if, a, 1);
		val_free(&r);
		r = run2(s_if_nested, a, 1);
		val_free(&r);
		r = run2(s_return, a, 1);
		val_free(&r);
		if (V[x].type == VAL_BOOL) {
			r = run2(s_ifelse, a, 1);
			val_free(&r);
		}
	}
	{
		MsValue a[1] = {V[4]};
		expect("if TRUE", run2(s_if, a, 1), val_float(12));
		a[0] = V[5];
		expect("if FALSE", run2(s_if, a, 1), val_float(11));
		a[0] = V[1];
		expect("if 2.5 (two blocks skipped: the script ends)", run2(s_if, a, 1), val_empty());
		expect("if 2.5 in an if-else", run2(s_if_nested, a, 1), val_empty());
		a[0] = V[4];
		expect("if TRUE in an if-else", run2(s_if_nested, a, 1), val_float(2));
		expect("if-else TRUE", run2(s_ifelse, a, 1), val_string("yes"));
		a[0] = V[1];
		expect("return from a nested block", run2(s_return, a, 1), val_string("deep"));
		a[0] = V[3];
		expect("return without value", run2(s_return, a, 1), val_empty());
	}

	/* built-ins */
	for (int x = 0; x < 13; x++)
		for (int y = 0; y < 13; y++) {
			MsValue a[2] = {V[x], V[y]};
			MsValue r = run2(s_builtin[0], a, 2);
			val_free(&r);
			r = run2(s_builtin[5], a, 2);
			val_free(&r);
		}
	for (int x = 0; x < 13; x++) {
		MsValue a[2] = {V[x], V[x]};
		for (int k = 1; k <= 7; k++) {
			MsValue r = run2(s_builtin[k], a, k == 6 ? 2 : 1);
			val_free(&r);
		}
	}
	expect("Platform()", run2(s_builtin[2], NULL, 0), val_float(3));
	expect("SquareRoot()", run2(s_builtin[4], NULL, 0), val_empty());
	expect("SquareRoot(-3)", run_with(s_builtin[3], val_float(-3), val_empty(), 1), val_float(0));
	expect("SquareRoot(time 4)", run_with(s_builtin[3], val_time(4), val_empty(), 1), val_float(0));
	expect("SquareRoot(6.25)", run_with(s_builtin[3], val_float(6.25), val_empty(), 1), val_float(2.5));
	expect("TimeOfDay(x): engine clock", run_with(s_builtin[1], val_float(1), val_empty(), 1), val_time(0));
	expect("Random(token, token)", run_with(s_builtin[0], val_token(1), val_token(2), 2), val_empty());
	expect("Random with 3 arguments", run_with(s_builtin[6], val_float(1), val_float(9), 2), val_empty());
	expect("missing function 0x6b", run2(s_builtin[7], NULL, 0), val_empty());
	{
		/* the generator of 1188:d6c2 from state 12345 */
		double rnd = (double)((12345u * 4096u + 150889u) % 714025u);
		expect("Random(1, 9)", run_with(s_builtin[0], val_float(1), val_float(9), 2),
			val_float(floor((9.99 - 1) * rnd / 714024 + 1)));
		expect("Random(1.7, -1.7) (truncated)", run_with(s_builtin[0], val_float(1.7), val_float(-1.7), 2),
			val_float(floor((1.99 + 1) * rnd / 714024 - 1)));
		expect("Random(FALSE, TRUE)", run_with(s_builtin[0], val_bool(0), val_bool(1), 2),
			val_bool(floor(1.99 * rnd / 714024) == 1.0));
		expect("GetUniqueRandom(1, 6, 3, 5)", run_with(s_builtin[5], val_float(1), val_float(6), 2),
			val_float((double[]){1, 2, 4, 6}[(int)floor(4 * rnd / 714024)]));
		expect("GetUniqueRandom(TRUE, ...)", run_with(s_builtin[5], val_bool(1), val_float(6), 2), val_empty());
	}

	/* title functions and the limit of 15 */
	for (int n = 0; n <= 20; n++) {
		MsValue r = run_with(s_calls, val_float(n), val_empty(), 1);
		/* rec(0) is the 15th running function for n = 14; beyond, rec gives empty and
		 * float + empty counts the empty as 0 */
		MsValue want = n <= 14 ? val_float(n + 1 + n) : val_float(n + 1);
		char what[64];
		snprintf(what, sizeof(what), "add(%d, 1) + rec(%d)", n, n);
		expect(what, r, want);
	}
	/* methods */
	for (int x = 0; x < 13; x++) {
		MsValue a[1] = {V[x]};
		MsValue r = run2(s_methods, a, 1);
		val_free(&r);
	}
	expect("literals", run2(s_literals, NULL, 0), val_empty());
	{
		/* "a" + (token 3 + 0) is empty: token arithmetic does nothing; "a\0b" is "a" */
		MsValue a[1] = {val_empty()};
		MsValue r = run2(s_ext, a, 1);
		expect("extensions", r, val_string("x?\?/\"\\?\?/\"\\?\?/\"\\?\?/\"\\?\?/\"\\"));
	}

	printf("selftest: %d scripts, %d comparisons, %d failed\n", nscripts, runs, fails);
	for (int i = 0; i < 13; i++)
		val_free(&V[i]);
	return fails != 0;
}
