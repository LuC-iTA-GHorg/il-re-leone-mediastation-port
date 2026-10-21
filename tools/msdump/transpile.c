/* msdump transpile: translates every script (actor responses, title functions) to a C function
 * that calls the runtime API of engine\script_rt.h. The walker mirrors the interpreter of
 * engine\script.c: it reads the bytecode with the same typed reads, in the same order, and
 * emits one C statement per evaluation so that the C code evaluates in bytecode order.
 * Control flow (if, if-else, while, return) becomes C control flow. A construct that is not
 * supported (e.g. a return inside an expression) leaves that script to the interpreter. */
#include "transpile.h"

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "constants.h"
#include "god.h"
#include "script.h"
#include "value.h"

typedef struct {
	char *s;
	size_t n, cap;
} Buf;

static void bput(Buf *b, const char *fmt, ...) {
	va_list ap, ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	int k = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (k < 0) {
		va_end(ap2);
		return;
	}
	if (b->n + (size_t)k + 1 > b->cap) {
		b->cap = (b->n + (size_t)k + 1) * 2;
		b->s = realloc(b->s, b->cap);
	}
	vsnprintf(b->s + b->n, (size_t)k + 1, fmt, ap2);
	va_end(ap2);
	b->n += (size_t)k;
}

static void bfree(Buf *b) {
	free(b->s);
	memset(b, 0, sizeof(*b));
}

/* ---- names table ---- */

static char **names_tab[4]; /* var, actor, context, function; each MS_MAX_ID entries */

static int kind_index(char kind) {
	switch (kind) {
	case 'v':
		return 0;
	case 'a':
		return 1;
	case 'c':
		return 2;
	case 'f':
		return 3;
	}
	return -1;
}

static int is_ident(const char *s) {
	if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_'))
		return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_'))
			return 0;
	return 1;
}

int names_load(const char *path) {
	FILE *fp = fopen(path, "r");
	if (!fp)
		return -1;
	char ln[1024];
	int n = 0, lineno = 0;
	while (fgets(ln, sizeof(ln), fp)) {
		lineno++;
		char *hash = strchr(ln, '#');
		if (hash)
			*hash = 0;
		char kind[32], name[256];
		unsigned id;
		int k = sscanf(ln, "%31s %u %255s", kind, &id, name);
		if (k <= 0)
			continue;
		int ki = -1;
		if (k == 3) {
			if (!strcmp(kind, "var"))
				ki = 0;
			else if (!strcmp(kind, "actor"))
				ki = 1;
			else if (!strcmp(kind, "context"))
				ki = 2;
			else if (!strcmp(kind, "function"))
				ki = 3;
		}
		if (ki < 0 || id >= MS_MAX_ID || !is_ident(name)) {
			fprintf(stderr, "%s:%d: invalid line ignored\n", path, lineno);
			continue;
		}
		if (!names_tab[ki])
			names_tab[ki] = calloc(MS_MAX_ID, sizeof(char *));
		if (names_tab[ki][id])
			fprintf(stderr, "%s:%d: %s %u named twice\n", path, lineno, kind, id);
		free(names_tab[ki][id]);
		names_tab[ki][id] = malloc(strlen(name) + 1);
		strcpy(names_tab[ki][id], name);
		n++;
	}
	fclose(fp);
	/* the same name twice in one kind would make the output ambiguous */
	for (int ki = 0; ki < 4; ki++)
		for (int i = 0; names_tab[ki] && i < MS_MAX_ID; i++)
			for (int j = i + 1; names_tab[ki][i] && j < MS_MAX_ID; j++)
				if (names_tab[ki][j] && !strcmp(names_tab[ki][i], names_tab[ki][j]))
					fprintf(stderr, "%s: name %s used for ids %d and %d\n", path, names_tab[ki][i], i, j);
	return n;
}

const char *names_get(char kind, unsigned id) {
	int ki = kind_index(kind);
	if (ki < 0 || id >= MS_MAX_ID || !names_tab[ki])
		return NULL;
	return names_tab[ki][id];
}

