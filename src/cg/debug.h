#ifndef CGF_CG_DEBUG_H
#define CGF_CG_DEBUG_H

#include "ir/ir.h"
#include "target.h"
#include "util/arena.h"
#include "util/buf.h"

/* Target-neutral DWARF v4: the line table and the minimal CU DIE gdb needs.
 *
 * CFI is deliberately NOT here. `.eh_frame` describes a specific prologue in
 * a specific register numbering -- x86's `push %rbp` / `mov %rsp,%rbp` and
 * arm64's `stp x29,x30,[sp,#-N]!` share no bytes -- so each backend encodes
 * its own and this file never learns an architecture.
 *
 * What the line table needs from a backend turns out to be very little: the
 * ORDERED sequence of points where the source location changes, as
 * (label, loc) pairs. Everything else -- the file table, the presumed-path
 * remapping, the prologue_end rule, the CU DIE -- is a property of the IR and
 * the diagnostics engine, not of the machine. Sprint 29 wrote it against
 * X64Func because x86 was the only backend; the only thing that made it
 * x86-specific was walking blocks and instructions to find those pairs. */

typedef struct CgDebugRow {
    u32 label; /* renders as .Lloc_<func>_<label>; 0 is the entry row */
    u32 loc;   /* IrModule debug-location id; 0 = deliberately unattributed */
} CgDebugRow;

/* One function's rows, in emission order. `nrows` may be 0 for a function
 * whose every instruction inherited the same location. */
typedef struct CgDebugFunc {
    const CgDebugRow *rows;
    u32 nrows;
} CgDebugFunc;

/* Sprint 29: compiler-side encoders because the bundled assembler's
 * intentionally-small dialect has no .loc or LEB directives. These helpers
 * are public so the exact boundary encodings are unit-testable. */
size_t cg_dwarf_uleb(u64 value, u8 out[10]);
size_t cg_dwarf_sleb(i64 value, u8 out[10]);
bool cg_dwarf_special(i64 line_delta, u64 address_delta, u8 *opcode);

/* Emit .debug_line, .debug_abbrev and .debug_info for the whole module.
 * Callers emit their own .eh_frame first and call this only under -g.
 *
 * The labels this references (.Lloc_<f>_<n>, .Lfe<f>_0) are the backend's to
 * define; a row whose label the emitter never wrote is a link error, not a
 * silently wrong table. */
void cg_emit_debug_info(Arena *arena, const IrModule *m,
                        const CgDebugFunc *funcs, u32 nfuncs, const char *input,
                        const char *comp_dir, Buf *out);

#endif
