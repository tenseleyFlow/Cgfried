#ifndef CGF_CG_DATA_H
#define CGF_CG_DATA_H

#include "ir/ir.h"

/* Where a global's bytes belong. This is a claim about the OBJECT and the
 * link mode, not about any machine, so both backends ask the same function
 * and cannot drift -- the x86 and arm64 emitters spell the segments
 * differently (ELF vs Mach-O) but must never disagree about which one a
 * given global gets. */
typedef enum CgDataSeg {
    CG_SEG_DATA,        /* writable, initialized */
    CG_SEG_BSS,         /* writable, zero-filled, no file bytes */
    CG_SEG_RODATA,      /* read-only for the life of the process */
    CG_SEG_DATA_REL_RO, /* written by the dynamic loader, then read-only */
    CG_SEG_TDATA,       /* thread-local, initialized */
    CG_SEG_TBSS         /* thread-local, zero-filled */
} CgDataSeg;

/* `pic` is true for -fPIC and -fpie alike: both make an address in an
 * initializer a load-time relocation rather than a link-time constant. */
CgDataSeg cg_global_segment(const IrGlobal *g, bool pic);

#endif
