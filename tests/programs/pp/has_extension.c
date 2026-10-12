// FLAGS: -E -P
// CHECK: feature_query_defined
// CHECK: unsupported_feature_is_zero
// CHECK: raw_feature_operand
// CHECK: ordinary_feature_value 0

#if !defined(__has_extension)
#error __has_extension must be visible to defined
#endif
#ifndef __has_extension
#error __has_extension must be visible to ifdef directives
#endif

feature_query_defined

#if __has_extension(define_target_os_macros)
#error Cgfried must not claim an unsupported Clang extension
#else
unsupported_feature_is_zero
#endif

#define FEATURE_ALIAS define_target_os_macros
#if __has_extension(FEATURE_ALIAS)
#error a feature-query operand must not undergo macro expansion
#else
raw_feature_operand
#endif

ordinary_feature_value __has_extension(define_target_os_macros)
