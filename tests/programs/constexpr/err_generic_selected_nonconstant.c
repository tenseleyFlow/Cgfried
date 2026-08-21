// FLAGS: -fsyntax-only
// ERROR_EXPECTED: not a constant expression
int runtime_value(void);
enum { SELECTED_CALL = _Generic(0, int: runtime_value(), default: 1) };
