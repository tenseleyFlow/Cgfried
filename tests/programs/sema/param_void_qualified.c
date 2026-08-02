// ERROR_EXPECTED: 'void' must be the only parameter and unnamed
// The parser consumes legal `(void)`; any surviving void parameter is bad.
int f(const void) { return 0; }
