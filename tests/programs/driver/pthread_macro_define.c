// FLAGS: -D_REENTRANT=7 -pthread -fsyntax-only
// WARNING_EXPECTED: '_REENTRANT' macro redefined
#ifndef _REENTRANT
#error "-pthread must define _REENTRANT"
#endif
_Static_assert(_REENTRANT == 7, "an explicit -D_REENTRANT must win");
int pthread_macro_define;
