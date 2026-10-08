// FLAGS: --target=arm64-macos -arch x86_64 -fsyntax-only
// ERROR_EXPECTED: architecture 'x86_64' is not supported for target
// 'arm64-macos'
/* Accepting and ignoring a mismatched -arch would silently emit the wrong
 * machine code. */
int main(void)
{
    return 0;
}
