// FLAGS: -fsyntax-only -Wall -Wextra
// WARN_COUNT: 1
// WARN_CHECK: unused-parameter unused parameter 'p'
int unused_parameter_fire(int p) { return 1; }
