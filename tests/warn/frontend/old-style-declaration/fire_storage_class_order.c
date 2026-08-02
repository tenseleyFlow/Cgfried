// FLAGS: -fsyntax-only -Wold-style-declaration
// WARN_COUNT: 1
// WARN_CHECK: old-style-declaration 'static' is not at beginning of declaration
const static int storage_after_qualifier = 1;
