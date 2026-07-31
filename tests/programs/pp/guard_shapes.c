// FLAGS: -E -Itests/fixtures/once
// ENV: CGF_PP_DUMP_GUARD=1
// Sprint 26 buffers -E text, so the GUARD dump lines now precede it.
// CHECK: GUARD tests/programs/pp/guard_shapes.c -
// CHECK: GUARD tests/fixtures/once/guarded.h GUARD_G_H
// CHECK: guarded_body
// The detector records the guard macro; no behavior change (Sprint 7's
// fast path is the consumer). The main file is guard-less: "-".
#include <guarded.h>
