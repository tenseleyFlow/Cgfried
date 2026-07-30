// FLAGS: -fsyntax-only
// WARNING_EXPECTED: promoted argument 's' doesn't match prototype
void g(short);
void g(s) short s; { (void)s; }
