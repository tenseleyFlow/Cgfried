// FLAGS: -E -std=c17
// CHECK: f(1)
// (gcc deletes the comma in ISO modes too; -pedantic only pedwarns.
//  Verified against gcc -std=c17 — the sprint file predicted otherwise.)
#define LOG(fmt, ...) f(fmt, ##__VA_ARGS__)
LOG(1)
