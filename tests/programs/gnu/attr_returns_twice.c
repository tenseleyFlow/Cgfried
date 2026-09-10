// GNU returns_twice joins the exact-name setjmp family at one conservative
// lowering policy: the complete CALLER is marked, mem2reg leaves its local in
// memory, and the declaration property survives a later plain redeclaration.
// FLAGS: -std=gnu17 -O2 -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: sym @resume returns_twice
// IR_CHECK: func i32 @uses(i32 %0) setjmp
// IR_CHECK: alloca 4
// IR_CHECK: call i32 @resume(i32 %
// IR_CHECK-NOT: func i32 @clean(i32 %0) setjmp
int resume(int) __attribute__((returns_twice));
int resume(int);

int uses(int x)
{
    int pinned = x;

    resume(x);
    return pinned;
}

int clean(int x)
{
    return x + 1;
}
