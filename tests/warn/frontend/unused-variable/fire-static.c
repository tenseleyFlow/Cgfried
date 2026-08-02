// FLAGS: -S -Wunused-variable
// WARN_COUNT: 1

// WARN_CHECK: unused-variable 'static_value' defined but not used
static int static_value;
