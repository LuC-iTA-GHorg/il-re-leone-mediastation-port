#include "value.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"

static char *dupstr(const char *s) {
	size_t n = strlen(s);
	char *d = malloc(n + 1);
	memcpy(d, s, n + 1);
	return d;
}

MsValue val_empty(void) {
	MsValue v;
	memset(&v, 0, sizeof(v));
	return v;
}

MsValue val_float(double d) {
	MsValue v = val_empty();
	v.type = VAL_FLOAT;
	v.u.d = d;
	return v;
}

MsValue val_bool(int b) {
	MsValue v = val_empty();
	v.type = VAL_BOOL;
	v.u.b = b != 0;
	return v;
}

MsValue val_time(double d) {
	MsValue v = val_empty();
	v.type = VAL_TIME;
	v.u.d = d;
	return v;
}

static MsValue val_u(int type, uint32_t u) {
	MsValue v = val_empty();
	v.type = (uint8_t)type;
	v.u.u = u;
	return v;
}

MsValue val_token(uint32_t t) {
	return val_u(VAL_TOKEN, t);
}

MsValue val_actor(uint32_t id) {
	return val_u(VAL_ACTOR, id);
}

MsValue val_function(uint32_t id) {
	return val_u(VAL_FUNCTION, id);
}

MsValue val_method(uint32_t id) {
	return val_u(VAL_METHOD, id);
}

MsValue val_string(const char *s) {
	MsValue v = val_empty();
	v.type = VAL_STRING;
	v.str = dupstr(s ? s : "");
	return v;
}

MsValue val_new_collection(void) {
	MsValue v = val_empty();
	v.type = VAL_COLLECTION;
	v.coll = calloc(1, sizeof(MsCollection));
	return v;
}

MsValue val_read(MsChunk *c) {
	MsValue v = val_empty();
	uint8_t type = ms_t_u8(c);
	switch (type) {
	case VAL_EMPTY:
		break;
	case VAL_FLOAT:
		v = val_float(ms_t_double(c));
		break;
	case VAL_BOOL: {
		uint8_t b = ms_t_u8(c);
		if (b > 1)
			ms_fatal("invalid literal bool %d", b);
		v = val_bool(b);
		break;
	}
	case VAL_TIME:
		/* the original stores a time read from data as a float */
		v = val_float(ms_t_time(c));
		break;
	case VAL_TOKEN:
		v = val_token(ms_t_u16(c));
		break;
	case VAL_ACTOR:
		v = val_actor(ms_t_u16(c));
		break;
	case VAL_STRING: {
		uint16_t n = ms_t_u16(c);
		char *s = malloc((size_t)n + 1);
		ms_read(c, s, n);
		s[n] = 0;
		v.type = VAL_STRING;
		v.str = s;
		break;
	}
	case VAL_COLLECTION: {
		uint16_t n = ms_t_u16(c);
		v = val_new_collection();
		for (int i = 0; i < n; i++) {
			MsValue item = val_read(c);
			coll_push(v.coll, &item);
			val_free(&item);
		}
		break;
	}
	case VAL_FUNCTION:
		v = val_function(ms_t_u16(c));
		break;
	case VAL_METHOD:
		v = val_method(ms_t_u16(c));
		break;
	default:
		ms_fatal("unknown script value type %d", type);
	}
	return v;
}

MsValue val_copy(const MsValue *v) {
	MsValue r = *v;
	if (v->type == VAL_STRING)
		r.str = dupstr(v->str ? v->str : "");
	else
		r.str = NULL;
	if (v->type == VAL_COLLECTION) {
		r.coll = calloc(1, sizeof(MsCollection));
		if (v->coll)
			for (int i = 0; i < v->coll->count; i++)
				coll_push(r.coll, &v->coll->items[i]);
	} else {
		r.coll = NULL;
	}
	return r;
}

void val_free(MsValue *v) {
	free(v->str);
	if (v->coll) {
		coll_clear(v->coll);
		free(v->coll->items);
		free(v->coll);
	}
	*v = val_empty();
}

