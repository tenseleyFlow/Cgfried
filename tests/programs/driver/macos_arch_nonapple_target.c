// FLAGS: --target=arm64-linux -arch arm64 -fsyntax-only
// ERROR_EXPECTED: option '-arch arm64' requires target 'arm64-macos'
/* -arch is an Apple platform spelling, not a second alias for Cgfried's
 * target-complete --target option. */
int main(void)
{
    return 0;
}
