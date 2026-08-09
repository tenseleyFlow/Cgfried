// FLAGS: -S -Wunused-function
// WARN_COUNT: 0
/* The function half of nofire-alias-target.c in unused-variable/: a static
 * function reached only through an `alias` is used, and gcc says nothing.
 * Both halves are here because the two warnings are decided by separate code
 * and musl's weak_alias produces both shapes. */
static void real_function(void) {}
void aliased_function(void) __attribute__((alias("real_function")));
