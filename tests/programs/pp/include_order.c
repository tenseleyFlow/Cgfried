// FLAGS: -E -Itests/fixtures/include/one -Itests/fixtures/include/two
// CHECK: local_dep
// CHECK: one_dep
// CHECK: one_chain
// CHECK: two_chain
#include "dep.h"
#include <dep.h>
#include <chain.h>
