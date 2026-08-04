#ifndef CGF_CG_H
#define CGF_CG_H

/* Backend entry points. Shared target-independent allocation helpers live in
 * cg/shared.h; each target owns its MIR, constraints, and selection. */
#include "cg/arm64/mir.h"
#include "cg/shared.h"
#include "cg/x86_64/mir.h"

#endif
