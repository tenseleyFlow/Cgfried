// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// Duff's-device copy vs a byte-compare oracle.
// EXIT_CODE: 0
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
int main(void) {
    char src[13], dst[13];
    int k;
    for (k = 0; k < 13; k++) { src[k] = (char)(k * 7 + 1); dst[k] = 0; }
    duff(dst, src, 13);
    for (k = 0; k < 13; k++)
        if (dst[k] != src[k])
            return 1;
    return 0;
}
