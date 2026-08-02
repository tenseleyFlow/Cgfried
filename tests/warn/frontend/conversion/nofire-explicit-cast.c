// FLAGS: -fsyntax-only -Wconversion
// WARN_COUNT: 0
int conversion_explicit(long value) { return (int)value; }
