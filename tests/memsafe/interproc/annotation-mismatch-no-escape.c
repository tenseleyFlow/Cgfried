// FLAGS: -fsyntax-only
// WARNING_EXPECTED: annotated cgf_no_escape
// WARN_COUNT: 1
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
static void *sink;

static void publish(void *p) CGF_NO_ESCAPE(1);
// WARN_CHECK: mem-annotation-mismatch annotated cgf_no_escape
static void publish(void *p)
{
    sink = p;
}
