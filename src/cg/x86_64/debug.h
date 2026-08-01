#ifndef CGF_CG_X86_64_DEBUG_H
#define CGF_CG_X86_64_DEBUG_H

#include "cg/x86_64/mir.h"
#include "target.h"
#include "util/arena.h"

/* Sprint 29: compiler-side encoders because the bundled assembler's
 * intentionally-small dialect has no .loc or LEB directives. These helpers
 * are public so the exact boundary encodings are unit-testable. */
size_t x64_dwarf_uleb(u64 value, u8 out[10]);
size_t x64_dwarf_sleb(i64 value, u8 out[10]);
bool x64_dwarf_special(i64 line_delta, u64 address_delta, u8 *opcode);

/* Assign deterministic .Lloc_<func>_<ordinal> labels after every MIR rewrite.
 * A transition to location 0 receives a row too: DWARF line 0 means “no
 * source”, which prevents unattributed instructions inheriting a lying row. */
void x64_debug_prepare(X64Func *f);

/* One CU and one CIE per object. .eh_frame is emitted unconditionally;
 * .debug_* sections are controlled only by emit_debug (-g0/no -g disables).
 * The target is explicit so the eventual arm64 encoder cannot sniff the host.
 */
void x64_emit_debug_sections(TargetSpec target, Arena *arena, const IrModule *m,
                             X64Func *const *funcs, u32 nfuncs,
                             const char *input, const char *comp_dir,
                             bool emit_debug, Buf *out);

#endif
