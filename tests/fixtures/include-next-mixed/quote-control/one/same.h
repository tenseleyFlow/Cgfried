#ifdef QUOTE_CONTROL_SEEN
#error ordinary quote include_next repeated its first header
#endif
#define QUOTE_CONTROL_SEEN
QUOTE_FIRST
#include_next "same.h"
