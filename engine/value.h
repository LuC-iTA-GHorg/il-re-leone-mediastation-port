/* Media Script values (ScriptValue in the original): float, bool, time, param token, actor id,
 * string, collection, function id, method id. Values have value semantics: copying a value
 * copies its string and, deeply, its collection. */
#ifndef MS_VALUE_H
#define MS_VALUE_H

#include <stdint.h>

#include "datafile.h"

enum {
	VAL_EMPTY = 0,
	VAL_FLOAT = 1,
	VAL_BOOL = 2,
	VAL_TIME = 3,
	VAL_TOKEN = 4,
	VAL_ACTOR = 5,
	VAL_STRING = 6,
	VAL_COLLECTION = 7,
	VAL_FUNCTION = 8,
	VAL_METHOD = 9
};

typedef struct MsCollection MsCollection;

typedef struct {
	uint8_t type;
	union {
		double d;
		int b;
		uint32_t u; /* token, actor, function, method */
	} u;
	char *str;          /* VAL_STRING, owned */
	MsCollection *coll; /* VAL_COLLECTION, owned */
} MsValue;

struct MsCollection {
	MsValue *items;
	int count, cap;
};

/* The value serialized in data files (ScriptValue(stream)): u8 type then the payload. */
MsValue val_read(MsChunk *c);

MsValue val_empty(void);
MsValue val_float(double d);
MsValue val_bool(int b);
MsValue val_time(double d);
MsValue val_token(uint32_t t);
MsValue val_actor(uint32_t id);
MsValue val_string(const char *s);
MsValue val_function(uint32_t id);
MsValue val_method(uint32_t id);
MsValue val_new_collection(void);

MsValue val_copy(const MsValue *v);
void val_free(MsValue *v);
/* dst = copy of src (frees the old dst) */
void val_assign(MsValue *dst, const MsValue *src);

/* Conversions; a type mismatch is logged and gives 0 (like the original). */
double val_as_float(const MsValue *v);      /* float or time */
int val_as_bool(const MsValue *v);
uint32_t val_as_token(const MsValue *v);
uint32_t val_as_actor(const MsValue *v);
uint32_t val_as_function(const MsValue *v);
uint32_t val_as_method(const MsValue *v);
const char *val_as_string(const MsValue *v); /* "" if not a string */

/* Exact accessors of LIONKING.EXE (1190:4dc0, 4e06, 4e26, 4e80, 4ea0, 4ec0): the payload if the
 * value has that type, otherwise 0 / 0.0 / "" without any warning. val_float_of gives 0.0 for a
 * time too. The script VM uses only these. */
double val_float_of(const MsValue *v);
int val_bool_of(const MsValue *v);
double val_time_of(const MsValue *v);
uint32_t val_token_of(const MsValue *v);
uint32_t val_actor_of(const MsValue *v);
const char *val_string_of(const MsValue *v);
/* _ftol of the original (1180:1202): truncation, 0x80000000 when out of range. */
int32_t ms_ftol(double d);

/* Comparisons and arithmetic use the opcodes of the bytecode (OP_EQUALS... OP_MODULO), with the
 * semantics of LIONKING.EXE (1190:516a, 1190:5760, 1190:56ec): see docs\ORIGINAL_SCRIPT.md. */
int val_compare(int op, const MsValue *a, const MsValue *b);
MsValue val_math(int op, const MsValue *a, const MsValue *b);
MsValue val_negate(const MsValue *v);
/* Equality used to match script responses to event arguments (same type required). */
int val_equal(const MsValue *a, const MsValue *b);

void coll_push(MsCollection *c, const MsValue *v);
void coll_insert(MsCollection *c, int index, const MsValue *v);
MsValue coll_remove(MsCollection *c, int index);
void coll_clear(MsCollection *c);

/* "float: 1.000000" etc., in a rotating static buffer. */
const char *val_debug(const MsValue *v);
const char *val_type_name(int type);

#endif
