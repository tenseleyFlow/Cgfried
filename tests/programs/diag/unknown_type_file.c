// FLAGS: -fsyntax-only
// ERROR_EXPECTED: unknown type name 'u32'
u32 a;
u32 b;
u32 *c;
u32 f(u32 x) { u32 y = x; return y; }
u32 arr[4];
