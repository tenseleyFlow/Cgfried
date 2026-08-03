// FLAGS: -fsyntax-only
// ERROR_EXPECTED: not yet supported for non-cgf attributes
void deferred(void) __attribute__((cold)); /* check_bans allow */
