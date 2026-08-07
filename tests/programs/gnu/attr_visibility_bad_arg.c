// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: unknown visibility 'sideways'
// An unknown visibility is an ERROR, not a silent default. Picking a
// visibility nobody asked for is the linkage equivalent of ignoring the
// attribute outright, and it would be just as invisible.
int g __attribute__((visibility("sideways")));
