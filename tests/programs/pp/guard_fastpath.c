// FLAGS: -E -Itests/fixtures/once
// ENV: CGF_PP_STATS=1
// Sprint 26 buffers -E text (multi-TU/-o/.S need it), so the in-process
// stats line now precedes the token text.
// CHECK: includes: 2 opened, 2 guard-skipped, 0 once-skipped
// CHECK: guarded_body
// The guard fast path skips re-includes without reading or tokenizing;
// #undef of the guard macro must force a genuine re-open (2nd "opened").
#include <guarded.h>
#include <guarded.h>
#include <guarded.h>
#undef GUARD_G_H
#include <guarded.h>
