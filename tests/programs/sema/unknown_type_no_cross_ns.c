// FLAGS: -fsyntax-only
// ERROR_EXPECTED: unknown type name 'bleh'
struct blah { int x; };
int blah_var;
bleh y;