void val_assign(MsValue *dst, const MsValue *src) {
	if (dst == src)
		return;
	MsValue tmp = val_copy(src);
	val_free(dst);
	*dst = tmp;
}

static void mismatch(const char *want, const MsValue *v) {
	ms_warn("script value type mismatch: expected %s, got %s", want, val_type_name(v->type));
}

double val_as_float(const MsValue *v) {
	if (v->type == VAL_FLOAT || v->type == VAL_TIME)
		return v->u.d;
	mismatch("float", v);
	return 0.0;
}

int val_as_bool(const MsValue *v) {
	if (v->type == VAL_BOOL)
		return v->u.b;
	mismatch("bool", v);
	return 0;
}

static uint32_t as_u(const MsValue *v, int type) {
	if (v->type == type)
		return v->u.u;
	mismatch(val_type_name(type), v);
	return 0;
}

uint32_t val_as_token(const MsValue *v) {
	return as_u(v, VAL_TOKEN);
}

uint32_t val_as_actor(const MsValue *v) {
	return as_u(v, VAL_ACTOR);
}

uint32_t val_as_function(const MsValue *v) {
	return as_u(v, VAL_FUNCTION);
}

uint32_t val_as_method(const MsValue *v) {
	return as_u(v, VAL_METHOD);
}

const char *val_as_string(const MsValue *v) {
	return (v->type == VAL_STRING && v->str) ? v->str : "";
}

/* ---- exact accessors of LIONKING.EXE: a value of another type gives the default, silently ---- */

double val_float_of(const MsValue *v) {
	/* 1190:4dc0: the float, 0.0 (DS:2F02) for any other type, time included */
	return v->type == VAL_FLOAT ? v->u.d : 0.0;
}

int val_bool_of(const MsValue *v) {
	/* 1190:4e06 */
	return v->type == VAL_BOOL ? v->u.b : 0;
}

double val_time_of(const MsValue *v) {
	/* 1190:4e26: a static zero time for any other type */
	return v->type == VAL_TIME ? v->u.d : 0.0;
}

uint32_t val_token_of(const MsValue *v) {
	/* 1190:4e80 */
	return v->type == VAL_TOKEN ? v->u.u : 0;
}

uint32_t val_actor_of(const MsValue *v) {
	/* 1190:4ea0 */
	return v->type == VAL_ACTOR ? v->u.u : 0;
}

const char *val_string_of(const MsValue *v) {
	/* 1190:4ec0: the empty string DS:032A for any other type */
	return (v->type == VAL_STRING && v->str) ? v->str : "";
}

int32_t ms_ftol(double d) {
	/* 1180:1202: FISTP with the rounding mode set to chop; out of range (or NaN) gives the x87
	 * "integer indefinite" 0x80000000 */
	if (!(d > -2147483649.0 && d < 2147483648.0))
		return INT32_MIN;
	return (int32_t)d;
}

/* ---- comparisons: 1190:516a ---- */

/* 1190:52ec: FCOM b, a then a conditional jump; an unordered result (NaN) sets ZF and CF. */
static int cmp_float(int op, double a, double b) {
	int unordered = isnan(a) || isnan(b);
	int zf = unordered || a == b;
	int cf = unordered || b < a;
	switch (op) {
	case OP_EQUALS: /* je -> true */
		return zf;
	case OP_NOT_EQUALS:
		return !zf;
	case OP_LESS: /* jbe -> false */
		return !(cf || zf);
	case OP_GREATER: /* jae -> false */
		return cf;
	case OP_LESS_EQUAL: /* jae -> true */
		return !cf;
	case OP_GREATER_EQUAL: /* ja -> false */
		return cf || zf;
	}
	return 0;
}

static int cmp_order(int op, int c) {
	switch (op) {
	case OP_EQUALS:
		return c == 0;
	case OP_NOT_EQUALS:
		return c != 0;
	case OP_LESS:
		return c < 0;
	case OP_GREATER:
		return c > 0;
	case OP_LESS_EQUAL:
		return c <= 0;
	case OP_GREATER_EQUAL:
		return c >= 0;
	}
	return 0;
}

