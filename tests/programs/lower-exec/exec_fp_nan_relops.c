// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// NaN through every relational operator: all ordered comparisons are
// false, != is true — the PF recipes at runtime.
// EXIT_CODE: 0
int main(void) {
    volatile double n = 0.0 / 0.0, one = 1.0;
    int bad = 0;
    if (n == one) bad++;
    if (n < one) bad++;
    if (n <= one) bad++;
    if (n > one) bad++;
    if (n >= one) bad++;
    if (!(n != one)) bad++;
    if (n == n) bad++;
    return bad;
}
