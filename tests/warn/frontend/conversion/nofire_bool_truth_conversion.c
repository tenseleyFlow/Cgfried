// FLAGS: -fsyntax-only -Wconversion -Woverflow -Wsign-conversion
// WARN_COUNT: 0
_Bool bool_zero = 0;
_Bool bool_one = 1;
_Bool bool_two = 2;
_Bool bool_negative = -1;
_Bool bool_wide = 256;

_Bool bool_from_int(int value) { return value; }
_Bool bool_from_double(double value) { return value; }
