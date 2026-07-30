// FLAGS: -E -Itests/fixtures/once
// CHECK: macro_once_body
// CHECK: end_marker
// _Pragma("once") produced by a macro must act at expansion point.
#include <ponce.h>
#include <ponce.h>
end_marker
