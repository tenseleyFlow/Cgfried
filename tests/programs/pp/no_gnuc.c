// FLAGS: -E
// CHECK: no_gnuc_defined
#ifdef __GNUC__
gnuc_defined
#else
no_gnuc_defined
#endif
