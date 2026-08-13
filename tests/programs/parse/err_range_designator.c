// FLAGS: --dump-ast
// ERROR_EXPECTED: GNU range designators are not supported
// Sprint 55 classified this extension as deliberately unsupported. Keep the
// rejection explicit without pretending that a closed sprint will land it.
int a[4] = { [1 ... 3] = 0 };
