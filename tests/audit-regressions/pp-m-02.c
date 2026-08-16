// XFAIL(audit): PP-M-02 dynamic builtins lose their containing macro backtrace
#define DECLARATOR __COUNTER__
int DECLARATOR;
