#ifndef CGF_WARN_ID_H
#define CGF_WARN_ID_H

/* Kept independent of diag.h so Diag can carry a structural warning id
 * without creating an include cycle. */
typedef enum WarnId {
    WARN_NONE = 0,
#define W(id, flag, groups, defstate, level) id,
#include "warn/warnings.def"
#undef W
    WARN_COUNT
} WarnId;

#endif
