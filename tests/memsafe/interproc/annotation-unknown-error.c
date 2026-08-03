// FLAGS: -fsyntax-only
// ERROR_EXPECTED: unknown cgf_ attribute
void typo(void *) __attribute__((cgf_borrwos(1))); /* check_bans allow */
