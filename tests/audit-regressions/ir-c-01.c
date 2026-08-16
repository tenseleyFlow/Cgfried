// XFAIL(audit): IR-C-01 a 16-byte long-double aggregate is returned by hidden pointer
// SysV x86-64 classifies a 16-byte aggregate whose whole content is one x87
// long double as X87/X87UP, and an X87 return goes on the x87 stack in st0.
// No post-merger rule demotes it: MEMORY is not among its classes, and the
// size does not exceed two eightbytes. GCC agrees and returns it in st0.
//
// Cgfried instead gives it the MEMORY treatment -- the caller passes a hidden
// pointer in %rdi and the callee stores through it. The two disagree in BOTH
// directions, which is this project's own signature for a shared assumption
// rather than a placement bug:
//
//   gcc caller + gcc callee -> 2.5   (correct)
//   CGF caller + gcc callee -> 0.0   (reads its never-written sret buffer)
//   gcc caller + CGF callee -> SIGSEGV (stores through an unset %rdi)
//   CGF caller + CGF callee -> 2.5   (self-consistent, so no lane sees it)
//
// The gcc-caller direction is the dangerous one: the callee writes 16 bytes
// through whatever %rdi happened to hold.
//
// Aggregates LARGER than two eightbytes agree -- they really are MEMORY --
// which is why `struct { long double v; char c; }` is fine and only the
// exactly-16-byte shapes below diverge.
long double bare(long double x) { return x + 1; } /* correct today: st0 */

struct sole_long_double {
    long double v;
};

union union_long_double {
    long double v;
};

struct array_of_one {
    long double v[1];
};

struct sole_long_double ret_struct(void);
union union_long_double ret_union(void);
struct array_of_one ret_array(void);

void call_them(void)
{
    ret_struct();
    ret_union();
    ret_array();
}
