// FLAGS: -E
// ERROR_EXPECTED: missing terminating >
// ppfuzz seed 1123: an unterminated header name yielded an EMPTY filename,
// and fopen succeeds on directories - so we "opened" the include dir and
// spun forever. Only regular files are headers now.
#include <n