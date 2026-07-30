// if (x = f()): the assignment's VALUE feeds the condition.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: call i32 @get
// IR_CHECK: store i32
// IR_CHECK: icmp ne i32
int get(void);
int f(void) {
    int x;
    if ((x = get()))
        return x;
    return -1;
}
