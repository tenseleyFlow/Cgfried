// A branch spanning well past 127 bytes: relaxation must widen it to
// rel32 (agreement between assemblers is byte-checked by the objdiff
// lane; this pins that the shape exists in the corpus).
// FLAGS: -S
// ASM_CHECK: jne	.Lf0_2
// ASM_CHECK: .Lf0_3:
int wide(int x, int a) {
    if (x) {
        a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8;
        a += 9; a += 10; a += 11; a += 12; a += 13; a += 14; a += 15;
        a += 16; a += 17; a += 18; a += 19; a += 20; a += 21; a += 22;
        a += 23; a += 24; a += 25; a += 26; a += 27; a += 28; a += 29;
    }
    return a;
}
