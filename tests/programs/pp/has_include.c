// FLAGS: -E -P -Itests/programs/pp
// CHECK: quoted_header_found
// CHECK: angled_header_found
// CHECK: missing_header_absent

#if !defined(__has_include)
#error __has_include must be visible to defined
#endif
#ifndef __has_include
#error __has_include must be visible to ifdef directives
#endif

#define LOCAL_HEADER "dep.h"
#if __has_include(LOCAL_HEADER)
quoted_header_found
#endif

#if __has_include(<dep.h>)
angled_header_found
#endif

#if !__has_include("cgfried-definitely-missing.h")
missing_header_absent
#endif
