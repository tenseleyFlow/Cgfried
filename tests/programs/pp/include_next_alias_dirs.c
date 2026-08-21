// FLAGS: -E -P -Itests/fixtures/include-next-mixed/aliases/one -I/proc/self/cwd/tests/fixtures/include-next-mixed/aliases/one -Itests/fixtures/include-next-mixed/aliases/one-link -I./tests/fixtures/include-next-mixed/aliases/one/. -Itests/fixtures/include-next-mixed/aliases/two
// CHECK: ALIAS_FIRST
// CHECK: ALIAS_SECOND
#include <same.h>
