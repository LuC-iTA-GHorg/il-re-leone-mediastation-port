/* "Static recompilation" of the Media Script bytecode to C (msdump transpile). */
#ifndef MSDUMP_TRANSPILE_H
#define MSDUMP_TRANSPILE_H

#include <stdint.h>

/* Starts a run that writes OUTDIR\scripts_<context>.c and OUTDIR\scripts_index.c. */
void transpile_begin(const char *outdir);
/* Translates the scripts of a loaded context (actor responses and title functions). */
void transpile_context(uint16_t context);
/* Writes the index and prints the totals. Returns the number of scripts left to the
 * interpreter (translation refused). */
int transpile_end(void);
/* Writes one script and a one-entry ms_native_scripts table to PATH (tools\msdump\selftest.c).
 * Returns 0 if the translation was refused. */
int transpile_single(const char *path, const char *ident, const uint8_t *code, uint32_t size);
/* Several scripts into one file with their table; returns how many were translated (a refused
 * one prints why and stays with the interpreter). */
int transpile_scripts(const char *path, int n, const char *const *idents, const uint8_t *const *codes,
	const uint32_t *sizes);

/* Names table (tools\msdump\names.txt): readable names for the raw ids of variables, actors,
 * contexts and title functions, used only in the output of msdump (decompiled scripts and the
 * comments of the generated C). Lines: "KIND ID NAME  # note", KIND = var|actor|context|function,
 * NAME a C identifier. Returns the number of names read, -1 if the file cannot be read. */
int names_load(const char *path);
/* The name of an id (kind 'v', 'a', 'c', 'f') or NULL. No table loaded: always NULL. */
const char *names_get(char kind, unsigned id);
/* Copy of a decompiled text with var_N, actor_N and function_N replaced by their names
 * (free() it). */
char *names_apply(const char *text);

#endif
