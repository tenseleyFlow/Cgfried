// FLAGS: -fsyntax-only
// WARNING_EXPECTED: annotated cgf_returns_owned
// WARN_COUNT: 1
// EXIT_CODE: 0
#include <cgfried/memsafe.h>

static CGF_RETURNS_OWNED void *borrow(void *p);
// WARN_CHECK: mem-annotation-mismatch annotated cgf_returns_owned
static CGF_RETURNS_OWNED void *borrow(void *p)
{
    return p;
}
