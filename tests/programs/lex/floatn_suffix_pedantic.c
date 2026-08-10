// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: non-standard suffix on floating constant
// WARN_COUNT: 2
// WARN_CHECK: pedantic non-standard suffix on floating constant
double floatn_suffix_warns = 0x1p0F32;
double floatn_suffix_extension_suppresses = __extension__ 0x1p0F64x;
__extension__ _Float32 floatn_decl_extension_suppresses = 1.0F32;
__extension__ int floatn_int_decl_extension_suppresses = 1.0F32;
double floatn_suffix_warns_after_decl = 0x1p0F64x;
