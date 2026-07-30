// FLAGS: -fsyntax-only
// ERROR_EXPECTED: nested redefinition of 'struct T'
// FUZZER FINDING (Sprint 16, seed 1773). An inner definition of the tag
// whose completion is underway must not COMPLETE that tag with itself:
// doing so makes the struct a member of itself, and the anonymous-member
// search in find_member recursed forever (stack overflow, signal 11).
// The TagDecl.defining flag closes the window; the message is gcc's.
struct T { struct T { int x; } v; };
