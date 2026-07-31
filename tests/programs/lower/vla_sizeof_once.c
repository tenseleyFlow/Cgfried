// sizeof(vla) reads the CACHED size — the side-effecting bound runs
// exactly once, at the declaration.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: call i32 @bump()
// IR_CHECK: iadd i64 %2, %2
int bump(void);
long f(void) {
    int a[bump()];
    return (long)(sizeof(a) + sizeof(a));
}
