// FLAGS: -fsyntax-only -std=c17 -Wstrict-prototypes
// WARN_COUNT: 1
// WARN_CHECK: strict-prototypes function declaration isn't a prototype
int no_prototype();