char *names_apply(const char *text) {
	static const struct {
		const char *prefix;
		char kind;
	} pre[] = {{"var_", 'v'}, {"actor_", 'a'}, {"function_", 'f'}};
	Buf o = {0};
	bput(&o, "");
	const char *p = text;
	while (*p) {
		int word_start = p == text || !(p[-1] == '_' || (p[-1] >= '0' && p[-1] <= '9') ||
			(p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= 'A' && p[-1] <= 'Z'));
		int done = 0;
		for (size_t k = 0; word_start && k < sizeof(pre) / sizeof(pre[0]); k++) {
			size_t len = strlen(pre[k].prefix);
			if (strncmp(p, pre[k].prefix, len) || !(p[len] >= '0' && p[len] <= '9'))
				continue;
			const char *q = p + len;
			unsigned id = 0;
			while (*q >= '0' && *q <= '9' && id < MS_MAX_ID)
				id = id * 10 + (unsigned)(*q++ - '0');
			if (*q == '_' || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9'))
				break;
			const char *nm = names_get(pre[k].kind, id);
			if (!nm)
				break;
			bput(&o, "%s", nm);
			p = q;
			done = 1;
			break;
		}
		if (!done)
			bput(&o, "%c", *p++);
	}
	return o.s;
}

/* " /" "* name *" "/" after a generated line, or "" */
static const char *name_comment(char kind, unsigned id) {
	static char buf[300];
	const char *nm = names_get(kind, id);
	if (!nm)
		return "";
	snprintf(buf, sizeof(buf), " /* %s */", nm);
	return buf;
}

/* ---- code generation for one script ---- */

typedef struct {
	MsChunk c;
	Buf *o;
	int tmp;  /* last temporary number */
	int ind;  /* indentation (tabs) */
	int done; /* the "done" label is used */
	char kinds[256]; /* enclosing blocks, outermost first: 'b', 't' (true block of an if-else),
	                  * 'w' (while body) */
	int nk;
	jmp_buf fail;
	char err[160];
} Gen;

static void line(Gen *g, const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	for (int i = 0; i < g->ind; i++)
		bput(g->o, "\t");
	bput(g->o, "%s\n", buf);
}

static _Noreturn void fail(Gen *g, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g->err, sizeof(g->err), fmt, ap);
	va_end(ap);
	longjmp(g->fail, 1);
}

static int gen_expr(Gen *g);
static int gen_expr_type(Gen *g, uint16_t type);
static void gen_block(Gen *g, char kind);

/* Bytecode reads like the interpreter (script.c): the datum type is skipped, not checked.
 * Reading past the end refuses the script (the interpreter would read zeros there). */
static void rd_raw(Gen *g, void *dst, size_t n) {
	if (g->c.pos > g->c.end || g->c.end - g->c.pos < n)
		fail(g, "read past the end of the script");
	memcpy(dst, g->c.base + g->c.pos, n);
	g->c.pos += n;
}

static uint8_t rd8(Gen *g) {
	uint8_t t[2], v;
	rd_raw(g, t, 2);
	rd_raw(g, &v, 1);
	return v;
}

static uint16_t rd16(Gen *g) {
	uint8_t b[4];
	rd_raw(g, b, 4);
	return (uint16_t)(b[2] | b[3] << 8);
}

static uint32_t rd32(Gen *g) {
	uint8_t b[6];
	rd_raw(g, b, 6);
	return (uint32_t)b[2] | (uint32_t)b[3] << 8 | (uint32_t)b[4] << 16 | (uint32_t)b[5] << 24;
}

static double rd_double(Gen *g) {
	uint8_t t[2];
	double d;
	rd_raw(g, t, 2);
	rd_raw(g, &d, 8);
	return d;
}

static const char *op_const(int op) {
	if (op == OP_NOP)
		return "OP_NOP";
	static const char *n[] = {"OP_IF", "OP_IF_ELSE", "OP_ASSIGN", "OP_OR", "OP_XOR", "OP_AND",
		"OP_EQUALS", "OP_NOT_EQUALS", "OP_LESS", "OP_GREATER", "OP_LESS_EQUAL", "OP_GREATER_EQUAL",
		"OP_ADD", "OP_SUBTRACT", "OP_MULTIPLY", "OP_DIVIDE", "OP_MODULO", "OP_NEGATE"};
	return (op >= OP_IF && op <= OP_NEGATE) ? n[op - OP_IF] : "?";
}

