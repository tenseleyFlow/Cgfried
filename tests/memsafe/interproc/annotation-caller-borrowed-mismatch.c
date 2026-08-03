// FLAGS: -fsyntax-only
// WARNING_EXPECTED: annotated cgf_returns_owned
// WARN_COUNT: 1
// EXIT_CODE: 0
#include <cgfried/memsafe.h>

static void *borrowed(void *p) CGF_RETURNS_BORROWED(1);
static void *borrowed(void *p)
{
    return p;
}

static CGF_RETURNS_OWNED void *caller(void *p);
// WARN_CHECK: mem-annotation-mismatch annotated cgf_returns_owned
static CGF_RETURNS_OWNED void *caller(void *p)
{
    return borrowed(p);
}
