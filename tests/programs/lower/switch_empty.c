// An empty switch body: just the default edge to the join.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: switch i32
// IR_CHECK: sw.join
int f(int x) {
    switch (x) {}
    return 0;
}
