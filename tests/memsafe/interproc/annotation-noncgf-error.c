// FLAGS: -fsyntax-only
// WARNING_EXPECTED: 'cold' attribute directive ignored
// Sprint 43 shipped a cgf_-only attribute parser, and this fixture pinned
// that anything else was an error. Sprint 55 classified the whole GNU
// attribute surface, so `cold` is now accepted-and-warned like any other
// hint whose only cost is a missed optimization.
//
// What the memsafe suite still needs from it is unchanged and is what the
// warning proves: a non-cgf attribute is RECOGNIZED as something else, not
// quietly absorbed into the ownership contract. An attribute that fell
// through into the cgf_ path would silently give a function a
// takes-ownership or returns-owned meaning nobody wrote -- which is a wrong
// analysis rather than a missing one, and no diagnostic would say so.
void deferred(void) __attribute__((cold)); /* check_bans allow */
