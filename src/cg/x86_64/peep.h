#ifndef CGF_X64_PEEP_H
#define CGF_X64_PEEP_H

#include "cg/x86_64/mir.h"

/* One complete local sweep.  Exposed so unit tests can pin changed-flag
 * honesty; production uses the capped fixpoint wrapper. */
bool x64_peep_once(X64Func *f);
void x64_peep(X64Func *f);

#endif
