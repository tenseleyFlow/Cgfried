// FLAGS: -fdump-sema
// CHECK: EXPR (b = (icast<_Bool> d)) : _Bool
// CHECK: EXPR (b = (icast<_Bool> p)) : _Bool
// CHECK: EXPR (i = (! d)) : int
// 6.3.1.2: conversion to _Bool is `!= 0`, NOT truncation — `(_Bool)0.5`
// is 1, where a naive float-to-int truncation would give 0. Pointers
// convert the same way, via != NULL. And `!` yields int, not _Bool: this
// is C, not C++.
void f(void) {
    _Bool b; double d; int *p; int i;
    b = d;
    b = p;
    i = !d;
}
