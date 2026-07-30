// Statements after return are unreachable; lowering deletes the dead
// blocks (the verifier REJECTS orphans, and this fixture proves the
// cleanup by verifying clean).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: ret i32 1
int f(void) {
    return 1;
    return 2;
}