static const char *scope_const(uint16_t scope) {
	switch (scope) {
	case SCOPE_LOCAL:
		return "SCOPE_LOCAL";
	case SCOPE_PARAMETER:
		return "SCOPE_PARAMETER";
	case SCOPE_GLOBAL:
		return "SCOPE_GLOBAL";
	}
	return NULL;
}

/* A variable reference: emits "MsValue *pN = ..." (may be NULL at run time) and returns N. */
static int gen_varref(Gen *g) {
	uint16_t id = rd16(g);
	uint16_t scope = rd16(g);
	if (id == 0)
		/* 1198:e970 reads nothing more (not even an index expression): no static layout */
		fail(g, "variable id 0");
	if (scope == SCOPE_INDIRECT_PARAMETER) {
		int t = gen_expr(g);
		int p = ++g->tmp;
		line(g, "MsValue *p%d = rs_var_indirect(f, %u, t%d);", p, id, t);
		return p;
	}
	int p = ++g->tmp;
	if (!scope_const(scope))
		line(g, "MsValue *p%d = rs_var(f, %u, %u); /* unknown scope: no variable */", p, id, scope);
	else
		line(g, "MsValue *p%d = rs_var(f, %u, %s);%s", p, id, scope_const(scope),
			scope == SCOPE_GLOBAL ? name_comment('v', id) : "");
	return p;
}

static void c_string(Buf *b, const uint8_t *s, size_t n) {
	bput(b, "\"");
	for (size_t i = 0; i < n; i++) {
		uint8_t ch = s[i];
		if (ch >= 0x20 && ch < 0x7f && ch != '"' && ch != '\\' && ch != '?')
			bput(b, "%c", ch);
		else
			bput(b, "\\%03o", ch);
	}
	bput(b, "\"");
}

static int gen_value(Gen *g) {
	uint16_t type = rd16(g);
	int t = ++g->tmp;
	switch (type) {
	case OPERAND_BOOL: {
		uint8_t b = rd8(g);
		line(g, "MsValue t%d = val_bool(%d); /* byte %u */", t, b >= 1, b);
		return t;
	}
	case OPERAND_FLOAT:
	case OPERAND_TIME: {
		double d = rd_double(g);
		if (!isfinite(d))
			fail(g, "non-finite literal");
		line(g, "MsValue t%d = %s(%a); /* %.17g */", t, type == OPERAND_FLOAT ? "val_float" : "val_time", d, d);
		return t;
	}
	case OPERAND_INT: {
		uint32_t v = rd32(g);
		line(g, "MsValue t%d = val_float(%d.0);", t, (int32_t)v);
		return t;
	}
	case OPERAND_STRING: {
		uint16_t n = rd16(g);
		if (g->c.end - g->c.pos < n)
			fail(g, "read past the end of the script");
		Buf s = {0};
		c_string(&s, ms_ptr(&g->c), n);
		ms_skip(&g->c, n);
		line(g, "MsValue t%d = rs_string(%s, %u);", t, s.s, n);
		bfree(&s);
		return t;
	}
	case OPERAND_TOKEN:
		line(g, "MsValue t%d = val_token(%u);", t, rd16(g));
		return t;
	case OPERAND_ACTOR: {
		uint16_t id = rd16(g);
		line(g, "MsValue t%d = val_actor(%u);%s", t, id, name_comment('a', id));
		return t;
	}
	case OPERAND_FUNCTION: {
		line(g, "rs_extension(\"operand 0x9f (function id)\");");
		uint16_t id = rd16(g);
		line(g, "MsValue t%d = val_function(%u);%s", t, id, name_comment('f', id));
		return t;
	}
	case OPERAND_METHOD: {
		line(g, "rs_extension(\"operand 0xa0 (method id)\");");
		uint16_t m = rd16(g);
		const char *nm = script_method_name(m);
		line(g, "MsValue t%d = val_method(0x%x); /* %s */", t, m, nm ? nm : "?");
		return t;
	}
	case OPERAND_VARIABLE: {
		/* a serialized ScriptValue: keep its bytes and read them at run time */
		line(g, "rs_extension(\"operand 0x9e (serialized value)\");");
		const uint8_t *p = ms_ptr(&g->c);
		size_t start = g->c.pos;
		MsValue v = val_read(&g->c);
		val_free(&v);
		size_t n = g->c.pos - start;
		Buf s = {0};
		for (size_t i = 0; i < n; i++)
			bput(&s, "%s0x%02x", i ? ", " : "", p[i]);
		line(g, "MsValue t%d = rs_serialized((const uint8_t[]){%s}, %zu);", t, s.s, n);
		bfree(&s);
		return t;
	}
	}
	fail(g, "unknown operand type 0x%x", type);
	return 0;
}

