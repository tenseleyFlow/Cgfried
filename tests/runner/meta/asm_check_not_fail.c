// The F-S22-MIRCHECK trap, second occurrence. ASM_CHECK-NOT was registered,
// validated and then silently dropped -- missing from directive.c's add_dir
// list, exactly as MIR_CHECK had been -- so a fixture whose negative named a
// symbol that WAS present still passed. Caught only by mutating the new
// directive before trusting it.
//
// This pin is the direction that matters: a VIOLATED ASM_CHECK-NOT must FAIL.
// FLAGS: -S
// ASM_CHECK-NOT: {{^f:}}
int f(void) { return 1; }
