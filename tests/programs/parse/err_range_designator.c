// FLAGS: --dump-ast
// ERROR_EXPECTED: Sprint
// No silent stubs: an unimplemented GNU extension must hard-error and name
// the sprint that lands it, so a deferral can never masquerade as support.
int a[4] = { [1 ... 3] = 0 };