/* Evaluates n arguments in order; returns the array number (0: no arguments, use NULL). */
static int gen_args(Gen *g, int n) {
	if (n == 0)
		return 0;
	int *t = malloc(sizeof(int) * (size_t)n);
	for (int i = 0; i < n; i++)
		t[i] = gen_expr(g);
	Buf s = {0};
	for (int i = 0; i < n; i++)
		bput(&s, "%st%d", i ? ", " : "", t[i]);
	int a = ++g->tmp;
	line(g, "MsValue a%d[%d] = {%s};", a, n, s.s);
	bfree(&s);
	free(t);
	return a;
}

static void args_ref(char *buf, size_t size, int a) {
	if (a)
		snprintf(buf, size, "a%d", a);
	else
		snprintf(buf, size, "NULL");
}

/* Method call: the method id is a constant (m >= 0) or the C variable mN (m < 0). */
static int gen_method(Gen *g, const char *method, int n) {
	uint16_t type = rd16(g);
	char a[32];
	int t;
	if (type == EXPR_VARIABLE) {
		/* 1198:e72e: the target value is taken before the arguments are evaluated */
		int p = gen_varref(g);
		int v = ++g->tmp;
		line(g, "MsValue t%d = rs_get(p%d);", v, p);
		args_ref(a, sizeof(a), gen_args(g, n));
		t = ++g->tmp;
		line(g, "MsValue t%d = rs_method_var(f, %s, p%d, t%d, %s, %d);", t, method, p, v, a, n);
	} else if (type == EXPR_VALUE || type == EXPR_OPERATION) {
		int v = gen_expr_type(g, type);
		args_ref(a, sizeof(a), gen_args(g, n));
		t = ++g->tmp;
		line(g, "MsValue t%d = rs_method_value(f, %s, t%d, %s, %d);", t, method, v, a, n);
	} else {
		fail(g, "method target of expression type 0x%x", type);
		t = 0;
	}
	return t;
}

