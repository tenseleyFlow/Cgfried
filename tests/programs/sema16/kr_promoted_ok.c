// FLAGS: -fsyntax-only
// The compatible direction: the prototype already says the PROMOTED
// types, so int and double parameters match their K&R spellings — and a
// K&R parameter with no declaration at all is implicitly int.
void f(int, double);
void f(i, d) int i; double d; { (void)i; (void)d; }
int g(x) { return x; }
