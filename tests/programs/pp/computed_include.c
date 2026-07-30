// FLAGS: -E -Itests/fixtures/once
// CHECK: guarded_body
// CHECK: once_body
#define AS_STRING "guarded.h"
#include AS_STRING
#define AS_ANGLE < oh.h
#include AS_ANGLE >
