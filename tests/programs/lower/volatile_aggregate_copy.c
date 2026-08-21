// Volatile aggregate reads and writes remain visible as memcpy markers, while
// an ordinary aggregate control stays unmarked.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: memcpy %0, @source, 8, align 4, volatile
// IR_CHECK: memcpy %0, @source, 8, align 4, volatile
// IR_CHECK: memcpy @sink, @plain, 8, align 4, volatile
// IR_CHECK: memcpy @plain, @plain, 8, align 4
// IR_CHECK-NOT: memcpy @plain, @plain, 8, align 4, volatile
struct pair {
    int first;
    int second;
};

volatile struct pair source;
volatile struct pair sink;
struct pair plain;

int copy_all(void)
{
    struct pair local = source;

    local = source;
    sink = plain;
    plain = plain;
    return local.first;
}
