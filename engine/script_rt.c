/* Runtime helpers shared by the interpreter (script.c) and the recompiled scripts
 * (src\generated). Same behaviour as the script interpreter of LIONKING.EXE (addresses in the
 * comments, details in docs\ORIGINAL_SCRIPT.md). */
#include "script_rt.h"

#include <stdlib.h>
#include <string.h>

#include "god.h"
#include "script.h"

MsValue *rs_var(MsFrame *f, uint16_t id, uint16_t scope) {
	/* 1198:e970: variable id 0 or an index out of range is "no variable", never an error */
	if (id == 0)
		return NULL;
	switch (scope) {
	case SCOPE_LOCAL:
		return id <= f->nlocals ? &f->locals[id - 1] : NULL;
	case SCOPE_PARAMETER:
		return (f->args && id <= f->nargs) ? &f->args[id - 1] : NULL;
	case SCOPE_GLOBAL:
		/* the globals container of the frame (vtable +4 lookup) */
		return god_var(id);
	}
	return NULL;
}

MsValue *rs_var_indirect(MsFrame *f, uint16_t id, MsValue index) {
	/* 1198:e970 scope 3: parameter (16-bit) _ftol(index) + id, valid if <= the number of
	 * parameters. Index 0 makes the original read the value before the array (garbage): here no
	 * variable. */
	uint16_t i = (uint16_t)((uint16_t)ms_ftol(val_float_of(&index)) + id);
	val_free(&index);
	if (i == 0 || i > (unsigned)f->nargs || !f->args)
		return NULL;
	return &f->args[i - 1];
}

MsValue rs_get(const MsValue *p) {
	return p ? val_copy(p) : val_empty();
}

MsValue rs_assign(MsValue *dst, MsValue v) {
	/* 1198:e3e0: the "locked" flag (+2) of the destination is never set in LIONKING.EXE
	 * (1190:4c4a has no callers), so a variable is always assigned, an empty value too */
	if (!dst) {
		val_free(&v);
		return val_empty();
	}
	val_assign(dst, &v);
	return v;
}

void rs_frame_free(MsFrame *f) {
	for (int i = 0; i < f->nlocals; i++)
		val_free(&f->locals[i]);
	free(f->locals);
	f->locals = NULL;
	f->nlocals = 0;
}

void rs_locals(MsFrame *f, uint16_t n) {
	/* 1198:e87c: the old locals are freed, n new empty ones (none if n is 0) */
	rs_frame_free(f);
	if (n) {
		f->locals = calloc(n, sizeof(MsValue));
		f->nlocals = n;
	}
}

int rs_if(MsValue c) {
	int r = c.type == VAL_BOOL ? c.u.b == 1 : -1;
	val_free(&c);
	return r;
}

int rs_if_else(MsValue c) {
	if (c.type != VAL_BOOL)
		ms_fatal("script: if-else condition is %s (LIONKING.EXE 1198:e31a throws an exception)",
			val_type_name(c.type));
	int r = c.u.b == 1;
	val_free(&c);
	return r;
}

void rs_extension(const char *what) {
	if (!script_extensions)
		ms_fatal("script: %s is not in LIONKING.EXE (exception in the original)", what);
}

int rs_while(MsFrame *f, const MsValue *c, int *iterations) {
	if (c->type != VAL_BOOL)
		ms_fatal("script: while condition is %s", val_type_name(c->type));
	if (++*iterations >= MS_SCRIPT_MAX_LOOP) {
		ms_warn("script (actor %u): loop exceeded %d iterations", f->actor, MS_SCRIPT_MAX_LOOP);
		return 0;
	}
	return c->u.b;
}

MsValue rs_logic(int op, MsValue a, MsValue b) {
	/* 1190:4ee8 / 4f36 / 4f84: an operand that is not a bool counts as false, no error */
	int x = val_bool_of(&a) == 1, y = val_bool_of(&b) == 1;
	val_free(&a);
	val_free(&b);
	int r = op == OP_OR ? (x || y) : op == OP_XOR ? (x != y) : (x && y);
	return val_bool(r);
}

MsValue rs_compare(int op, MsValue a, MsValue b) {
	int r = val_compare(op, &a, &b);
	val_free(&a);
	val_free(&b);
	return val_bool(r);
}

MsValue rs_math(int op, MsValue a, MsValue b) {
	MsValue r = val_math(op, &a, &b);
	val_free(&a);
	val_free(&b);
	return r;
}

MsValue rs_negate(MsValue a) {
	MsValue r = val_negate(&a);
	val_free(&a);
	return r;
}

MsValue rs_string(const char *s, uint16_t n) {
	/* 1198:df20 operand 0x9A: n raw bytes, then the value is set with strcpy (1190:4d2c), so
	 * an embedded NUL ends the string */
	char *p = malloc((size_t)n + 1);
	memcpy(p, s, n);
	p[n] = 0;
	MsValue v = val_empty();
	v.type = VAL_STRING;
	v.str = p;
	return v;
}

MsValue rs_serialized(const uint8_t *data, uint32_t n) {
	MsChunk c;
	memset(&c, 0, sizeof(c));
	c.base = data;
	c.end = n;
	return val_read(&c);
}

uint32_t rs_function_id(MsValue v) {
	uint32_t id = val_as_function(&v);
	val_free(&v);
	return id;
}

uint32_t rs_method_id(MsValue v) {
	uint32_t id = val_as_method(&v);
	val_free(&v);
	return id;
}

static void free_args(MsValue *args, int n) {
	for (int i = 0; i < n; i++)
		val_free(&args[i]);
}

MsValue rs_call(uint32_t id, MsValue *args, int n) {
	MsValue r = script_call_function(id, args, n);
	free_args(args, n);
	return r;
}

MsValue rs_method(MsFrame *f, uint32_t m, MsValue *target, MsValue *args, int n) {
	MsValue none = val_empty();
	MsValue r = script_call_method(f->actor, target ? target : &none, m, args, n);
	free_args(args, n);
	return r;
}

MsValue rs_method_value(MsFrame *f, uint32_t m, MsValue target, MsValue *args, int n) {
	MsValue r = rs_method(f, m, &target, args, n);
	val_free(&target);
	return r;
}

MsValue rs_method_var(MsFrame *f, uint32_t m, MsValue *p, MsValue snapshot, MsValue *args, int n) {
	if (p && script_extensions && p->type == VAL_COLLECTION) {
		val_free(&snapshot);
		return rs_method(f, m, p, args, n);
	}
	return rs_method_value(f, m, snapshot, args, n);
}
