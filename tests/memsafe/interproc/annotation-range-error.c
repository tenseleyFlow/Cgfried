// FLAGS: -fsyntax-only
// ERROR_EXPECTED: parameter index 2 is out of range
void bad_range(void *) __attribute__((cgf_no_escape(2))); /* check_bans allow */
