// DoD 2: Duff's device — the case labels live INSIDE the do-while, so
// the two-pass lowering must have created their blocks before the loop
// body lowered. The switch table targets blocks the loop falls through.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: 0: sw.case
// IR_CHECK: do.cond
void duff(char *d, const char *s, int n) {
    int rounds = (n + 7) / 8;
    switch (n & 7) {
    case 0: do { *d++ = *s++;
    case 7: *d++ = *s++;
    case 6: *d++ = *s++;
    case 5: *d++ = *s++;
    case 4: *d++ = *s++;
    case 3: *d++ = *s++;
    case 2: *d++ = *s++;
    case 1: *d++ = *s++;
            } while (--rounds > 0);
    }
}
