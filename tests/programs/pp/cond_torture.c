// FLAGS: -E
// CHECK: alive_one
// CHECK: alive_two
// CHECK: alive_three
#if 0
#garbage this is fine in a skipped group
@ $ ` junk tokens too
#if 1
never
#endif
#elif 2 > 1
alive_one
#else
dead
#endif
#ifdef NOT_DEFINED
dead
#ifndef ALSO_SKIPPED
dead
#endif
#else
alive_two
#endif
#if defined(X) || 1
alive_three
#endif
