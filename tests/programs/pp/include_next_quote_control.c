// FLAGS: -E -P -iquotetests/fixtures/include-next-mixed/quote-control/one -iquotetests/fixtures/include-next-mixed/quote-control/two
// CHECK: QUOTE_FIRST
// CHECK: QUOTE_SECOND
#include "same.h"
