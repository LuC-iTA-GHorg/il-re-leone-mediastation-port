/* Empty table of recompiled scripts: every script runs in the bytecode interpreter.
 * Linked into msdump (which never runs scripts natively) and into the game when no recompiled
 * scripts are available (no generated\ folder and no game data at build time). */
#include "script_rt.h"

const MsNativeScript ms_native_scripts[] = {{0, 0, NULL}};
const int ms_native_count = 0;
