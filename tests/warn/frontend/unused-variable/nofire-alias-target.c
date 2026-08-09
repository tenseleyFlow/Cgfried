// FLAGS: -S -Wunused-variable
// WARN_COUNT: 0
/* An `alias` reference is a `.set`, not a relocation, so nothing that scans
 * uses can see it -- which is why IPO had to learn the same fact separately
 * and delete a static function reachable only through its alias. The warning
 * engine had the same hole: musl's weak_alias(dummy, __stdout_used) shape
 * warned "defined but not used" for the target. gcc says nothing. */
static int real_object = 1;
extern int aliased_object __attribute__((alias("real_object")));
