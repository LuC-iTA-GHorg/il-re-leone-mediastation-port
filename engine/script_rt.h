/* Runtime API of the statically recompiled scripts (src\generated, written by
 * `msdump transpile`). Every script body becomes a C function MsValue fn(MsFrame *f); the
 * helpers below perform exactly the operations of the interpreter in script.c, which follows
 * the script interpreter of LIONKING.EXE (1198:dc7c..1198:ea8c, docs\ORIGINAL_SCRIPT.md), so
 * that the native and the interpreted path behave alike.
 *
 * Ownership: every helper that takes an MsValue by value consumes (frees) it, unless noted.
 * Evaluation order is the bytecode order: the generated code evaluates every operand into its
 * own temporary, one statement at a time, never inside a C argument list. */
#ifndef MS_SCRIPT_RT_H
#define MS_SCRIPT_RT_H

#include <stdint.h>

#include "constants.h"
#include "value.h"

#define MS_SCRIPT_MAX_LOOP 1000 /* while (not in LIONKING.EXE): warning and exit after this */

/* The activation record of a running script (the frame 1198:dba8 of the original, without the
 * bytecode cursor). code/size: the bytecode the native function replaces (rs_resume). */
typedef struct {
	MsValue *locals;
	int nlocals;
	MsValue *args;
	int nargs;
	uint16_t actor; /* for diagnostics */
	const uint8_t *code;
	uint32_t size;
} MsFrame;

typedef MsValue (*MsNativeFn)(MsFrame *f);

/* One compiled script, keyed by the bytecode it replaces (FNV-1a 64 hash and length). */
typedef struct {
	uint64_t hash;
	uint32_t size;
	MsNativeFn fn;
} MsNativeScript;

/* Generated index, sorted by (hash, size). ms_native_count excludes the sentinel. */
extern const MsNativeScript ms_native_scripts[];
extern const int ms_native_count;

/* Variables (1198:e970): local, parameter or global; NULL if it does not exist (id 0, index
 * out of range, unknown global or scope). Never an error. */
MsValue *rs_var(MsFrame *f, uint16_t id, uint16_t scope);
/* Indirect parameter: parameter id + _ftol(index) - 1 (consumes index). */
MsValue *rs_var_indirect(MsFrame *f, uint16_t id, MsValue index);
/* Reading a variable (1198:defa): a copy, empty if p is NULL. */
MsValue rs_get(const MsValue *p);
/* Assignment (1198:e3e0): copies v into *dst (an empty value too) and gives v; with no
 * variable (NULL) nothing is assigned and the result is empty. Consumes v. */
MsValue rs_assign(MsValue *dst, MsValue v);
/* Declare locals (1198:e87c): frees the previous ones, n new empty ones (n may be 0). */
void rs_locals(MsFrame *f, uint16_t n);
void rs_frame_free(MsFrame *f);

/* Condition of an if (1198:e2b4): 1 true, 0 false, -1 not a bool (the original then skips two
 * blocks: the generated code continues with rs_resume). Consumes c. */
int rs_if(MsValue c);
/* Condition of an if-else (1198:e31a): a non-bool is a fatal error (the original throws an
 * exception that ends the player). Consumes c. */
int rs_if_else(MsValue c);
/* Continues the script in the interpreter from byte pos, where an if with a non-bool condition
 * reads its second block size. cont describes the blocks that enclose the if, innermost
 * first: 't' the true block of an if-else (its end skips the else block), 'b' any other block;
 * the last one is the script body. Returns the value of the script. */
MsValue rs_resume(MsFrame *f, uint32_t pos, const char *cont);

/* Constructs that LIONKING.EXE does not have (opcodes 0xE0..0xE2, operands 0x9E..0xA1):
 * fatal error unless script_extensions (script.h) is set. */
void rs_extension(const char *what);
/* One iteration of a while (extension): checks the condition, counts the iteration (warning
 * and 0 past MS_SCRIPT_MAX_LOOP) and returns 1 if the body must run. */
int rs_while(MsFrame *f, const MsValue *c, int *iterations);

/* Operators (OP_OR..OP_MODULO, OP_NEGATE). */
MsValue rs_logic(int op, MsValue a, MsValue b);
MsValue rs_compare(int op, MsValue a, MsValue b);
MsValue rs_math(int op, MsValue a, MsValue b);
MsValue rs_negate(MsValue a);

/* Literals that are not plain constructors. */
MsValue rs_string(const char *s, uint16_t n);
MsValue rs_serialized(const uint8_t *data, uint32_t n); /* OPERAND_VARIABLE (extension) */

/* Calls. Arguments are consumed. rs_function_id / rs_method_id consume their value. */
uint32_t rs_function_id(MsValue v);
uint32_t rs_method_id(MsValue v);
MsValue rs_call(uint32_t id, MsValue *args, int n);
/* Method on the value of a variable target (1198:e72e evaluates the target before the
 * arguments): snapshot = rs_get(p) taken before the arguments, consumed. With script_extensions
 * a collection variable is changed in place by the collection methods. */
MsValue rs_method_var(MsFrame *f, uint32_t m, MsValue *p, MsValue snapshot, MsValue *args, int n);
/* Method on a target in place (may be NULL). */
MsValue rs_method(MsFrame *f, uint32_t m, MsValue *target, MsValue *args, int n);
/* Method on a computed value (consumed). */
MsValue rs_method_value(MsFrame *f, uint32_t m, MsValue target, MsValue *args, int n);

#endif
