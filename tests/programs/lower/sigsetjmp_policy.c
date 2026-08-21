// IR-H-06: glibc expands POSIX sigsetjmp to __sigsetjmp.  The emitted
// returns-twice marker must survive -O2 and keep the caller's locals pinned.
// FLAGS: -O2 -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func i32 @uses(ptr %0) setjmp {
// IR_CHECK: alloca 4
// IR_CHECK: call i32 @__sigsetjmp
#define _POSIX_C_SOURCE 200809L
#include <setjmp.h>

int uses(sigjmp_buf env)
{
    int pinned = 7;

    if (sigsetjmp(env, 1))
        return pinned;
    return 0;
}
