// FLAGS: -std=gnu17 -O2 -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func i32 @probe()
// IR_CHECK: load i32, @alias_name
// IR_CHECK: store i32 11, @target
// IR_CHECK: isub i32 11,
// IR_CHECK-NOT: ret i32 1
// GVN may forward the post-store alias load to 11, but not reuse the stale
// pre-store load across a write through the target spelling.

int target = 7;
extern int alias_name __attribute__((alias("target")));

int probe(void)
{
    int before = alias_name;

    target = 11;
    return alias_name - before == 4 ? 0 : 1;
}
