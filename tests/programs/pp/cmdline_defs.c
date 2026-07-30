// FLAGS: -E -DALPHA -DBETA=2 -UALPHA
// CHECK: beta_defined
// CHECK: alpha_gone
#ifdef BETA
beta_defined
#endif
#ifndef ALPHA
alpha_gone
#endif
