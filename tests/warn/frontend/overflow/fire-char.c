// FLAGS: -fsyntax-only
// WARN_COUNT: 2
// WARN_CHECK: overflow changes value from '300'
char overflow_char = 300;
// WARN_CHECK: overflow changes value from '300'
char overflow_braced_array[1] = {300};
