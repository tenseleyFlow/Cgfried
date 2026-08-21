#ifdef ALIAS_FIRST_SEEN
#error include_next reopened a path alias of its first directory
#endif
#define ALIAS_FIRST_SEEN
ALIAS_FIRST
#include_next <same.h>
