// A directory named by both -I and -isystem remains a system directory.
// FLAGS: -E -MM -Itests/fixtures/driver/system-alias -isystem tests/fixtures/driver/system-alias
// IR_CHECK: dep_mm_system_alias.o: tests/programs/driver/dep_mm_system_alias.c
// IR_CHECK-NOT: system_alias.h
#include <system_alias.h>
