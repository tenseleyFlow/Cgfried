// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func i32 @f(i32 %0) unproto {
// IR_CHECK: call i32 @f()
// An old-style definition has concrete incoming parameters but exposes no
// prototype to callers, so the IR verifier must not impose strict arity.
int f(x)
int x;
{
    return x;
}

int g(void) { return f(); }
