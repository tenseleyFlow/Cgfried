// VLAs went LIVE in Sprint 20 (this fixture pinned the deferral until
// then): dynamic alloca + lazy stacksave + fall-out restore.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: imul i64
// IR_CHECK: stacksave
// IR_CHECK: alloca %
int f(int n) { { int a[n]; a[0] = 1; } return 0; }
