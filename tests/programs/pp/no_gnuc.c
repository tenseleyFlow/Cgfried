// FLAGS: -std=c17 -E
// CHECK: no_gnuc_defined
#if defined(__GNUC_MINOR__) || defined(__GNUC_PATCHLEVEL__) ||                 \
    defined(__GNUC_STDC_INLINE__) || defined(__GNUC_GNU_INLINE__) ||           \
    defined(__USER_LABEL_PREFIX__)
#error GNU identity leaked into ISO mode
#endif
#ifdef __GNUC__
gnuc_defined
#else
no_gnuc_defined
#endif
