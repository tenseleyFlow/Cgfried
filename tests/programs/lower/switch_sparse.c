// The switch table is SORTED by case value (i64 extremes included);
// dense-vs-sparse is a backend decision the IR never makes.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: -9223372036854775808: sw.case
int f(long x) {
    switch (x) {
    case 9223372036854775807L: return 1;
    case -9223372036854775807L - 1: return 2;
    case 0: return 3;
    default: return 4;
    }
}
