// goto INTO a loop body is legal (it is not a VLA scope).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: L.inside
int f(int n) {
    int acc = 0;
    goto inside;
    while (acc < n) {
inside:
        acc += 2;
    }
    return acc;
}
