#include "cg/data.h"

#include <stdio.h>

/* THE RULE, measured against gcc on x86_64 AND aarch64, at -fno-pic, -fPIE
 * and -fPIC (both targets agree on every row):
 *
 *   const, initializer needs no relocation      -> .rodata
 *   const, needs a relocation, non-PIC          -> .rodata
 *   const, needs a relocation, PIC or PIE       -> .data.rel.ro
 *   otherwise, zero-initialized                 -> .bss
 *   otherwise                                   -> .data
 *
 * The PIC row is the one that matters and the one a first draft gets wrong.
 * Without dynamic relocations an address in an initializer is a link-time
 * constant, so .rodata is correct; under PIC or PIE the loader must WRITE
 * that word at startup, and putting it in .rodata hands the dynamic linker a
 * read-only page it has to modify. .data.rel.ro is the standard answer:
 * writable while relocations are applied, then mprotected read-only (RELRO).
 *
 * TWO deliberate simplifications, named rather than silent:
 *
 * - gcc splits `.local` variants (.data.rel.ro.local, .data.rel.local) when
 *   the referenced symbol cannot be preempted -- which depends on PIC vs PIE
 *   and on the symbol's binding. Both forms land in the same RELRO segment
 *   and the plain spelling is correct for either, so the split is a linker
 *   ordering optimization we decline for now rather than a behaviour we get
 *   wrong.
 * - a const zero-initialized object gets REAL BYTES in .rodata rather than a
 *   .bss reservation, because .bss is writable. gcc does the same, including
 *   for a const TENTATIVE definition, which therefore never becomes .comm.
 *
 * Constness here is the object's own qualifier, looked through array-element
 * qualification (6.7.3p9): `const char a[4]` qualifies the ELEMENT and still
 * belongs in .rodata. A const MEMBER of a non-const record does not count --
 * gcc puts `struct { const int x; } s;` in .data, and so do we. Lowering
 * settles that; by here it is one flag. */
CgDataSeg cg_global_segment(const IrGlobal *g, bool pic)
{
    bool zero_init = g->init == NULL;

    if (g->is_tls)
        return zero_init ? CG_SEG_TBSS : CG_SEG_TDATA;
    if (g->is_const) {
        if (g->nrelocs && pic)
            return CG_SEG_DATA_REL_RO;
        return CG_SEG_RODATA;
    }
    return zero_init ? CG_SEG_BSS : CG_SEG_DATA;
}

/* The default priority is NOT a sentinel meaning "unprioritized": gcc emits
 * the plain unnumbered section for the bare `constructor` and for an explicit
 * `constructor(65535)` alike, so the two really are the same request. The
 * linker places numbered sections ahead of the plain one, which is why a
 * priority orders a function BEFORE the unprioritized ones rather than after.
 */
void cg_init_array_section(char *buf, size_t bufsz, bool is_ctor, u16 prio)
{
    const char *base = is_ctor ? ".init_array" : ".fini_array";

    if (prio == (u16)CGF_INIT_PRIORITY_DEFAULT)
        snprintf(buf, bufsz, "%s", base);
    else
        snprintf(buf, bufsz, "%s.%05u", base, (unsigned)prio);
}
