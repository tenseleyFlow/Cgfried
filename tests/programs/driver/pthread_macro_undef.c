// FLAGS: -U_REENTRANT -pthread -fsyntax-only
#ifdef _REENTRANT
#error "an explicit -U_REENTRANT must override -pthread"
#endif
int pthread_macro_undef;
