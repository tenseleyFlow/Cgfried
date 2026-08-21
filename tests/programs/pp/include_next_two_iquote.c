// FLAGS: -E -P -iquotetests/fixtures/include-next-mixed/two-iquote/one -iquotetests/fixtures/include-next-mixed/two-iquote/two
// CHECK: IQUOTE_FIRST
// CHECK: IQUOTE_SECOND
#include "same.h"
