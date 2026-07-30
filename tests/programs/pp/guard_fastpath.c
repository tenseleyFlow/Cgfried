// FLAGS: -E -Itests/fixtures/once
// ENV: CGF_PP_STATS=1
// CHECK: guarded_body
// CHECK: includes: 2 opened, 2 guard-skipped, 0 once-skipped
// The guard fast path skips re-includes without reading or tokenizing;
// #undef of the guard macro must force a genuine re-open (2nd "opened").
#include <guarded.h>
#include <guarded.h>
#include <guarded.h>
#undef GUARD_G_H
#include <guarded.h>
