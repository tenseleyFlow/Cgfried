// FLAGS: -fdump-sema
// CHECK: func only_inline: int (void) [external] [defined] [inline-def emit-external=no]
// CHECK: func ext_inline: int (void) [external] [defined] [extern-inline emit-external=yes]
// CHECK: func flip_late: int (void) [external] [defined] [extern-inline emit-external=yes]
// CHECK: func flip_early: int (void) [external] [defined] [extern-inline emit-external=yes]
// CHECK: func st: int (void) [internal] [defined] [static-inline]
// The 6.7.4p7 matrix, decided at END of TU from EVERY declaration: all
// -inline-no-extern means "inline definition, do not emit here"; any
// extern (or any plain declaration, before OR after the body) flips the
// answer to "this TU provides the external definition". The differential
// (scripts/inline_diff.sh) proves each row against what gcc -S actually
// emits.
inline int only_inline(void) { return 1; }
extern inline int ext_inline(void) { return 2; }
inline int flip_late(void) { return 3; }
int flip_late(void);
int flip_early(void);
inline int flip_early(void) { return 4; }
static inline int st(void) { return 5; }
int use(void) { return st(); }
