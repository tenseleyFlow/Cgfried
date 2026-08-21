#ifdef DUP_SEEN
#error duplicate -I directory repeated its first header
#endif
#define DUP_SEEN
DUP_FIRST
#include_next <same.h>
