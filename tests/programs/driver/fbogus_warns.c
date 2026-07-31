// Unknown -f: warning, then the build CONTINUES to a working binary
// (hard-erroring breaks flag-probing configure scripts).
// FLAGS: -fbogus
// WARNING_EXPECTED: unrecognized command-line option '-fbogus'
// EXIT_CODE: 7
int main(void)
{
    return 7;
}
