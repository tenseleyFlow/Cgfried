// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 0
int sign_compare_uchar(unsigned char u, int s) { return u < s; }