/* An operation in expression position (opcode already read). */
static int gen_op(Gen *g, uint16_t op) {
	char a[32];
	switch (op) {
	case OP_NOP: {
		int t = ++g->tmp;
		line(g, "MsValue t%d = val_empty(); /* opcode 0xc8 */", t);
		return t;
	}
	case OP_ASSIGN: {
		int p = gen_varref(g);
		int v = gen_expr(g);
		int t = ++g->tmp;
		line(g, "MsValue t%d = rs_assign(p%d, t%d);", t, p, v);
		return t;
	}
	case OP_OR:
	case OP_XOR:
	case OP_AND:
	case OP_EQUALS:
	case OP_NOT_EQUALS:
	case OP_LESS:
	case OP_GREATER:
	case OP_LESS_EQUAL:
	case OP_GREATER_EQUAL:
	case OP_ADD:
	case OP_SUBTRACT:
	case OP_MULTIPLY:
	case OP_DIVIDE:
	case OP_MODULO: {
		int x = gen_expr(g);
		int y = gen_expr(g);
		const char *fn = op <= OP_AND ? "rs_logic" : op <= OP_GREATER_EQUAL ? "rs_compare" : "rs_math";
		int t = ++g->tmp;
		line(g, "MsValue t%d = %s(%s, t%d, t%d);", t, fn, op_const(op), x, y);
		return t;
	}
	case OP_NEGATE: {
		int x = gen_expr(g);
		int t = ++g->tmp;
		line(g, "MsValue t%d = rs_negate(t%d);", t, x);
		return t;
	}
	case OP_CALL_FUNCTION: {
		uint16_t id = rd16(g);
		uint16_t n = rd16(g);
		args_ref(a, sizeof(a), gen_args(g, n));
		int t = ++g->tmp;
		const char *nm = ms_god.functions[id] ? NULL : script_function_name(id);
		if (nm)
			line(g, "MsValue t%d = rs_call(0x%x, %s, %u); /* %s */", t, id, a, n, nm);
		else if (names_get('f', id))
			line(g, "MsValue t%d = rs_call(%u, %s, %u); /* %s */", t, id, a, n, names_get('f', id));
		else
			line(g, "MsValue t%d = rs_call(%u, %s, %u); /* function_%u */", t, id, a, n, id);
		return t;
	}
	case OP_CALL_FUNCTION_IN_VARIABLE: {
		line(g, "rs_extension(\"opcode 0xe1 (call through a variable)\");");
		uint16_t n = rd16(g);
		int fv = gen_expr(g);
		int id = ++g->tmp;
		line(g, "uint32_t f%d = rs_function_id(t%d);", id, fv);
		args_ref(a, sizeof(a), gen_args(g, n));
		int t = ++g->tmp;
		line(g, "MsValue t%d = rs_call(f%d, %s, %u);", t, id, a, n);
		return t;
	}
	case OP_CALL_METHOD: {
		uint16_t m = rd16(g);
		uint16_t n = rd16(g);
		char ms[64];
		const char *nm = script_method_name(m);
		snprintf(ms, sizeof(ms), "0x%x /* %s */", m, nm ? nm : "?");
		return gen_method(g, ms, n);
	}
	case OP_CALL_METHOD_IN_VARIABLE: {
		line(g, "rs_extension(\"opcode 0xe2 (method through a variable)\");");
		uint16_t n = rd16(g);
		int mv = gen_expr(g);
		int id = ++g->tmp;
		line(g, "uint32_t m%d = rs_method_id(t%d);", id, mv);
		char ms[32];
		snprintf(ms, sizeof(ms), "m%d", id);
		return gen_method(g, ms, n);
	}
	case OP_IF:
	case OP_IF_ELSE:
	case OP_WHILE:
	case OP_RETURN:
	case OP_RETURN_NO_VALUE:
	case OP_DECLARE_LOCALS:
		fail(g, "statement opcode 0x%x inside an expression", op);
	}
	fail(g, "unknown opcode 0x%x", op);
	return 0;
}

static int gen_expr_type(Gen *g, uint16_t type) {
	switch (type) {
	case EXPR_OPERATION:
		return gen_op(g, rd16(g));
	case EXPR_VALUE:
		return gen_value(g);
	case EXPR_VARIABLE: {
		int p = gen_varref(g);
		int t = ++g->tmp;
		line(g, "MsValue t%d = rs_get(p%d);", t, p);
		return t;
	}
	}
	fail(g, "unknown expression type 0x%x", type);
	return 0;
}

static int gen_expr(Gen *g) {
	return gen_expr_type(g, rd16(g));
}

