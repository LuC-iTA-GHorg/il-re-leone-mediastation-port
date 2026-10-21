/* Media Script bytecode interpreter ("CodeChunk" of the original) and the built-in functions.
 * The bytecode is a tree of typed datums: a block is <u32 size> then expressions until an empty
 * one; an expression is a variable, a value or an operation (opcodes 0xC8..0xDF). */
#ifndef MS_SCRIPT_H
#define MS_SCRIPT_H

#include <stdint.h>

#include "value.h"

/* Runs a script response or function body: the recompiled C function if one matches the
 * bytecode (src\generated), otherwise the interpreter. args may be NULL. */
MsValue script_run(uint16_t actor_id, const uint8_t *code, uint32_t size, MsValue *args, int nargs);
/* Calls a title function (if defined) or a built-in function. */
MsValue script_call_function(uint32_t id, MsValue *args, int nargs);

/* Disassembles a block to text (for msdump and the trace). Returns a malloc'd string. */
char *script_decompile(const uint8_t *code, uint32_t size, int indent);

extern int script_trace; /* 1 = print every response/function executed */
/* 1 = always interpret the bytecode, ignoring the recompiled scripts (--interpret). */
extern int script_interpret;
/* 1 = also accept what LIONKING.EXE does not have (opcodes 0xE0..0xE2, operands 0x9E..0xA1,
 * collections, built-in functions other than 0x64..0x6A), as later engines do. Default 0: such
 * bytecode is a fatal error, like the exception of the original. */
extern int script_extensions;

/* The random generator of LIONKING.EXE (1188:d6c2): next value, 0..714024. Also meant for the
 * minigames (Zazu uses the same generator in the original). */
uint32_t script_random(void);
uint32_t script_random_max(void);
/* 1188:d6aa: sets the state (seed % 714025). Without it the first use seeds from rt_random(). */
void script_random_seed(uint32_t seed);

/* Calls method m on an actor id or a collection (target may be changed by collection methods).
 * caller is the actor id for diagnostics. Does not free args. */
MsValue script_call_method(uint16_t caller, MsValue *target, uint32_t m, MsValue *args, int n);
/* FNV-1a 64 hash of a bytecode block: the key of the recompiled scripts. */
uint64_t script_hash(const uint8_t *code, uint32_t size);
/* Names used by the decompiler (NULL if unknown). */
const char *script_method_name(unsigned m);
const char *script_function_name(unsigned f);

/* Provided by the runtime (video/audio) for the built-in functions. */
void video_effect_transition(MsValue *args, int nargs);
void video_set_transition_on_sync(MsValue *args, int nargs);
void audio_set_volume(double v);
double audio_get_volume(void);

#endif
