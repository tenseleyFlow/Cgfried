// FLAGS: -fsyntax-only
// Both spellings on scalars; both spellings on ARRAYS qualify the
// element (corrected against gcc during implementation: the specifier
// names the element type, so `_Atomic(int) a[3]` is an array of atomic
// int, not an atomic array); pointers inside the specifier are fine, and
// _Alignas composes with _Atomic.
_Atomic int qual_scalar;
_Atomic(int) spec_scalar;
_Atomic int qual_arr[3];
_Atomic(int) spec_arr[3];
_Atomic(int *) atomic_ptr;
int *_Atomic ptr_to_atomic;
_Alignas(16) _Atomic int aligned_atomic;
