// FLAGS: -E
// CHECK: #pragma GCC visibility push(default)
// CHECK: #pragma message ("in a macro")
// CHECK: #pragma STDC FP_CONTRACT OFF
// CHECK: after_pragmas
_Pragma("GCC visibility push(default)")
#define DO_PRAGMA(x) _Pragma(#x)
DO_PRAGMA(message ("in a macro"))
_Pragma("STDC FP_CONTRACT OFF")
after_pragmas
