// FLAGS: --dump-ast
// ERROR_EXPECTED: multiple storage classes
// 6.7.1p2: at most one storage-class specifier, with _Thread_local the
// single exception (it may pair with static or extern).
static extern int x;
