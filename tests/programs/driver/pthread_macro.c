// FLAGS: -pthread -fsyntax-only
#ifndef _REENTRANT
#error "-pthread must define _REENTRANT"
#endif
_Static_assert(_REENTRANT == 1, "default _REENTRANT value");
int pthread_macro;
