// FLAGS: -S -Wall
// WARN_COUNT: 1
// WARN_CHECK: unused-function defined but not used
static int unused_function_fire(void) { return 1; }
