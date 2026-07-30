// FLAGS: --dump-ast
// CHECK: IF a
// CHECK: IF b
// CHECK: ELSE
// CHECK: FOR
// CHECK: FORCOND (i < 10)
// CHECK: FORSTEP (i post++)
// CHECK: WHILE (a < b)
// CHECK: BREAK
// CHECK: CONTINUE
// CHECK: LABEL again
// CHECK: GOTO again
// The dangling `else` binds to the INNER `if` — recursive descent gives
// that for free, and the indentation in the dump is the proof.
int a, b;
void f(void) {
    if (a) if (b) a = 1; else a = 2;
    for (int i = 0; i < 10; i++) a += i;
    while (a < b) { if (a) break; else continue; }
again:
    a = 0;
    if (a) goto again;
}
