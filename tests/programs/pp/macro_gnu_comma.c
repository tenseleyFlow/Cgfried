// FLAGS: -E -std=gnu17
// CHECK: f(1)
// CHECK: g(2, 3)
#define LOG(fmt, ...) f(fmt, ##__VA_ARGS__)
LOG(1)
#define LOG2(fmt, ...) g(fmt, ##__VA_ARGS__)
LOG2(2, 3)