/* One expression of a block: control flow becomes C, anything else is evaluated and freed. */
static void gen_stmt(Gen *g, uint16_t type) {
	if (type != EXPR_OPERATION) {
		line(g, "{");
		g->ind++;
		int t = gen_expr_type(g, type);
		line(g, "val_free(&t%d);", t);
		g->ind--;
		line(g, "}");
		return;
	}
	uint16_t op = rd16(g);
	switch (op) {
	case OP_IF: {
		/* 1198:e2b4; a condition that is not a bool continues in the interpreter (two blocks
		 * skipped, rs_resume) */
		line(g, "{");
		g->ind++;
		int c = gen_expr(g);
		int k = ++g->tmp;
		line(g, "int k%d = rs_if(t%d);", k, c);
		line(g, "if (k%d > 0) {", k);
		g->ind++;
		gen_block(g, 'b');
		g->ind--;
		char cont[260];
		int n = 0;
		for (int i = g->nk - 1; i >= 0; i--) {
			if (g->kinds[i] == 'w')
				fail(g, "if inside a while (no resume)");
			cont[n++] = g->kinds[i];
		}
		cont[n] = 0;
		line(g, "} else if (k%d < 0) {", k);
		line(g, "\trv = rs_resume(f, %zu, \"%s\");", g->c.pos, cont);
		line(g, "\tgoto done;");
		line(g, "}");
		g->done = 1;
		g->ind--;
		line(g, "}");
		return;
	}
	case OP_IF_ELSE: {
		/* 1198:e31a */
		line(g, "{");
		g->ind++;
		int c = gen_expr(g);
		line(g, "if (rs_if_else(t%d)) {", c);
		g->ind++;
		gen_block(g, 't');
		g->ind--;
		line(g, "} else {");
		g->ind++;
		gen_block(g, 'b');
		g->ind--;
		line(g, "}");
		g->ind--;
		line(g, "}");
		return;
	}
	case OP_WHILE: {
		int it = ++g->tmp;
		line(g, "rs_extension(\"opcode 0xe0 (while)\");");
		line(g, "{");
		g->ind++;
		line(g, "int it%d = 0;", it);
		line(g, "for (;;) {");
		g->ind++;
		int c = gen_expr(g);
		line(g, "if (!rs_while(f, &t%d, &it%d))", c, it);
		line(g, "\tbreak;");
		gen_block(g, 'w');
		g->ind--;
		line(g, "}");
		g->ind--;
		line(g, "}");
		return;
	}
	case OP_RETURN: {
		line(g, "{");
		g->ind++;
		int t = gen_expr(g);
		line(g, "rv = t%d;", t);
		line(g, "goto done;");
		g->ind--;
		line(g, "}");
		g->done = 1;
		return;
	}
	case OP_RETURN_NO_VALUE:
		line(g, "goto done;");
		g->done = 1;
		return;
	case OP_DECLARE_LOCALS:
		line(g, "rs_locals(f, %u);", rd16(g));
		return;
	}
	line(g, "{");
	g->ind++;
	int t = gen_op(g, op);
	line(g, "val_free(&t%d);", t);
	g->ind--;
	line(g, "}");
}

/* A block (1198:dda4). Its size must match what it contains: the interpreter skips blocks by
 * their size, the C code by their structure. */
static void gen_block(Gen *g, char kind) {
	if (g->nk >= (int)sizeof(g->kinds))
		fail(g, "blocks nested too deep");
	g->kinds[g->nk++] = kind;
	uint32_t size = rd32(g);
	size_t start = g->c.pos;
	uint16_t type;
	while ((type = rd16(g)) != EXPR_EMPTY)
		gen_stmt(g, type);
	if (g->c.pos - start != size)
		fail(g, "block of %u bytes, read %zu", size, g->c.pos - start);
	g->nk--;
}

/* ---- the run: scripts, deduplicated by bytecode ---- */

typedef struct {
	uint64_t hash;
	uint32_t size;
	uint8_t *code;
	char name[48];
	char first[240]; /* first identity, for the index */
	int uses;
} Script;

static struct {
	char outdir[512];
	Script *s;
	int n, cap;
	int responses, functions, refused, contexts;
	long lines;
} run;

void transpile_begin(const char *outdir) {
	memset(&run, 0, sizeof(run));
	snprintf(run.outdir, sizeof(run.outdir), "%s", outdir);
}

static void comment_text(Buf *o, const char *s) {
	/* a decompiled script inside a C comment: no comment delimiters, trigraphs or control chars */
	for (const char *p = s; *p; p++) {
		char c = *p;
		if ((c == '*' && p[1] == '/') || (c == '/' && p[1] == '*') || (c == '?' && p[1] == '?'))
			bput(o, "%c ", c);
		else if (c == '\n') {
			if (p[1])
				bput(o, "\n * ");
		}
		else if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f || c == '\\')
			bput(o, ".");
		else
			bput(o, "%c", c);
	}
}

