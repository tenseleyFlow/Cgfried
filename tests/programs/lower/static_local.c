// A block-scope static is a file-lifetime internal global with a
// mangled, deterministic name; a second call sees the same object.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: global @counter.0 size 4 align 4 internal init x2a000000
// IR_CHECK: load i32, @counter.0
int next(void) {
    static int counter = 42;
    return counter++;
}
