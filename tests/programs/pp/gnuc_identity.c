// FLAGS: -std=gnu17 -E
// CHECK: gnuc_identity_ok
// CHECK: stdc_inline_ok
// CHECK: user_label_prefix_empty

#if __GNUC__ == 8 && __GNUC_MINOR__ == 3 && __GNUC_PATCHLEVEL__ == 0
gnuc_identity_ok
#endif

#if __GNUC_STDC_INLINE__ == 1 && !defined(__GNUC_GNU_INLINE__)
    stdc_inline_ok
#endif

#define CAT_RAW(a, b) a##b
#define CAT(a, b) CAT_RAW(a, b)
    CAT(__USER_LABEL_PREFIX__, user_label_prefix_empty)
