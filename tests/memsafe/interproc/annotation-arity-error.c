// FLAGS: -fsyntax-only
// ERROR_EXPECTED: requires one integer argument
void bad_arity(void *) __attribute__((cgf_borrows)); /* check_bans allow */