/* Translates one script into o; returns 0 (and prints why) if refused. */
static int translate(Buf *o, const char *ident, const uint8_t *code, uint32_t size) {
	uint64_t h = script_hash(code, size);
	for (int i = 0; i < run.n; i++)
		if (run.s[i].hash == h && run.s[i].size == size) {
			if (memcmp(run.s[i].code, code, size) != 0) {
				printf("refused (%s): hash collision\n", ident);
				return 0;
			}
			run.s[i].uses++;
			return 1;
		}
	/* static: they must survive a longjmp from fail() */
	static Buf body;
	static Gen g;
	memset(&body, 0, sizeof(body));
	memset(&g, 0, sizeof(g));
	g.c.base = code;
	g.c.end = size;
	g.o = &body;
	g.ind = 1;
	if (setjmp(g.fail)) {
		printf("refused (%s): %s\n", ident, g.err);
		bfree(&body);
		return 0;
	}
	gen_block(&g, 'b');
	if (g.c.pos != size)
		fail(&g, "script of %u bytes, read %zu", size, g.c.pos);

	if (run.n == run.cap) {
		run.cap = run.cap ? run.cap * 2 : 256;
		run.s = realloc(run.s, sizeof(Script) * (size_t)run.cap);
	}
	Script *s = &run.s[run.n++];
	memset(s, 0, sizeof(*s));
	s->hash = h;
	s->size = size;
	s->code = malloc(size ? size : 1);
	memcpy(s->code, code, size);
	s->uses = 1;
	snprintf(s->name, sizeof(s->name), "ms_script_%016llx_%u", (unsigned long long)h, size);
	snprintf(s->first, sizeof(s->first), "%s", ident);

	char *dec = script_decompile(code, size, 1);
	char *named = names_apply(dec);
	bput(o, "\n/* %s, %u bytes\n * ", ident, size);
	comment_text(o, named);
	free(named);
	free(dec);
	bput(o, "\n */\nMsValue %s(MsFrame *f) {\n\tMsValue rv = val_empty();\n\t(void)f;\n%s", s->name, body.s ? body.s : "");
	if (g.done)
		bput(o, "done:\n");
	bput(o, "\treturn rv;\n}\n");
	bfree(&body);
	return 1;
}

static void write_file(const char *name, const Buf *b) {
	char path[700];
	snprintf(path, sizeof(path), "%s/%s", run.outdir, name);
	FILE *fp = fopen(path, "wb");
	if (!fp)
		ms_fatal("transpile: cannot write %s", path);
	fwrite(b->s, 1, b->n, fp);
	fclose(fp);
	for (size_t i = 0; i < b->n; i++)
		if (b->s[i] == '\n')
			run.lines++;
}

void transpile_context(uint16_t context) {
	Buf o = {0};
	const char *cname = names_get('c', context);
	bput(&o, "/* Generated by \"msdump transpile\" from the scripts of context %u%s%s%s. Do not edit:\n"
		" * regenerate (docs\\RECOMPILATION.md). Every function replaces the bytecode block with the\n"
		" * same hash and length; see engine\\script_rt.h for the runtime API. */\n"
		"#include <stdint.h>\n#include <stddef.h>\n\n#include \"script_rt.h\"\n", context,
		cname ? " (" : "", cname ? cname : "", cname ? ")" : "");
	char ident[240];
	for (int i = 0; i < MS_MAX_ID; i++) {
		MsActor *a = ms_god.actors[i];
		if (!a || a->context != context)
			continue;
		for (int k = 0; k < a->nresp; k++) {
			MsResponse *r = &a->resp[k];
			const char *an = names_get('a', a->id);
			snprintf(ident, sizeof(ident), "context %u, actor %u %s%s%s, event 0x%x%s%s", context, a->id,
				a->vt->name, an ? " " : "", an ? an : "", r->event, r->arg.type ? " " : "",
				r->arg.type ? val_debug(&r->arg) : "");
			run.responses++;
			if (!translate(&o, ident, r->code, r->size))
				run.refused++;
		}
	}
	for (int i = 0; i < MS_MAX_ID; i++) {
		MsFunction *f = ms_god.functions[i];
		if (!f || f->context != context)
			continue;
		const char *fn = names_get('f', f->id);
		snprintf(ident, sizeof(ident), "context %u, function_%u%s%s", context, f->id, fn ? " " : "", fn ? fn : "");
		run.functions++;
		if (!translate(&o, ident, f->code, f->size))
			run.refused++;
	}
	char name[64];
	snprintf(name, sizeof(name), "scripts_%u.c", context);
	write_file(name, &o);
	bfree(&o);
	run.contexts++;
}

