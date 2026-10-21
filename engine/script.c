#include "script.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "actor.h"
#include "constants.h"
#include "god.h"
#include "runtime.h"
#include "script_rt.h"

/* The script interpreter of LIONKING.EXE (segment 1198, "CodeChunk"): every behaviour below
 * carries the address it was taken from; docs\ORIGINAL_SCRIPT.md has the whole picture. */

int script_trace = 0;
int script_interpret = 0;
int script_extensions = 0;

/* 1198:dc7c: a title function runs only while fewer than 15 are running (DS:03B6 < 0xF);
 * otherwise nothing happens and the result is empty. Responses are not counted (1198:86a4). */
#define MAX_CALL_DEPTH 15

typedef struct {
	const uint8_t *code;
	size_t pos, end;
	MsFrame *fr;
	int ret;        /* +0x2C of the frame: a return was executed */
	MsValue retval; /* +0x08 */
} Exec;

static int call_depth;

static MsValue eval(Exec *e);
static MsValue eval_type(Exec *e, uint16_t type);

/* ---- bytecode reads of the original (1188:e95a u16, 1188:ea02 u32, 1188:e8ea u8, ...): the
 * two-byte datum type is read and ignored, never checked. Past the end of the script the
 * original would read whatever memory follows; here the missing bytes read as 0 (so a block
 * ends) and the position stops at the end. */

static void rd_raw(Exec *e, void *dst, size_t n) {
	size_t avail = e->pos < e->end ? e->end - e->pos : 0;
	size_t k = n < avail ? n : avail;
	memcpy(dst, e->code + e->pos, k);
	memset((uint8_t *)dst + k, 0, n - k);
	e->pos += k;
}

static void rd_tag(Exec *e) {
	uint8_t t[2];
	rd_raw(e, t, 2);
}

static uint8_t rd8(Exec *e) {
	uint8_t v;
	rd_tag(e);
	rd_raw(e, &v, 1);
	return v;
}

static uint16_t rd16(Exec *e) {
	uint8_t b[2];
	rd_tag(e);
	rd_raw(e, b, 2);
	return (uint16_t)(b[0] | b[1] << 8);
}

