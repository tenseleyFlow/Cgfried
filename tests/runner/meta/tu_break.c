// ERROR_EXPECTED: redefinition
int shared_name(void) { return 1; }
// TU-BREAK
int shared_name(void) { return 1; }
int shared_name(void) { return 2; }