static int cmp_script(const void *x, const void *y) {
	const Script *a = x, *b = y;
	if (a->hash != b->hash)
		return a->hash < b->hash ? -1 : 1;
	return a->size < b->size ? -1 : a->size > b->size;
}

int transpile_end(void) {
	qsort(run.s, (size_t)run.n, sizeof(Script), cmp_script);
	Buf o = {0};
	bput(&o, "/* Generated by \"msdump transpile\": index of the recompiled scripts, sorted by bytecode\n"
		" * hash and length (engine\\script.c looks them up with a binary search). Do not edit. */\n"
		"#include <stddef.h>\n\n#include \"script_rt.h\"\n\n");
	for (int i = 0; i < run.n; i++)
		bput(&o, "MsValue %s(MsFrame *f);\n", run.s[i].name);
	bput(&o, "\nconst MsNativeScript ms_native_scripts[] = {\n");
	for (int i = 0; i < run.n; i++) {
		Script *s = &run.s[i];
		bput(&o, "\t{0x%016llxull, %u, %s}, /* %s", (unsigned long long)s->hash, s->size, s->name, s->first);
		if (s->uses > 1)
			bput(&o, " (+%d)", s->uses - 1);
		bput(&o, " */\n");
	}
	bput(&o, "\t{0, 0, NULL} /* sentinel */\n};\nconst int ms_native_count = %d;\n", run.n);
	write_file("scripts_index.c", &o);
	bfree(&o);
	printf("transpile: %d contexts, %d responses, %d functions -> %d unique C functions, %d refused, "
		"%ld lines of C\n", run.contexts, run.responses, run.functions, run.n, run.refused, run.lines);
	for (int i = 0; i < run.n; i++)
		free(run.s[i].code);
	free(run.s);
	return run.refused;
}

int transpile_scripts(const char *path, int n, const char *const *idents, const uint8_t *const *codes,
	const uint32_t *sizes) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char *slash = strrchr(dir, '/');
	char *bslash = strrchr(dir, '\\');
	if (bslash > slash)
		slash = bslash;
	const char *file = slash ? slash + 1 : dir;
	char name[512];
	snprintf(name, sizeof(name), "%s", file);
	if (slash)
		*slash = 0;
	else
		snprintf(dir, sizeof(dir), ".");
	transpile_begin(dir);
	Buf o = {0};
	bput(&o, "/* Generated by msdump transpile (test scripts). */\n#include <stdint.h>\n#include <stddef.h>\n\n"
		"#include \"script_rt.h\"\n");
	int ok = 0;
	for (int i = 0; i < n; i++)
		ok += translate(&o, idents[i], codes[i], sizes[i]);
	qsort(run.s, (size_t)run.n, sizeof(Script), cmp_script);
	bput(&o, "\nconst MsNativeScript ms_native_scripts[] = {\n");
	for (int i = 0; i < run.n; i++)
		bput(&o, "\t{0x%016llxull, %u, %s},\n", (unsigned long long)run.s[i].hash, run.s[i].size, run.s[i].name);
	bput(&o, "\t{0, 0, NULL}\n};\nconst int ms_native_count = %d;\n", run.n);
	write_file(name, &o);
	bfree(&o);
	for (int i = 0; i < run.n; i++)
		free(run.s[i].code);
	free(run.s);
	memset(&run, 0, sizeof(run));
	return ok;
}

int transpile_single(const char *path, const char *ident, const uint8_t *code, uint32_t size) {
	return transpile_scripts(path, 1, &ident, &code, &size) == 1;
}
