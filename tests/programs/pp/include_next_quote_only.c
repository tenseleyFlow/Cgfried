// FLAGS: -E -P -iquotetests/fixtures/include-next-mixed/quote-only/q -Itests/fixtures/include-next-mixed/quote-only/one -Itests/fixtures/include-next-mixed/quote-only/two
// CHECK: QUOTE_ONLY
// CHECK: ANGLE_FIRST
// CHECK: ANGLE_SECOND
#include "same.h"