/* == and != only; any other operator is false */
static int cmp_eq_only(int op, int equal) {
	if (op == OP_EQUALS)
		return equal;
	if (op == OP_NOT_EQUALS)
		return !equal;
	return 0;
}

int val_compare(int op, const MsValue *a, const MsValue *b) {
	/* 1190:516a: values of different types are compared as floats (1190:4dc0 gives 0.0 for
	 * anything that is not a float, time included); no warning, no error. */
	int type = a->type == b->type ? a->type : VAL_FLOAT;
	switch (type) {
	case VAL_EMPTY:
		/* 1190:52c4: empty == empty, every other operator false */
		return op == OP_EQUALS;
	case VAL_FLOAT:
		return cmp_float(op, val_float_of(a), val_float_of(b));
	case VAL_BOOL:
		/* 1190:5394 */
		return cmp_eq_only(op, val_bool_of(a) == val_bool_of(b));
	case VAL_TIME: {
		/* 1190:53cc: all six operators */
		double x = val_time_of(a), y = val_time_of(b);
		return cmp_order(op, x < y ? -1 : x > y ? 1 : 0);
	}
	case VAL_TOKEN:
		/* 1190:54de: ==, != and < (unsigned); >, <= and >= are always false */
		if (op == OP_LESS)
			return val_token_of(a) < val_token_of(b);
		return cmp_eq_only(op, val_token_of(a) == val_token_of(b));
	case VAL_ACTOR:
		/* 1190:5524 */
		return cmp_eq_only(op, val_actor_of(a) == val_actor_of(b));
	case VAL_STRING: {
		/* 1190:555c: inline strcmp (unsigned bytes), all six operators */
		const unsigned char *x = (const unsigned char *)val_string_of(a);
		const unsigned char *y = (const unsigned char *)val_string_of(b);
		while (*x && *x == *y)
			x++, y++;
		return cmp_order(op, *x < *y ? -1 : *x > *y ? 1 : 0);
	}
	/* not in LIONKING.EXE (its values have only the types 0..6): later engines */
	case VAL_COLLECTION:
		return cmp_eq_only(op, a->coll == b->coll);
	case VAL_FUNCTION:
	case VAL_METHOD:
		return cmp_eq_only(op, a->u.u == b->u.u);
	}
	return 0;
}

int val_equal(const MsValue *a, const MsValue *b) {
	if (a->type != b->type)
		return 0;
	return val_compare(OP_EQUALS, a, b);
}

/* ---- arithmetic: 1190:5760, dispatched on the type of the first operand ---- */

/* 1190:588c: float op float; dividing by zero (or modulo zero) gives 0.0, no error */
static double math_float(int op, double a, double b) {
	switch (op) {
	case OP_ADD:
		return a + b;
	case OP_SUBTRACT:
		return a - b;
	case OP_MULTIPLY:
		return a * b;
	case OP_DIVIDE:
		return b == 0.0 ? 0.0 : a / b;
	case OP_MODULO:
		return b == 0.0 ? 0.0 : fmod(a, b); /* 1180:119a: FPREM loop */
	}
	return 0.0;
}

