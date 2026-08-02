// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 0
enum sign_compare_color { SIGN_RED, SIGN_GREEN };
int sign_compare_enum(unsigned int value) { return value == SIGN_GREEN; }
