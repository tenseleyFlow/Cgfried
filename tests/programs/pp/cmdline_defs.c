// FLAGS: -E -DALPHA -DBETA=2 -UALPHA
// CHECK: beta_is_two
// CHECK: alpha_gone
#if BETA == 2
beta_is_two
#endif
#ifndef ALPHA
alpha_gone
#endif
