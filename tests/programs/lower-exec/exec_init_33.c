// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// A 33-byte object with one runtime element: template copy first, then
// the runtime store overrides — copy-then-store order observable here.
// EXIT_CODE: 42
int f(char v) {
    char t[33] = {1, 2, v, 4, 5, 6, 7, 8, 9, 10,
                  11, 12, 13, 14, 15, 16, 17, 18};
    return t[2] + t[17] + t[32];
}
int main(void) { return f(24); }
