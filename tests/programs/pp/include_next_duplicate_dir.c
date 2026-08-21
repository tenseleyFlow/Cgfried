// FLAGS: -E -P -Itests/fixtures/include-next-mixed/duplicate/one -Itests/fixtures/include-next-mixed/duplicate/one -Itests/fixtures/include-next-mixed/duplicate/two
// CHECK: DUP_FIRST
// CHECK: DUP_SECOND
#include <same.h>
