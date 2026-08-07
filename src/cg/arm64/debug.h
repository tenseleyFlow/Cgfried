#ifndef CGF_CG_ARM64_DEBUG_H
#define CGF_CG_ARM64_DEBUG_H

#include "cg/arm64/mir.h"
#include "target.h"
#include "util/arena.h"

/* Assign deterministic .Lloc_<func>_<ordinal> labels after every MIR rewrite.
 * The arm64 mirror of x64_debug_prepare, and the same rule: a transition to
 * location 0 gets a row too, because DWARF line 0 means "no source" and
 * without it an unattributed instruction inherits a lying row. */
void a64_debug_prepare(A64Func *f);

/* One CU and one CIE per object. .eh_frame is emitted unconditionally;
 * .debug_* only under emit_debug. The line table itself is target-neutral and
 * comes from cg/debug.h -- only the CFI here is architecture-specific. */
void a64_emit_debug_sections(TargetSpec target, Arena *arena, const IrModule *m,
                             A64Func *const *funcs, u32 nfuncs,
                             const char *input, const char *comp_dir,
                             bool emit_debug, Buf *out);

#endif
