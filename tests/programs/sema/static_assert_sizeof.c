// FLAGS: -fsyntax-only
// This was an ERROR_EXPECTED fixture in Sprint 12, pinning a deferral
// that named Sprint 14 ("the VALUE of sizeof needs object layout"). Sprint
// 14 landed layout, so the deferral is gone and the fixture flipped to an
// accept case — which is exactly what a deferral pinned by a fixture is
// for: it cannot be quietly forgotten, and the day it starts working the
// suite says so.
_Static_assert(sizeof(int) >= 2, "int too small");
_Static_assert(sizeof(long) == 8, "LP64");
_Static_assert(_Alignof(double) == 8, "double alignment");
struct S { long l; char c; };
_Static_assert(sizeof(struct S) == 16, "tail padding is part of sizeof");
