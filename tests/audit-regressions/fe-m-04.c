// XFAIL(audit): FE-M-04 an invalid initializer item causes three cascading errors
int a[3] = {1, struct S; 2};
int after;
