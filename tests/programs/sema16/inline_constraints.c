// FLAGS: -fsyntax-only
// WARNING_EXPECTED: 'x' is static but declared in inline function 'f' which is not static
// 6.7.4p3: an inline DEFINITION may not define a modifiable static-storage
// object — the N copies inlined into other TUs would each get their own x,
// silently breaking the "one object" the author wrote. gcc warns (const
// statics are exempt, extern-inline is exempt: it IS the one definition);
// severity matched by observation, not by the constraint's letter.
inline int f(void) { static int x = 0; return x++; }
