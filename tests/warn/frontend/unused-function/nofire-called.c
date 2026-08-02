// FLAGS: -S -Wall
// WARN_COUNT: 0
static int used_helper(void) { return 1; }
int unused_function_called(void) { return used_helper(); }
