// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: icmp eq ptr
// IR_CHECK: icmp ult ptr
int f(int *p, int *q) { return (p == 0) + (p < q); }
