// FLAGS: -fsyntax-only -Wpointer-arith
// WARN_COUNT: 0
unsigned long pointer_arith_object_size(void) { return sizeof(int); }