static uint32_t rd32(Exec *e) {
	uint8_t b[4];
	rd_tag(e);
	rd_raw(e, b, 4);
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static double rd_double(Exec *e) {
	double d;
	rd_tag(e);
	rd_raw(e, &d, 8);
	return d;
}

/* ---- blocks ---- */

/* 1198:dc48: skips a block (u32 size, then that many bytes) */
static void skip_block(Exec *e) {
	uint32_t n = rd32(e);
	e->pos = n < e->end - e->pos ? e->pos + n : e->end;
}

/* the loop of 1198:dda4: expressions until type 0 or a return; the next type is read after
 * every expression, even after a return */
static void block_loop(Exec *e) {
	uint16_t type = rd16(e);
	while (type != EXPR_EMPTY && !e->ret) {
		MsValue v = eval_type(e, type);
		val_free(&v);
		type = rd16(e);
	}
}

/* 1198:dda4: the block size is read and not used (no check of the bytes read) */
static void exec_block(Exec *e) {
	(void)rd32(e);
	block_loop(e);
}

/* ---- variables and values ---- */

/* 1198:e970: NULL if there is no such variable. With id 0 nothing more is read, not even the
 * index expression of an indirect parameter. */
static MsValue *variable_ref(Exec *e) {
	uint16_t id = rd16(e);
	uint16_t scope = rd16(e);
	if (id == 0)
		return NULL;
	if (scope == SCOPE_INDIRECT_PARAMETER)
		return rs_var_indirect(e->fr, id, eval(e));
	return rs_var(e->fr, id, scope);
}

/* 1198:df20 (jump table 1198:e144) */
static MsValue eval_value(Exec *e) {
	uint16_t t = rd16(e);
	switch (t) {
	case OPERAND_BOOL:
		/* CMP b,1 / SBB / INC: any byte but 0 is TRUE */
		return val_bool(rd8(e) >= 1);
	case OPERAND_FLOAT:
		return val_float(rd_double(e));
	case OPERAND_INT: {
		uint32_t v = rd32(e);
		return val_float((int32_t)v); /* FILD dword */
	}
	case OPERAND_STRING: {
		uint16_t n = rd16(e);
		char *s = malloc((size_t)n + 1);
		rd_raw(e, s, n);
		MsValue v = rs_string(s, n);
		free(s);
		return v;
	}
	case OPERAND_TOKEN:
		return val_token(rd16(e));
	case OPERAND_ACTOR:
		return val_actor(rd16(e));
	case OPERAND_TIME:
		return val_time(rd_double(e));
	case OPERAND_VARIABLE: {
		rs_extension("operand 0x9e (serialized value)");
		MsChunk c;
		memset(&c, 0, sizeof(c));
		c.base = e->code;
		c.pos = e->pos;
		c.end = e->end;
		MsValue v = val_read(&c);
		e->pos = c.pos;
		return v;
	}
	case OPERAND_FUNCTION:
		rs_extension("operand 0x9f (function id)");
		return val_function(rd16(e));
	case OPERAND_METHOD:
		rs_extension("operand 0xa0 (method id)");
		return val_method(rd16(e));
	}
	ms_fatal("script: unknown operand type 0x%x (LIONKING.EXE 1198:df20 throws an exception)", t);
	return val_empty();
}

/* ---- calls ---- */

static MsValue call_function_args(Exec *e, uint32_t id, int n) {
	/* 1198:e642: the arguments are evaluated in order, then the function is looked up */
	MsValue *args = n ? calloc((size_t)n, sizeof(MsValue)) : NULL;
	for (int i = 0; i < n; i++)
		args[i] = eval(e);
	MsValue r = rs_call(id, args, n);
	free(args);
	return r;
}

static MsValue collection_method(MsCollection *c, uint32_t m, MsValue *args, int nargs);

MsValue script_call_method(uint16_t caller, MsValue *target, uint32_t m, MsValue *args, int n) {
	(void)caller;
	if (target->type == VAL_ACTOR) {
		/* 1198:e72e: the actor map (DS:0326, vtable +4) gives the actor, then its method
		 * handler (vtable +4); no actor: nothing happens */
		uint32_t id = target->u.u;
		MsActor *a = id ? god_actor((uint16_t)id) : NULL;
		if (!a) {
			if (id)
				ms_warn("script (actor %u): method 0x%x on actor %u that is not loaded (ignored, as in "
					"the original)", caller, m, id);
			return val_empty();
		}
		return actor_call_method(a, m, args, n);
	}
	if (target->type == VAL_COLLECTION && script_extensions)
		return collection_method(target->coll, m, args, n);
	/* 1198:e72e: a method on anything but an actor does nothing */
	return val_empty();
}

static MsValue call_method_args(Exec *e, uint32_t m, int n) {
	/* 1198:e72e: the target is evaluated before the arguments. A variable target keeps its
	 * address only for the collection methods of script_extensions (they change the
	 * variable); the original evaluates it as a value. */
	uint16_t type = rd16(e);
	MsValue *p = NULL;
	MsValue target;
	if (type == EXPR_VARIABLE) {
		p = variable_ref(e);
		target = rs_get(p);
	} else {
		target = eval_type(e, type);
	}
	MsValue *args = n ? calloc((size_t)n, sizeof(MsValue)) : NULL;
	for (int i = 0; i < n; i++)
		args[i] = eval(e);
	MsValue r = type == EXPR_VARIABLE ? rs_method_var(e->fr, m, p, target, args, n)
	                                  : rs_method_value(e->fr, m, target, args, n);
	free(args);
	return r;
}

/* ---- operations: 1198:e158 ---- */

static MsValue eval_operation(Exec *e) {
	uint16_t op = rd16(e);
	switch (op) {
	case OP_NOP:
		/* table entry 0xC8 jumps straight to the end: no operands, empty result */
		return val_empty();
	case OP_IF: {
		/* 1198:e2b4: TRUE runs the block; FALSE skips it; a condition that is not a bool skips
		 * a block twice (the block, then whatever follows read as a block size) */
		int k = rs_if(eval(e));
		if (k > 0) {
			exec_block(e);
		} else {
			skip_block(e);
			if (k < 0)
				skip_block(e);
		}
		return val_empty();
	}
	case OP_IF_ELSE: {
		/* 1198:e31a: the else block is skipped even after a return in the true block */
		if (rs_if_else(eval(e))) {
			exec_block(e);
			skip_block(e);
		} else {
			skip_block(e);
			exec_block(e);
		}
		return val_empty();
	}
	case OP_ASSIGN: {
		/* 1198:e3e0: the variable is resolved before the value is evaluated */
		MsValue *target = variable_ref(e);
		MsValue v = eval(e);
		return rs_assign(target, v);
	}
	case OP_OR:
	case OP_XOR:
	case OP_AND: {
		/* 1198:e454: both operands are always evaluated, first then second */
		MsValue a = eval(e);
		MsValue b = eval(e);
		return rs_logic(op, a, b);
	}
	case OP_EQUALS:
	case OP_NOT_EQUALS:
	case OP_LESS:
	case OP_GREATER:
	case OP_LESS_EQUAL:
	case OP_GREATER_EQUAL: {
		MsValue a = eval(e);
		MsValue b = eval(e);
		return rs_compare(op, a, b);
	}
	case OP_ADD:
	case OP_SUBTRACT:
	case OP_MULTIPLY:
	case OP_DIVIDE:
	case OP_MODULO: {
		MsValue a = eval(e);
		MsValue b = eval(e);
		return rs_math(op, a, b);
	}
	case OP_NEGATE:
		/* 1198:e602 */
		return rs_negate(eval(e));
	case OP_CALL_FUNCTION: {
		uint16_t id = rd16(e);
		uint16_t n = rd16(e);
		return call_function_args(e, id, n);
	}
	case OP_CALL_METHOD: {
		uint16_t m = rd16(e);
		uint16_t n = rd16(e);
		return call_method_args(e, m, n);
	}
	case OP_DECLARE_LOCALS:
		rs_locals(e->fr, rd16(e));
		return val_empty();
	case OP_RETURN:
		/* 1198:e93c: the value goes straight into the frame result */
		val_free(&e->retval);
		e->retval = eval(e);
		e->ret = 1;
		return val_empty();
	case OP_RETURN_NO_VALUE:
		/* 1198:e960 */
		e->ret = 1;
		return val_empty();
	case OP_WHILE: {
		rs_extension("opcode 0xe0 (while)");
		size_t top = e->pos;
		int iterations = 0;
		for (;;) {
			e->pos = top;
			MsValue c = eval(e);
			int run = rs_while(e->fr, &c, &iterations);
			val_free(&c);
			if (!run) {
				skip_block(e);
				break;
			}
			exec_block(e);
			if (e->ret)
				break;
		}
		return val_empty();
	}
	case OP_CALL_FUNCTION_IN_VARIABLE: {
		rs_extension("opcode 0xe1 (call through a variable)");
		uint16_t n = rd16(e);
		uint32_t id = rs_function_id(eval(e));
		return call_function_args(e, id, n);
	}
	case OP_CALL_METHOD_IN_VARIABLE: {
		rs_extension("opcode 0xe2 (method through a variable)");
		uint16_t n = rd16(e);
		uint32_t m = rs_method_id(eval(e));
		return call_method_args(e, m, n);
	}
	}
	ms_fatal("script: unknown opcode 0x%x (LIONKING.EXE 1198:e158 throws an exception)", op);
	return val_empty();
}

/* 1198:de5e */
static MsValue eval_type(Exec *e, uint16_t type) {
	switch (type) {
	case EXPR_OPERATION:
		return eval_operation(e);
	case EXPR_VALUE:
		return eval_value(e);
	case EXPR_VARIABLE:
		/* 1198:defa: no variable leaves the result empty */
		return rs_get(variable_ref(e));
	}
	ms_fatal("script: unknown expression type 0x%x (LIONKING.EXE 1198:de5e throws an exception)", type);
	return val_empty();
}

/* 1198:de2c */
static MsValue eval(Exec *e) {
	return eval_type(e, rd16(e));
}

static MsValue interpret(MsFrame *fr) {
	Exec e;
	memset(&e, 0, sizeof(e));
	e.code = fr->code;
	e.end = fr->size;
	e.fr = fr;
	exec_block(&e);
	return e.retval;
}

MsValue rs_resume(MsFrame *f, uint32_t pos, const char *cont) {
	Exec e;
	memset(&e, 0, sizeof(e));
	e.code = f->code;
	e.pos = pos;
	e.end = f->size;
	e.fr = f;
	skip_block(&e); /* the second skip of 1198:e2b4 */
	for (const char *k = cont; *k; k++) {
		block_loop(&e);
		if (e.ret)
			break;
		if (*k == 't')
			skip_block(&e); /* 1198:e31a after its true block */
	}
	return e.retval;
}

/* ---- native (statically recompiled) scripts ---- */

uint64_t script_hash(const uint8_t *code, uint32_t size) {
	uint64_t h = 0xcbf29ce484222325ull; /* FNV-1a 64 */
	for (uint32_t i = 0; i < size; i++) {
		h ^= code[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

static struct {
	int enabled;
	long native, interpreted;
} stats;

static void print_stats(void) {
	fprintf(stderr, "scripts: %ld native, %ld interpreted (%d compiled scripts)\n", stats.native,
		stats.interpreted, ms_native_count);
}

static MsNativeFn native_find(const uint8_t *code, uint32_t size) {
	uint64_t h = script_hash(code, size);
	int lo = 0, hi = ms_native_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		const MsNativeScript *s = &ms_native_scripts[mid];
		if (s->hash == h && s->size == size)
			return s->fn;
		if (s->hash < h || (s->hash == h && s->size < size))
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return NULL;
}

MsValue script_run(uint16_t actor_id, const uint8_t *code, uint32_t size, MsValue *args, int nargs) {
	static int first = 1;
	if (first) {
		first = 0;
		const char *s = getenv("MS_SCRIPT_STATS");
		if (s && *s && *s != '0') {
			stats.enabled = atoi(s) > 1 ? 2 : 1;
			atexit(print_stats);
		}
	}
	MsFrame fr;
	memset(&fr, 0, sizeof(fr));
	fr.args = args;
	fr.nargs = nargs;
	fr.actor = actor_id;
	fr.code = code;
	fr.size = size;
	MsNativeFn fn = script_interpret ? NULL : native_find(code, size);
	MsValue r;
	if (!fn) {
		stats.interpreted++;
		if (stats.enabled > 1 && !script_interpret)
			fprintf(stderr, "scripts: no native code for actor %u (%u bytes, hash %016llx)\n", actor_id,
				size, (unsigned long long)script_hash(code, size));
		r = interpret(&fr);
	} else {
		stats.native++;
		r = fn(&fr);
	}
	rs_frame_free(&fr);
	return r;
}

/* ---- collections (not in LIONKING.EXE: its values have no collection type) ---- */

static int coll_sort_cmp(const void *x, const void *y) {
	const MsValue *a = x, *b = y;
	if (val_compare(OP_LESS, a, b))
		return -1;
	if (val_compare(OP_LESS, b, a))
		return 1;
	return 0;
}

static MsValue collection_method(MsCollection *c, uint32_t m, MsValue *args, int nargs) {
	switch (m) {
	case M_APPEND:
		for (int i = 0; i < nargs; i++)
			coll_push(c, &args[i]);
		return val_empty();
	case M_APPLY: {
		if (nargs < 1)
			return val_empty();
		uint32_t f = val_as_function(&args[0]);
		/* the collection may change while the function runs: iterate over a copy */
		MsCollection copy = {0};
		for (int i = 0; i < c->count; i++)
			coll_push(&copy, &c->items[i]);
		for (int i = 0; i < copy.count; i++) {
			MsValue *a = calloc((size_t)nargs, sizeof(MsValue));
			a[0] = val_copy(&copy.items[i]);
			for (int k = 1; k < nargs; k++)
				a[k] = val_copy(&args[k]);
			MsValue r = script_call_function(f, a, nargs);
			val_free(&r);
			for (int k = 0; k < nargs; k++)
				val_free(&a[k]);
			free(a);
		}
		coll_clear(&copy);
		free(copy.items);
		return val_empty();
	}
	case M_COUNT:
		return val_float(c->count);
	case M_DELETE_FIRST:
		if (c->count > 0)
			return coll_remove(c, 0);
		ms_warn("collection: delete first of an empty collection");
		return val_empty();
	case M_DELETE_LAST:
		if (c->count > 0)
			return coll_remove(c, c->count - 1);
		ms_warn("collection: delete last of an empty collection");
		return val_empty();
	case M_EMPTY:
		coll_clear(c);
		return val_empty();
	case M_GET_AT: {
		if (nargs != 1)
			return val_empty();
		int i = (int)(unsigned)val_as_float(&args[0]);
		if (i >= 0 && i < c->count)
			return val_copy(&c->items[i]);
		ms_warn("collection: index %d out of bounds %d", i, c->count);
		return val_float(0);
	}
	case M_IS_EMPTY:
		return val_bool(c->count == 0);
	case M_JUMBLE:
		for (int i = c->count - 1; i > 0; i--) {
			int j = (int)(rt_random() % (uint32_t)c->count);
			MsValue t = c->items[i];
			c->items[i] = c->items[j];
			c->items[j] = t;
		}
		return val_empty();
	case M_SEEK:
		if (nargs != 1)
			return val_float(-1);
		for (int i = c->count - 1; i >= 0; i--)
			if (c->items[i].type == args[0].type && val_compare(OP_EQUALS, &args[0], &c->items[i]))
				return val_float(i);
		return val_float(-1);
	case M_SEND: {
		if (nargs < 1)
			return val_empty();
		uint32_t mth = val_as_method(&args[0]);
		MsCollection copy = {0};
		for (int i = 0; i < c->count; i++)
			coll_push(&copy, &c->items[i]);
		for (int i = 0; i < copy.count; i++) {
			MsActor *a = god_actor((uint16_t)val_as_actor(&copy.items[i]));
			if (a) {
				MsValue r = actor_call_method(a, mth, args + 1, nargs - 1);
				val_free(&r);
			}
		}
		coll_clear(&copy);
		free(copy.items);
		return val_empty();
	}
	case M_DELETE_AT: {
		if (nargs != 1)
			return val_empty();
		unsigned i = (unsigned)val_as_float(&args[0]);
		if ((int)i < c->count)
			return coll_remove(c, (int)i);
		ms_warn("collection: delete at %u out of bounds %d", i, c->count);
		return val_empty();
	}
	case M_INSERT_AT: {
		if (nargs != 2)
			return val_empty();
		unsigned i = (unsigned)val_as_float(&args[1]);
		if ((int)i <= c->count)
			coll_insert(c, (int)i, &args[0]);
		else
			ms_warn("collection: insert at %u out of bounds %d", i, c->count);
		return val_empty();
	}
	case M_REPLACE_AT: {
		if (nargs != 2)
			return val_empty();
		unsigned i = (unsigned)val_as_float(&args[1]);
		if ((int)i < c->count)
			val_assign(&c->items[i], &args[0]);
		else
			ms_warn("collection: replace at %u out of bounds %d", i, c->count);
		return val_empty();
	}
	case M_PREPEND_LIST:
		for (int i = nargs - 1; i >= 0; i--)
			coll_insert(c, 0, &args[i]);
		return val_empty();
	case M_SORT:
		qsort(c->items, (size_t)c->count, sizeof(MsValue), coll_sort_cmp);
		return val_empty();
	}
	ms_warn("collection: method 0x%x not implemented", m);
	return val_empty();
}

/* ---- built-in functions of LIONKING.EXE ---- */

/* The random generator of the engine (1188:d6c2): state = (state * 4096 + 150889) % 714025
 * (DS:01EA, DS:01E2, DS:01E6, DS:01DE); 1188:d69c gives the largest value, 714024. The original
 * seeds it once from bits of the local time (1188:d6aa, called near 1188:ddee); here the seed
 * comes from rt_random(), so that --seed gives a fixed sequence. */
#define LCG_M 714025u
static uint32_t lcg_state;
static int lcg_seeded;

uint32_t script_random(void) {
	if (!lcg_seeded) {
		lcg_seeded = 1;
		lcg_state = rt_random() % LCG_M;
	}
	lcg_state = (lcg_state * 4096u + 150889u) % LCG_M;
	return lcg_state;
}

uint32_t script_random_max(void) {
	return LCG_M - 1;
}

void script_random_seed(uint32_t seed) {
	/* 1188:d6aa: state = seed % 714025 */
	lcg_seeded = 1;
	lcg_state = seed % LCG_M;
}

/* Random(min, max), 1198:eb9a */
static MsValue bi_random(MsValue *args, int nargs) {
	if (nargs != 2)
		return val_empty();
	uint32_t rnd = script_random(); /* drawn before the type check */
	double lo, hi;
	int integer = 1, type = args[0].type;
	switch (type) {
	case VAL_FLOAT:
		/* 1180:1294 (modf): integer parts, truncated toward zero */
		lo = trunc(val_float_of(&args[0]));
		hi = trunc(val_float_of(&args[1]));
		break;
	case VAL_BOOL:
		lo = val_bool_of(&args[0]) == 1;
		hi = val_bool_of(&args[1]) == 1;
		break;
	case VAL_TIME:
		lo = val_time_of(&args[0]);
		hi = val_time_of(&args[1]);
		integer = 0;
		break;
	default:
		return val_empty();
	}
	if (hi < lo) {
		double t = hi;
		hi = lo;
		lo = t;
	}
	if (integer)
		hi = (double)(0.99L + hi); /* DS:2FE6 */
	/* x87: (hi - lo) * rnd stored as a double, / 714024 + lo stored as a double, then floor
	 * (1180:113e, rounding down) */
	double scaled = (double)(((long double)hi - lo) * (long double)rnd);
	double r = (double)((long double)scaled / (long double)script_random_max() + lo);
	if (integer)
		r = floor(r);
	if (type == VAL_FLOAT)
		return val_float(r);
	if (type == VAL_BOOL)
		return val_bool(r == 1.0);
	return val_time(r);
}

/* GetUniqueRandom(min, max, excluded...), 1198:ee58 */
static MsValue bi_unique_random(MsValue *args, int nargs) {
	/* every argument must be a float, otherwise nothing happens (and no number is drawn) */
	for (int i = 0; i < nargs; i++)
		if (args[i].type != VAL_FLOAT)
			return val_empty();
	uint32_t rnd = script_random();
	/* with fewer than two arguments the original reads past them (the extra empty value of
	 * 1198:e642, then garbage): here missing arguments count 0.0 */
	double lo = trunc(nargs > 0 ? args[0].u.d : 0.0), hi = trunc(nargs > 1 ? args[1].u.d : 0.0);
	if (hi < lo) {
		double t = hi;
		hi = lo;
		lo = t;
	}
	double nexcl = (double)nargs - 2.0;
	hi = (double)(0.99L + hi);
	int32_t cap = ms_ftol((double)((long double)hi - lo + 1.0L));
	if (cap <= 0)
		cap = 1;
	double *list = malloc(sizeof(double) * (size_t)cap);
	int32_t count = 0;
	for (double v = lo; v <= hi && count < cap; v += 1.0) {
		int unique = 1;
		for (double k = 0.0; k < nexcl; k += 1.0) {
			int32_t i = ms_ftol(2.0 + k);
			if (i >= 0 && i < nargs && args[i].u.d == v) {
				unique = 0;
				break;
			}
		}
		if (unique)
			list[count++] = v;
	}
	MsValue r = val_empty();
	if (count) {
		double scaled = (double)((long double)count * (long double)rnd);
		int32_t i = ms_ftol(floor((double)((long double)scaled / (long double)script_random_max())));
		/* i == count (rnd = 714024) reads one entry past the list in the original (an
		 * uninitialized value): here the last entry */
		if (i >= count)
			i = count - 1;
		if (i < 0)
			i = 0;
		r = val_float(list[i]);
	}
	free(list);
	return r;
}

/* TimeOfDay, 1198:f11c */
static MsValue bi_time_of_day(int nargs) {
	/* with exactly one argument: the running time of the engine clock (1190:05a4, DS:025A) */
	if (nargs == 1)
		return val_time(rt_now() / 1000.0);
	/* otherwise seconds since midnight, local time (1188:e440) */
	time_t t = time(NULL);
	struct tm *lt = t != (time_t)-1 ? localtime(&t) : NULL;
	return val_time(lt ? (lt->tm_hour * 60 + lt->tm_min) * 60 + lt->tm_sec : 0);
}

/* The functions registered by LIONKING.EXE, looked up before the title functions (1198:814c
 * takes the first function of the list with the id: these are registered first). */
static int builtin(uint32_t id, MsValue *args, int nargs, MsValue *r) {
	*r = val_empty();
	switch (id) {
	case FN_LEGACY_RANDOM:
		*r = bi_random(args, nargs);
		return 1;
	case FN_LEGACY_TIME_OF_DAY:
		*r = bi_time_of_day(nargs);
		return 1;
	case FN_LEGACY_EFFECT_TRANSITION:
		/* 1190:aef8: the arguments go to the palette manager (DS:033E, vtable +8) */
		video_effect_transition(args, nargs);
		return 1;
	case FN_LEGACY_EFFECT_TRANSITION_ON_SYNC:
		/* 1190:af24 -> 1190:a5d4 */
		video_set_transition_on_sync(args, nargs);
		return 1;
	case FN_LEGACY_PLATFORM:
		/* 1198:f1f4: the float 3.0 (DS:2FFE) */
		*r = val_float(3.0);
		return 1;
	case FN_LEGACY_SQUARE_ROOT:
		/* 1198:f19c: no argument: empty; a negative or non-float argument: 0 */
		if (nargs != 0) {
			double v = val_float_of(&args[0]);
			*r = val_float(v >= 0.0 ? sqrt(v) : 0.0);
		}
		return 1;
	case FN_LEGACY_GET_UNIQUE_RANDOM:
		*r = bi_unique_random(args, nargs);
		return 1;
	}
	return 0;
}

/* Built-in functions of later engines (ScummVM): not in LIONKING.EXE, script_extensions only. */
static int builtin_extension(uint32_t id, MsValue *args, int nargs, MsValue *r) {
	*r = val_empty();
	switch (id) {
	case FN_RANDOM:
	case FN_TIME_OF_DAY:
	case FN_EFFECT_TRANSITION:
	case FN_EFFECT_TRANSITION_ON_SYNC:
	case FN_PLATFORM:
	case FN_SQUARE_ROOT:
	case FN_GET_UNIQUE_RANDOM:
		return builtin(id - FN_RANDOM + FN_LEGACY_RANDOM, args, nargs, r);
	case FN_CURRENT_RUN_TIME:
	case FN_LEGACY_CURRENT_RUN_TIME:
		*r = val_float(rt_now() / 1000.0);
		return 1;
	case FN_SET_GAMMA:
	case FN_LEGACY_SET_GAMMA:
		return 1;
	case FN_GET_DEFAULT_GAMMA:
	case FN_LEGACY_GET_DEFAULT_GAMMA:
	case FN_GET_CURRENT_GAMMA:
	case FN_LEGACY_GET_CURRENT_GAMMA: {
		*r = val_new_collection();
		MsValue one = val_float(1.0);
		for (int i = 0; i < 3; i++)
			coll_push(r->coll, &one);
		return 1;
	}
	case FN_SET_AUDIO_VOLUME:
	case FN_LEGACY_SET_AUDIO_VOLUME:
		if (nargs == 1 && args[0].type == VAL_FLOAT)
			audio_set_volume(args[0].u.d < 0 ? 0 : args[0].u.d > 1 ? 1 : args[0].u.d);
		return 1;
	case FN_GET_AUDIO_VOLUME:
	case FN_LEGACY_GET_AUDIO_VOLUME:
		*r = val_float(audio_get_volume());
		return 1;
	case FN_SYSTEM_LANGUAGE:
	case FN_LEGACY_SYSTEM_LANGUAGE:
		ms_warn("SystemLanguagePreference: not implemented");
		return 1;
	case FN_LEGACY_DEBUG_PRINT:
		fprintf(stderr, "IMT @ clock %u", rt_now());
		for (int i = 0; i < nargs; i++)
			fprintf(stderr, "%s%s", i ? ", " : " ", val_debug(&args[i]));
		fputc('\n', stderr);
		return 1;
	}
	return 0;
}

MsValue script_call_function(uint32_t id, MsValue *args, int nargs) {
	MsValue r;
	if (builtin(id, args, nargs, &r))
		return r;
	MsFunction *f = id < MS_MAX_ID ? ms_god.functions[id] : NULL;
	if (f) {
		/* 1198:f3ba -> 1198:dc7c */
		if (call_depth >= MAX_CALL_DEPTH) {
			if (script_trace)
				fprintf(stderr, "[%u] function %u not run: %d functions running\n", rt_now(), id,
					call_depth);
			return val_empty();
		}
		if (script_trace)
			fprintf(stderr, "[%u] function %u\n", rt_now(), id);
		call_depth++;
		r = script_run(0, f->code, f->size, args, nargs);
		call_depth--;
		return r;
	}
	if (script_extensions && builtin_extension(id, args, nargs, &r))
		return r;
	/* 1198:814c: no function with this id: nothing happens, the result stays empty */
	ms_warn("script: function %u does not exist (empty result, as in the original)", id);
	return val_empty();
}

/* ---- decompiler (debugging aid, mirrors the interpreter) ---- */

typedef struct {
	char *s;
	size_t n, cap;
} Str;

static void sp(Str *o, const char *fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	int k = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (k < 0)
		return;
	if ((size_t)k >= sizeof(buf))
		k = sizeof(buf) - 1;
	if (o->n + (size_t)k + 1 > o->cap) {
		o->cap = (o->n + (size_t)k + 1) * 2;
		o->s = realloc(o->s, o->cap);
	}
	memcpy(o->s + o->n, buf, (size_t)k + 1);
	o->n += (size_t)k;
}

static const char *method_name(unsigned m) {
	switch (m) {
	case 0xc8: return "cursorSet";
	case 0xc9: return "branchToScreen";
	case 0xca: return "show";
	case 0xcb: return "hide";
	case 0xcc: return "moveTo";
	case 0xcd: return "moveBy";
	case 0xce: return "play";
	case 0xcf: return "stop";
	case 0xd0: return "pause";
	case 0xd1: return "resume";
	case 0xd2: return "activate";
	case 0xd3: return "deactivate";
	case 0xd8: return "zMoveTo";
	case 0xd9: return "quit";
	case 0xdb: return "reset";
	case 0xdc: return "setClip";
	case 0xdd: return "nextFrame";
	case 0xde: return "previousFrame";
	case 0xdf: return "zazuStartLooking";
	case 0xe0: return "zazuStopLooking";
	case 0xe1: return "zazuLookDirection";
	case 0xe2: return "minigameReset";
	case 0xe3: return "minigameActivate";
	case 0xe4: return "minigameDeactivate";
	case 0xe5: return "dotHit";
	case 0xe6: return "centerMoveTo";
	case 0xe7: return "zazuDisableAudio";
	case 0xe8: return "zazuEnableAudio";
	case 0xe9: return "leftX";
	case 0xea: return "topY";
	case 0xeb: return "width";
	case 0xec: return "height";
	case 0xed: return "centerX";
	case 0xee: return "centerY";
	case 0xef: return "z";
	case 0xf0: return "clipId";
	case 0xf1: return "setDissolve";
	case 0xf2: return "setStartPoint";
	case 0xf3: return "setEndPoint";
	case 0xf4: return "setTotalSteps";
	case 0xf5: return "setStepRate";
	case 0xf6: return "isPointInside";
	case 0xf7: return "append";
	case 0xf8: return "apply";
	case 0xf9: return "count";
	case 0xfa: return "deleteFirst";
	case 0xfb: return "deleteLast";
	case 0xfc: return "empty";
	case 0xfd: return "getAt";
	case 0xfe: return "isEmpty";
	case 0xff: return "jumble";
	case 0x100: return "seek";
	case 0x101: return "send";
	case 0x102: return "deleteAt";
	case 0x103: return "insertAt";
	case 0x104: return "replaceAt";
	case 0x105: return "prependList";
	case 0x106: return "setDuration";
	case 0x107: return "percentComplete";
	case 0x108: return "mouseXOffset";
	case 0x109: return "mouseYOffset";
	case 0x10a: return "sort";
	case 0x10d: return "isVisible";
	case 0x141: return "mouseAbsX";
	case 0x142: return "mouseAbsY";
	case 0x169: return "isLoading";
	case 0x173: return "isActive";
	case 0x174: return "isPlaying";
	case 0x175: return "isPaused";
	case 0x176: return "loadContext";
	case 0x177: return "releaseContext";
	case 0x178: return "contextIsLoaded";
	}
	return NULL;
}

static const char *function_name(unsigned f) {
	switch (f) {
	case 0x64: return "Random";
	case 0x65: return "TimeOfDay";
	case 0x66: return "EffectTransition";
	case 0x67: return "EffectTransitionOnSync";
	case 0x68: return "Platform";
	case 0x69: return "SquareRoot";
	case 0x6a: return "GetUniqueRandom";
	case 0x6b: return "CurrentRunTime";
	case 0xaa: return "SetGammaCorrection";
	case 0xab: return "GetDefaultGammaCorrection";
	case 0xac: return "GetCurrentGammaCorrection";
	case 0xb4: return "DebugPrint";
	case 0xbe: return "SetAudioVolume";
	case 0xbf: return "GetAudioVolume";
	case 0xc8: return "SystemLanguagePreference";
	}
	return NULL;
}

static const char *token_name(unsigned t) {
	switch (t) {
	case 300: return "FadeToBlack";
	case 301: return "FadeToPalette";
	case 302: return "SetToPalette";
	case 303: return "SetToBlack";
	case 304: return "FadeToColor";
	case 305: return "SetToColor";
	case 306: return "SetToPercentOfPalette";
	case 307: return "FadeToPaletteObject";
	case 308: return "SetToPaletteObject";
	case 309: return "SetToPercentOfPaletteObject";
	case 310: return "ColorShiftCurrentPalette";
	}
	return NULL;
}

static void d_expr(MsChunk *c, Str *o, int ind);
static void d_block(MsChunk *c, Str *o, int ind);

static const char *opname(int op) {
	static const char *n[] = {"if", "ifelse", "=", "||", "^", "&&", "==", "!=", "<", ">", "<=", ">=",
		"+", "-", "*", "/", "%", "-", "call", "msg", "locals", "return", "return", "while", "icall",
		"imsg"};
	return (op >= OP_IF && op <= OP_CALL_METHOD_IN_VARIABLE) ? n[op - OP_IF] : "?";
}

static void d_var(MsChunk *c, Str *o, int ind) {
	uint16_t id = ms_t_u16(c), scope = ms_t_u16(c);
	switch (scope) {
	case SCOPE_GLOBAL:
		sp(o, "var_%u", id);
		break;
	case SCOPE_LOCAL:
		sp(o, "local_%u", id);
		break;
	case SCOPE_PARAMETER:
		sp(o, "arg_%u", id);
		break;
	case SCOPE_INDIRECT_PARAMETER:
		sp(o, "arg_%u[", id);
		d_expr(c, o, ind);
		sp(o, "]");
		break;
	default:
		sp(o, "?scope%u_%u", scope, id);
	}
}

static void d_value(MsChunk *c, Str *o) {
	uint16_t t = ms_t_u16(c);
	switch (t) {
	case OPERAND_BOOL:
		sp(o, "%s", ms_t_u8(c) ? "TRUE" : "FALSE");
		break;
	case OPERAND_FLOAT:
		sp(o, "%g", ms_t_double(c));
		break;
	case OPERAND_INT:
		sp(o, "%d", ms_t_s32(c));
		break;
	case OPERAND_STRING: {
		uint16_t n = ms_t_u16(c);
		char buf[256];
		size_t k = n < 255 ? n : 255;
		memcpy(buf, ms_ptr(c), k);
		buf[k] = 0;
		ms_skip(c, n);
		sp(o, "\"%s\"", buf);
		break;
	}
	case OPERAND_TOKEN: {
		uint16_t v = ms_t_u16(c);
		const char *nm = god_token_name(v);
		if (!nm)
			nm = token_name(v);
		if (nm)
			sp(o, "%s", nm);
		else
			sp(o, "token_%u", v);
		break;
	}
	case OPERAND_ACTOR:
		sp(o, "actor_%u", ms_t_u16(c));
		break;
	case OPERAND_TIME:
		sp(o, "time(%g)", ms_t_time(c));
		break;
	case OPERAND_VARIABLE: {
		MsValue v = val_read(c);
		sp(o, "{%s}", val_debug(&v));
		val_free(&v);
		break;
	}
	case OPERAND_FUNCTION:
		sp(o, "function_%u", ms_t_u16(c));
		break;
	case OPERAND_METHOD: {
		uint16_t m = ms_t_u16(c);
		if (method_name(m))
			sp(o, "method %s", method_name(m));
		else
			sp(o, "method_0x%x", m);
		break;
	}
	default:
		sp(o, "?operand%x", t);
	}
}

static void d_args(MsChunk *c, Str *o, int n, int ind) {
	sp(o, "(");
	for (int i = 0; i < n; i++) {
		if (i)
			sp(o, ", ");
		d_expr(c, o, ind);
	}
	sp(o, ")");
}

static void d_op(MsChunk *c, Str *o, int ind) {
	uint16_t op = ms_t_u16(c);
	switch (op) {
	case OP_IF:
	case OP_IF_ELSE:
		sp(o, "if (");
		d_expr(c, o, ind);
		sp(o, ")\n");
		d_block(c, o, ind + 1);
		if (op == OP_IF_ELSE) {
			sp(o, "%*selse\n", ind * 4, "");
			d_block(c, o, ind + 1);
		}
		sp(o, "%*send", ind * 4, "");
		break;
	case OP_ASSIGN:
		d_var(c, o, ind);
		sp(o, " = ");
		d_expr(c, o, ind);
		break;
	case OP_NEGATE:
		sp(o, "-(");
		d_expr(c, o, ind);
		sp(o, ")");
		break;
	case OP_CALL_FUNCTION: {
		uint16_t id = ms_t_u16(c), n = ms_t_u16(c);
		if (!ms_god.functions[id] && function_name(id))
			sp(o, "%s", function_name(id));
		else
			sp(o, "function_%u", id);
		d_args(c, o, n, ind);
		break;
	}
	case OP_CALL_FUNCTION_IN_VARIABLE: {
		uint16_t n = ms_t_u16(c);
		sp(o, "call[");
		d_expr(c, o, ind);
		sp(o, "]");
		d_args(c, o, n, ind);
		break;
	}
	case OP_CALL_METHOD:
	case OP_CALL_METHOD_IN_VARIABLE: {
		uint16_t m = 0, n;
		if (op == OP_CALL_METHOD) {
			m = ms_t_u16(c);
			n = ms_t_u16(c);
		} else {
			n = ms_t_u16(c);
			sp(o, "[");
			d_expr(c, o, ind);
			sp(o, "] ");
		}
		d_expr(c, o, ind);
		if (op == OP_CALL_METHOD && method_name(m))
			sp(o, ".%s", method_name(m));
		else if (op == OP_CALL_METHOD)
			sp(o, ".method_0x%x", m);
		d_args(c, o, n, ind);
		break;
	}
	case OP_NOP:
		sp(o, "nop");
		break;
	case OP_DECLARE_LOCALS:
		sp(o, "locals %u", ms_t_u16(c));
		break;
	case OP_RETURN:
		sp(o, "return ");
		d_expr(c, o, ind);
		break;
	case OP_RETURN_NO_VALUE:
		sp(o, "return");
		break;
	case OP_WHILE:
		sp(o, "while (");
		d_expr(c, o, ind);
		sp(o, ")\n");
		d_block(c, o, ind + 1);
		sp(o, "%*send", ind * 4, "");
		break;
	default:
		if (op >= OP_OR && op <= OP_MODULO) {
			sp(o, "(");
			d_expr(c, o, ind);
			sp(o, " %s ", opname(op));
			d_expr(c, o, ind);
			sp(o, ")");
		} else {
			sp(o, "?op%x", op);
		}
	}
}

static void d_expr_type(MsChunk *c, Str *o, int ind, uint16_t t) {
	switch (t) {
	case EXPR_OPERATION:
		d_op(c, o, ind);
		break;
	case EXPR_VALUE:
		d_value(c, o);
		break;
	case EXPR_VARIABLE:
		d_var(c, o, ind);
		break;
	case EXPR_EMPTY:
		break;
	default:
		sp(o, "?expr%x", t);
	}
}

static void d_expr(MsChunk *c, Str *o, int ind) {
	d_expr_type(c, o, ind, ms_t_u16(c));
}

static void d_block(MsChunk *c, Str *o, int ind) {
	uint32_t size = ms_t_u32(c);
	size_t end = c->pos + size;
	uint16_t t;
	while (c->pos < end && (t = ms_t_u16(c)) != EXPR_EMPTY) {
		sp(o, "%*s", ind * 4, "");
		d_expr_type(c, o, ind, t);
		sp(o, "\n");
	}
	c->pos = end;
}

char *script_decompile(const uint8_t *code, uint32_t size, int indent) {
	MsChunk c = {code, 0, size, 0, 0};
	Str o = {0};
	sp(&o, "");
	d_block(&c, &o, indent);
	return o.s;
}

const char *script_method_name(unsigned m) {
	return method_name(m);
}

const char *script_function_name(unsigned f) {
	return function_name(f);
}