MsValue val_math(int op, const MsValue *a, const MsValue *b) {
	switch (a->type) {
	case VAL_FLOAT:
		if (b->type == VAL_TIME)
			/* 1190:5acc: float * time is a time; every other operator gives time 0 */
			return val_time(op == OP_MULTIPLY ? val_time_of(b) * a->u.d : 0.0);
		/* 1190:588c: the second operand counts 0.0 if it is not a float */
		return val_float(math_float(op, a->u.d, val_float_of(b)));
	case VAL_TIME:
		if (b->type == VAL_FLOAT) {
			/* 1190:5b54: time * float and time / float (no zero check: x87 gives inf/NaN),
			 * every other operator gives time 0 */
			if (op == OP_MULTIPLY)
				return val_time(a->u.d * b->u.d);
			if (op == OP_DIVIDE)
				return val_time(a->u.d / b->u.d);
			return val_time(0.0);
		}
		{
			/* 1190:5926: time op time (any non-time second operand counts as time 0) */
			double x = a->u.d, y = val_time_of(b);
			switch (op) {
			case OP_ADD:
				return val_time(x + y);
			case OP_SUBTRACT:
				return val_time(x - y);
			case OP_DIVIDE:
				/* the only case that gives a float; by zero: time 0 */
				return y != 0.0 ? val_float(x / y) : val_time(0.0);
			case OP_MODULO:
				return val_time(y != 0.0 ? fmod(x, y) : 0.0);
			}
			return val_time(0.0); /* multiply */
		}
	case VAL_STRING:
		if (b->type == VAL_STRING) {
			/* 1190:5bf0: + concatenates; with another operator the original returns a string
			 * made of the uninitialized new buffer (garbage): here the empty string */
			MsValue r = val_empty();
			r.type = VAL_STRING;
			if (op != OP_ADD) {
				r.str = calloc(1, 1);
				return r;
			}
			const char *x = val_string_of(a), *y = val_string_of(b);
			size_t n = strlen(x), m = strlen(y);
			r.str = malloc(n + m + 1);
			memcpy(r.str, x, n);
			memcpy(r.str + n, y, m + 1);
			return r;
		}
		break;
	}
	/* any other combination: the result stays empty (no error) */
	return val_empty();
}

MsValue val_negate(const MsValue *v) {
	/* 1190:56ec: float and time (1190:6086); any other type leaves the result empty */
	if (v->type == VAL_FLOAT)
		return val_float(-v->u.d);
	if (v->type == VAL_TIME)
		return val_time(-v->u.d);
	return val_empty();
}

void coll_push(MsCollection *c, const MsValue *v) {
	coll_insert(c, c->count, v);
}

void coll_insert(MsCollection *c, int index, const MsValue *v) {
	if (index < 0 || index > c->count)
		return;
	if (c->count == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 8;
		c->items = realloc(c->items, sizeof(MsValue) * (size_t)c->cap);
	}
	memmove(&c->items[index + 1], &c->items[index], sizeof(MsValue) * (size_t)(c->count - index));
	c->items[index] = val_copy(v);
	c->count++;
}

MsValue coll_remove(MsCollection *c, int index) {
	if (index < 0 || index >= c->count)
		return val_empty();
	MsValue r = c->items[index];
	memmove(&c->items[index], &c->items[index + 1], sizeof(MsValue) * (size_t)(c->count - index - 1));
	c->count--;
	return r;
}

void coll_clear(MsCollection *c) {
	for (int i = 0; i < c->count; i++)
		val_free(&c->items[i]);
	c->count = 0;
}

const char *val_type_name(int type) {
	static const char *names[] = {"empty", "float", "bool", "time", "token", "actor", "string",
		"collection", "function", "method"};
	return (type >= 0 && type <= 9) ? names[type] : "?";
}

const char *val_debug(const MsValue *v) {
	static char buf[8][96];
	static int n;
	char *s = buf[n++ & 7];
	switch (v->type) {
	case VAL_EMPTY:
		snprintf(s, 96, "empty");
		break;
	case VAL_FLOAT:
		snprintf(s, 96, "%g", v->u.d);
		break;
	case VAL_TIME:
		snprintf(s, 96, "time %g", v->u.d);
		break;
	case VAL_BOOL:
		snprintf(s, 96, "%s", v->u.b ? "TRUE" : "FALSE");
		break;
	case VAL_STRING:
		snprintf(s, 96, "\"%.80s\"", val_as_string(v));
		break;
	case VAL_COLLECTION:
		snprintf(s, 96, "[%d items]", v->coll ? v->coll->count : 0);
		break;
	default:
		snprintf(s, 96, "%s %u", val_type_name(v->type), (unsigned)v->u.u);
	}
	return s;
}
