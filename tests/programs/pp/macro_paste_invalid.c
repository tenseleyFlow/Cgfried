// FLAGS: -E
// ERROR_EXPECTED: does not give a valid preprocessing token
#define cat(a,b) a##b
cat(x,+)
